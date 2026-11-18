// Host-side validator for the P2 console package.
//
// It reads the files the cross-build actually produced and checks their
// structure: the param.sfo header/tables/required keys, the PNG chunk table and
// CRCs, and (when the payload SDK build was available) the fake-SELF and ELF
// headers. Nothing here compares against a stored blob, so a generator bug
// cannot pass by matching a golden file.
//
//   ./build/ps5_package_test <package-dir> [--assets-only]
//
// Without --assets-only, eboot.bin and harness.elf are required; the mode exists
// because the SFO and icon build on a host with no payload SDK, which is what
// CI has. bringup.elf is validated when present but is not required.

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void fail(const std::string& message) {
  std::printf("ps5 package: FAIL %s\n", message.c_str());
  ++g_failures;
}

std::uint16_t le16(const unsigned char* bytes) {
  return static_cast<std::uint16_t>(bytes[0] | (bytes[1] << 8));
}

std::uint32_t le32(const unsigned char* bytes) {
  return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) |
         (static_cast<std::uint32_t>(bytes[2]) << 16) |
         (static_cast<std::uint32_t>(bytes[3]) << 24);
}

std::uint32_t be32(const unsigned char* bytes) {
  return (static_cast<std::uint32_t>(bytes[0]) << 24) |
         (static_cast<std::uint32_t>(bytes[1]) << 16) |
         (static_cast<std::uint32_t>(bytes[2]) << 8) | static_cast<std::uint32_t>(bytes[3]);
}

std::uint32_t crc32(const unsigned char* data, std::size_t length) {
  std::uint32_t crc = 0xFFFFFFFFu;
  for (std::size_t index = 0; index < length; ++index) {
    crc ^= data[index];
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
  }
  return ~crc;
}

bool read_file(const std::filesystem::path& path, std::vector<unsigned char>& out) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    fail("cannot open " + path.string());
    return false;
  }
  input.seekg(0, std::ios::end);
  const std::streamoff size = input.tellg();
  if (size < 0) {
    fail("cannot size " + path.string());
    return false;
  }
  input.seekg(0, std::ios::beg);
  out.resize(static_cast<std::size_t>(size));
  if (size > 0) {
    input.read(reinterpret_cast<char*>(out.data()), size);
  }
  if (!input) {
    fail("short read on " + path.string());
    return false;
  }
  return true;
}

bool valid_title_id(const std::string& id) {
  if (id.size() != 9) {
    return false;
  }
  for (std::size_t index = 0; index < 4; ++index) {
    if (!std::isupper(static_cast<unsigned char>(id[index]))) {
      return false;
    }
  }
  for (std::size_t index = 4; index < 9; ++index) {
    if (!std::isdigit(static_cast<unsigned char>(id[index]))) {
      return false;
    }
  }
  return true;
}

bool valid_version(const std::string& version) {
  return version.size() == 5 && std::isdigit(static_cast<unsigned char>(version[0])) &&
         std::isdigit(static_cast<unsigned char>(version[1])) && version[2] == '.' &&
         std::isdigit(static_cast<unsigned char>(version[3])) &&
         std::isdigit(static_cast<unsigned char>(version[4]));
}

bool valid_content_id(const std::string& content_id, const std::string& title_id) {
  if (content_id.size() != 36) {
    return false;
  }
  if (content_id.compare(7, title_id.size(), title_id) != 0 ||
      content_id.compare(16, 4, "_00-") != 0 || content_id[6] != '-') {
    return false;
  }
  for (std::size_t index = 0; index < 2; ++index) {
    if (!std::isupper(static_cast<unsigned char>(content_id[index]))) {
      return false;
    }
  }
  for (std::size_t index = 2; index < 6; ++index) {
    if (!std::isdigit(static_cast<unsigned char>(content_id[index]))) {
      return false;
    }
  }
  for (std::size_t index = 20; index < 36; ++index) {
    const unsigned char character = static_cast<unsigned char>(content_id[index]);
    if (!std::isupper(character) && !std::isdigit(character)) {
      return false;
    }
  }
  return true;
}

struct SfoEntry {
  std::string key;
  std::uint16_t format = 0;
  std::uint32_t length = 0;
  std::uint32_t max_length = 0;
  std::uint32_t offset = 0;
};

constexpr std::uint16_t kUtf8 = 0x0204;
constexpr std::uint16_t kInt32 = 0x0404;

bool parse_sfo(const std::vector<unsigned char>& bytes,
               std::map<std::string, SfoEntry>& entries, std::uint32_t& data_start) {
  bool ok = true;
  if (bytes.size() < 20) {
    fail("param.sfo is shorter than its 20-byte header");
    return false;
  }
  const unsigned char* base = bytes.data();
  if (le32(base) != 0x46535000u) {
    fail("param.sfo magic is not \\0PSF");
    ok = false;
  }
  if (le32(base + 4) != 0x00000101u) {
    fail("param.sfo format version is not 0x00000101");
    ok = false;
  }
  const std::uint32_t key_start = le32(base + 8);
  data_start = le32(base + 12);
  const std::uint32_t count = le32(base + 16);
  if (count < 6 || count > 64) {
    fail("param.sfo entry count is implausible: " + std::to_string(count));
    return false;
  }
  const std::uint64_t entries_end = 20ull + 16ull * count;
  if (key_start < entries_end || key_start > bytes.size()) {
    fail("param.sfo key table offset is outside the entry table");
    ok = false;
  }
  if (data_start < key_start || data_start > bytes.size()) {
    fail("param.sfo data table offset is outside the file");
    return false;
  }

  for (std::uint32_t index = 0; index < count; ++index) {
    const unsigned char* record = base + 20 + 16 * index;
    SfoEntry entry;
    const std::uint16_t key_offset = le16(record);
    entry.format = le16(record + 2);
    entry.length = le32(record + 4);
    entry.max_length = le32(record + 8);
    entry.offset = le32(record + 12);

    if (entry.format != kUtf8 && entry.format != kInt32) {
      fail("param.sfo entry " + std::to_string(index) + " has format 0x" +
           std::to_string(entry.format));
      ok = false;
      continue;
    }
    if (entry.length == 0 || entry.max_length < entry.length) {
      fail("param.sfo entry " + std::to_string(index) + " has a bad length");
      ok = false;
      continue;
    }
    if (entry.offset % 4 != 0) {
      fail("param.sfo entry " + std::to_string(index) + " is not 4-byte aligned");
      ok = false;
    }
    if (entry.offset > bytes.size() - data_start ||
        entry.max_length > bytes.size() - data_start - entry.offset) {
      fail("param.sfo entry " + std::to_string(index) + " points outside the data table");
      ok = false;
      continue;
    }

    const std::uint64_t key_position = static_cast<std::uint64_t>(key_start) + key_offset;
    if (key_position < key_start || key_position >= data_start) {
      fail("param.sfo entry " + std::to_string(index) + " has a key outside the key table");
      ok = false;
      continue;
    }
    const unsigned char* key_begin = base + key_position;
    const unsigned char* key_end =
        static_cast<const unsigned char*>(std::memchr(key_begin, 0, data_start - key_position));
    if (key_end == nullptr || key_end == key_begin) {
      fail("param.sfo entry " + std::to_string(index) + " has an unterminated key");
      ok = false;
      continue;
    }
    entry.key.assign(reinterpret_cast<const char*>(key_begin),
                     static_cast<std::size_t>(key_end - key_begin));
    for (const char character : entry.key) {
      const unsigned char value = static_cast<unsigned char>(character);
      if (!std::isupper(value) && !std::isdigit(value) && character != '_') {
        fail("param.sfo key \"" + entry.key + "\" has unexpected characters");
        ok = false;
      }
    }
    if (entries.count(entry.key) != 0) {
      fail("param.sfo repeats key " + entry.key);
      ok = false;
      continue;
    }
    entries.emplace(entry.key, entry);
  }
  return ok;
}

bool sfo_utf8(const std::vector<unsigned char>& bytes, std::uint32_t data_start,
              const std::map<std::string, SfoEntry>& entries, const std::string& key,
              std::string& value) {
  const auto found = entries.find(key);
  if (found == entries.end()) {
    fail("param.sfo is missing key " + key);
    return false;
  }
  const SfoEntry& entry = found->second;
  if (entry.format != kUtf8) {
    fail("param.sfo key " + key + " is not a UTF-8 string");
    return false;
  }
  const unsigned char* data = bytes.data() + data_start + entry.offset;
  if (data[entry.length - 1] != 0) {
    fail("param.sfo key " + key + " is not NUL-terminated");
    return false;
  }
  if (std::memchr(data, 0, entry.length - 1) != nullptr) {
    fail("param.sfo key " + key + " contains an embedded NUL");
    return false;
  }
  value.assign(reinterpret_cast<const char*>(data), entry.length - 1);
  return true;
}

bool sfo_int32(const std::vector<unsigned char>& bytes, std::uint32_t data_start,
               const std::map<std::string, SfoEntry>& entries, const std::string& key,
               std::uint32_t& value) {
  const auto found = entries.find(key);
  if (found == entries.end()) {
    fail("param.sfo is missing key " + key);
    return false;
  }
  const SfoEntry& entry = found->second;
  if (entry.format != kInt32 || entry.length != 4 || entry.max_length != 4) {
    fail("param.sfo key " + key + " is not a 4-byte integer");
    return false;
  }
  value = le32(bytes.data() + data_start + entry.offset);
  return true;
}

struct PngInfo {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  unsigned bit_depth = 0;
  unsigned color_type = 0;
  std::size_t idat_bytes = 0;
  std::uint32_t chunk_count = 0;
};

bool validate_png(const std::vector<unsigned char>& bytes, PngInfo& info) {
  static const unsigned char kSignature[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
  bool ok = true;
  if (bytes.size() < 8 || std::memcmp(bytes.data(), kSignature, sizeof kSignature) != 0) {
    fail("icon0.png does not start with the PNG signature");
    return false;
  }
  std::size_t position = 8;
  bool first_chunk = true;
  bool saw_ihdr = false;
  bool saw_idat = false;
  bool saw_iend = false;
  bool checked_zlib_header = false;

  while (position + 12 <= bytes.size()) {
    const std::uint32_t length = be32(bytes.data() + position);
    if (length > bytes.size() - position - 12) {
      fail("icon0.png chunk length overruns the file");
      return false;
    }
    const unsigned char* type = bytes.data() + position + 4;
    const unsigned char* data = type + 4;
    const std::uint32_t stored_crc = be32(data + length);
    const std::uint32_t actual_crc = crc32(type, 4 + length);
    const std::string name(reinterpret_cast<const char*>(type), 4);
    if (stored_crc != actual_crc) {
      fail("icon0.png chunk " + name + " has a bad CRC");
      ok = false;
    }
    if (name == "IHDR") {
      if (!first_chunk) {
        fail("icon0.png IHDR is not the first chunk");
        ok = false;
      }
      if (length != 13) {
        fail("icon0.png IHDR is not 13 bytes");
        return false;
      }
      info.width = be32(data);
      info.height = be32(data + 4);
      info.bit_depth = data[8];
      info.color_type = data[9];
      if (data[10] != 0 || data[11] != 0 || data[12] != 0) {
        fail("icon0.png uses an unsupported compression/filter/interlace method");
        ok = false;
      }
      saw_ihdr = true;
    } else if (name == "IDAT") {
      saw_idat = true;
      info.idat_bytes += length;
      if (!checked_zlib_header && length >= 2) {
        const unsigned cmf = data[0];
        const unsigned flg = data[1];
        if (cmf != 0x78 || (cmf * 256 + flg) % 31 != 0) {
          fail("icon0.png IDAT does not begin with a valid zlib header");
          ok = false;
        }
        checked_zlib_header = true;
      }
    } else if (name == "IEND") {
      if (length != 0) {
        fail("icon0.png IEND is not empty");
        ok = false;
      }
      saw_iend = true;
      position += 12 + length;
      break;
    }
    first_chunk = false;
    position += 12 + length;
    ++info.chunk_count;
  }

  if (position != bytes.size()) {
    fail("icon0.png has trailing bytes after its last chunk");
    ok = false;
  }
  if (!saw_ihdr || !saw_idat || !saw_iend) {
    fail("icon0.png is missing IHDR, IDAT or IEND");
    return false;
  }
  if (info.width != 512 || info.height != 512) {
    fail("icon0.png is " + std::to_string(info.width) + "x" + std::to_string(info.height) +
         ", expected 512x512");
    ok = false;
  }
  if (info.bit_depth != 8) {
    fail("icon0.png is not 8 bits per channel");
    ok = false;
  }
  if (info.color_type != 6 && info.color_type != 2) {
    fail("icon0.png is not RGB or RGBA");
    ok = false;
  }
  if (info.idat_bytes == 0) {
    fail("icon0.png has no image data");
    ok = false;
  }
  return ok;
}

bool validate_elf(const std::vector<unsigned char>& bytes, const char* name, bool require_pie) {
  bool ok = true;
  if (bytes.size() < 64) {
    fail(std::string(name) + " is shorter than an ELF header");
    return false;
  }
  if (bytes[0] != 0x7f || bytes[1] != 'E' || bytes[2] != 'L' || bytes[3] != 'F') {
    fail(std::string(name) + " does not start with the ELF magic");
    return false;
  }
  if (bytes[4] != 2 || bytes[5] != 1) {
    fail(std::string(name) + " is not 64-bit little-endian");
    ok = false;
  }
  // elfldr spawns ET_DYN payloads only; a fixed-address ET_EXEC image fails
  // with "Error spawning payload" on hardware. eboot.bin is packed from a
  // separate non-PIE link by make_fself.py, which rejects ET_DYN.
  if (require_pie && le16(bytes.data() + 16) != 3) {
    fail(std::string(name) + " is not ET_DYN (elfldr only spawns PIE payloads)");
    ok = false;
  }
  if (!require_pie && le16(bytes.data() + 16) != 2) {
    fail(std::string(name) + " is not ET_EXEC (make_fself.py rejects ET_DYN)");
    ok = false;
  }
  if (le16(bytes.data() + 18) != 62) {
    fail(std::string(name) + " is not x86-64");
    ok = false;
  }
  return ok;
}

bool validate_fself(const std::vector<unsigned char>& bytes) {
  static const unsigned char kMagic[4] = {0x4f, 0x15, 0x3d, 0x1d};
  if (bytes.size() < 1024) {
    fail("eboot.bin is too small to be a fake SELF");
    return false;
  }
  if (std::memcmp(bytes.data(), kMagic, sizeof kMagic) != 0) {
    fail("eboot.bin does not start with the fSELF magic 4f153d1d");
    return false;
  }
  return true;
}

std::uintmax_t disk_size(const std::filesystem::path& path) {
  std::error_code error;
  const std::uintmax_t size = std::filesystem::file_size(path, error);
  return error ? 0 : size;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf("usage: %s <package-dir> [--assets-only]\n", argv[0]);
    return 2;
  }
  bool assets_only = false;
  for (int index = 2; index < argc; ++index) {
    if (std::strcmp(argv[index], "--assets-only") == 0) {
      assets_only = true;
    } else {
      std::printf("usage: %s <package-dir> [--assets-only]\n", argv[0]);
      return 2;
    }
  }

  const std::filesystem::path package = argv[1];
  std::error_code error;
  if (!std::filesystem::is_directory(package, error)) {
    fail("package directory does not exist: " + package.string());
    return 1;
  }
  const std::string title_id = package.filename().string();
  if (!valid_title_id(title_id)) {
    fail("package folder name is not a title id: " + title_id);
  }

  const std::filesystem::path sfo_path = package / "sce_sys" / "param.sfo";
  const std::filesystem::path icon_path = package / "sce_sys" / "icon0.png";

  std::vector<unsigned char> sfo;
  std::vector<unsigned char> icon;
  if (read_file(sfo_path, sfo)) {
    std::map<std::string, SfoEntry> entries;
    std::uint32_t data_start = 0;
    if (parse_sfo(sfo, entries, data_start)) {
      const char* required[] = {"APP_TYPE", "APP_VER", "CATEGORY", "CONTENT_ID",
                                "TITLE",    "TITLE_ID", "VERSION"};
      for (const char* key : required) {
        if (entries.count(key) == 0) {
          fail(std::string("param.sfo is missing required key ") + key);
        }
      }

      std::string got_title_id;
      if (sfo_utf8(sfo, data_start, entries, "TITLE_ID", got_title_id)) {
        if (got_title_id != title_id) {
          fail("param.sfo TITLE_ID " + got_title_id + " does not match the package folder " +
               title_id);
        }
        if (!valid_title_id(got_title_id)) {
          fail("param.sfo TITLE_ID is not four letters and five digits: " + got_title_id);
        }
      }

      std::string content_id;
      if (sfo_utf8(sfo, data_start, entries, "CONTENT_ID", content_id)) {
        if (!valid_content_id(content_id, title_id)) {
          fail("param.sfo CONTENT_ID is not Sony's 36-character form for " + title_id + ": " +
               content_id);
        }
      }

      std::string category;
      if (sfo_utf8(sfo, data_start, entries, "CATEGORY", category) && category != "gd") {
        fail("param.sfo CATEGORY is not \"gd\": " + category);
      }

      std::string title;
      if (sfo_utf8(sfo, data_start, entries, "TITLE", title) && title.empty()) {
        fail("param.sfo TITLE is empty");
      }

      std::string app_ver;
      std::string version;
      const bool have_app_ver = sfo_utf8(sfo, data_start, entries, "APP_VER", app_ver);
      const bool have_version = sfo_utf8(sfo, data_start, entries, "VERSION", version);
      if (have_app_ver && !valid_version(app_ver)) {
        fail("param.sfo APP_VER is not NN.NN: " + app_ver);
      }
      if (have_version && !valid_version(version)) {
        fail("param.sfo VERSION is not NN.NN: " + version);
      }
      if (have_app_ver && have_version && app_ver != version) {
        fail("param.sfo APP_VER and VERSION disagree");
      }

      std::uint32_t app_type = 0;
      if (sfo_int32(sfo, data_start, entries, "APP_TYPE", app_type) &&
          (app_type < 1 || app_type > 3)) {
        fail("param.sfo APP_TYPE is implausible: " + std::to_string(app_type));
      }
      for (const char* key : {"ATTRIBUTE", "PARENTAL_LEVEL", "SYSTEM_VER"}) {
        if (entries.count(key) != 0) {
          std::uint32_t ignored = 0;
          sfo_int32(sfo, data_start, entries, key, ignored);
        }
      }

      std::printf("ps5 package: param.sfo entries=%zu title_id=%s bytes=%zu\n", entries.size(),
                  got_title_id.c_str(), sfo.size());
    }
  }

  if (read_file(icon_path, icon)) {
    PngInfo info;
    if (validate_png(icon, info)) {
      std::printf("ps5 package: icon0.png %ux%u depth=%u color=%u idat=%zu bytes=%zu\n",
                  info.width, info.height, info.bit_depth, info.color_type, info.idat_bytes,
                  icon.size());
    }
  }

  if (!assets_only) {
    std::vector<unsigned char> eboot;
    std::vector<unsigned char> elf;
    if (read_file(package / "eboot.bin", eboot)) {
      validate_fself(eboot);
      std::printf("ps5 package: eboot.bin bytes=%zu\n", eboot.size());
    }
    if (read_file(package / "harness.elf", elf)) {
      validate_elf(elf, "harness.elf", true);
      std::printf("ps5 package: harness.elf bytes=%zu (ET_DYN, elfldr)\n", elf.size());
    }
    if (std::filesystem::exists(package / "bringup.elf")) {
      std::vector<unsigned char> bringup;
      if (read_file(package / "bringup.elf", bringup)) {
        validate_elf(bringup, "bringup.elf", true);
        std::printf("ps5 package: bringup.elf bytes=%zu (ET_DYN, elfldr)\n", bringup.size());
      }
    }
  } else {
    std::printf("ps5 package: binaries not required (--assets-only)\n");
  }

  if (g_failures != 0) {
    std::printf("ps5 package: %d failure(s) in %s (%ju bytes on disk)\n", g_failures,
                package.string().c_str(),
                static_cast<std::uintmax_t>(disk_size(sfo_path) + disk_size(icon_path)));
    return 1;
  }
  std::printf("ps5 package: OK %s\n", package.string().c_str());
  return 0;
}
