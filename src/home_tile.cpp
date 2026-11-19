#include "ps5h/home_tile.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace ps5h {
namespace {

constexpr int kIconSize = 512;
constexpr int kIconScale = 2;
constexpr std::size_t kCopyBuffer = 1024u * 1024u;
constexpr const char* kContentSuffix = "HARNESSHOMETILE1";

bool is_upper(char c) { return c >= 'A' && c <= 'Z'; }
bool is_digit(char c) { return c >= '0' && c <= '9'; }

bool valid_title_id(const std::string& id) {
  if (id.size() != 9) return false;
  for (int i = 0; i < 4; ++i) {
    if (!is_upper(id[static_cast<std::size_t>(i)])) return false;
  }
  for (int i = 4; i < 9; ++i) {
    if (!is_digit(id[static_cast<std::size_t>(i)])) return false;
  }
  const std::string prefix = id.substr(0, 4);
  return prefix != "PPSA" && prefix != "CUSA" && prefix != "NPXS";
}

std::string trim_slashes(std::string value) {
  while (!value.empty() && value.back() == '/') value.pop_back();
  return value;
}

std::string join_path(const std::string& parent, const std::string& child) {
  const std::string base = trim_slashes(parent);
  if (base.empty()) return "/" + child;
  return base + "/" + child;
}

std::string dirname_of(const std::string& path) {
  const std::size_t slash = path.rfind('/');
  if (slash == std::string::npos) return ".";
  if (slash == 0) return "/";
  return path.substr(0, slash);
}

long long now_seconds() { return static_cast<long long>(std::time(nullptr)); }

bool path_is_dir(const std::string& path) {
  struct stat info {};
  return ::lstat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

bool make_dir(const std::string& path, std::string* error) {
  struct stat info {};
  if (::lstat(path.c_str(), &info) == 0) {
    if (!S_ISDIR(info.st_mode)) {
      if (error != nullptr) *error = path + " exists and is not a directory";
      return false;
    }
  } else if (::mkdir(path.c_str(), 0777) != 0) {
    if (error != nullptr) {
      *error = "cannot create " + path + ": " + std::strerror(errno);
    }
    return false;
  }
  if (::chmod(path.c_str(), 0777) != 0) {
    if (error != nullptr) {
      *error = "cannot set 0777 on " + path + ": " + std::strerror(errno);
    }
    return false;
  }
  return true;
}

bool write_bytes(const std::string& path, const std::string& bytes,
                 std::string* error) {
  const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0777);
  if (fd < 0) {
    if (error != nullptr) {
      *error = "cannot open " + path + " for writing: " + std::strerror(errno);
    }
    return false;
  }
  if (::fchmod(fd, 0777) != 0) {
    if (error != nullptr) *error = "cannot set 0777 on " + path;
    ::close(fd);
    ::unlink(path.c_str());
    return false;
  }
  std::size_t written = 0;
  while (written < bytes.size()) {
    const ssize_t amount = ::write(fd, bytes.data() + written, bytes.size() - written);
    if (amount < 0) {
      if (errno == EINTR) continue;
      if (error != nullptr) {
        *error = "cannot write " + path + ": " + std::strerror(errno);
      }
      ::close(fd);
      ::unlink(path.c_str());
      return false;
    }
    written += static_cast<std::size_t>(amount);
  }
  if (::close(fd) != 0) {
    if (error != nullptr) *error = "cannot close " + path;
    ::unlink(path.c_str());
    return false;
  }
  return true;
}

bool copy_file(const std::string& source, const std::string& destination,
               std::string* error) {
  const int input = ::open(source.c_str(), O_RDONLY);
  if (input < 0) {
    if (error != nullptr) {
      *error = "cannot read " + source + ": " + std::strerror(errno);
    }
    return false;
  }
  const int output = ::open(destination.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0777);
  if (output < 0) {
    if (error != nullptr) {
      *error = "cannot create " + destination + ": " + std::strerror(errno);
    }
    ::close(input);
    return false;
  }
  if (::fchmod(output, 0777) != 0) {
    if (error != nullptr) *error = "cannot set 0777 on " + destination;
    ::close(input);
    ::close(output);
    ::unlink(destination.c_str());
    return false;
  }
  std::vector<char> buffer(kCopyBuffer);
  for (;;) {
    const ssize_t got = ::read(input, buffer.data(), buffer.size());
    if (got < 0) {
      if (errno == EINTR) continue;
      if (error != nullptr) {
        *error = "cannot read " + source + ": " + std::strerror(errno);
      }
      ::close(input);
      ::close(output);
      ::unlink(destination.c_str());
      return false;
    }
    if (got == 0) break;
    std::size_t written = 0;
    while (written < static_cast<std::size_t>(got)) {
      const ssize_t amount = ::write(
          output, buffer.data() + written, static_cast<std::size_t>(got) - written);
      if (amount < 0) {
        if (errno == EINTR) continue;
        if (error != nullptr) {
          *error = "cannot write " + destination + ": " + std::strerror(errno);
        }
        ::close(input);
        ::close(output);
        ::unlink(destination.c_str());
        return false;
      }
      written += static_cast<std::size_t>(amount);
    }
  }
  const bool input_ok = ::close(input) == 0;
  const bool output_ok = ::close(output) == 0;
  if (!input_ok || !output_ok) {
    if (error != nullptr) *error = "cannot close copied file";
    ::unlink(destination.c_str());
    return false;
  }
  return true;
}

bool remove_tree(const std::string& path, std::string* error) {
  struct stat info {};
  if (::lstat(path.c_str(), &info) != 0) {
    if (errno == ENOENT) return true;
    if (error != nullptr) {
      *error = "cannot inspect " + path + ": " + std::strerror(errno);
    }
    return false;
  }
  if (!S_ISDIR(info.st_mode)) {
    if (::unlink(path.c_str()) != 0) {
      if (error != nullptr) {
        *error = "cannot remove " + path + ": " + std::strerror(errno);
      }
      return false;
    }
    return true;
  }
  DIR* dir = ::opendir(path.c_str());
  if (dir == nullptr) {
    if (error != nullptr) {
      *error = "cannot open " + path + ": " + std::strerror(errno);
    }
    return false;
  }
  bool ok = true;
  while (dirent* entry = ::readdir(dir)) {
    const std::string name = entry->d_name;
    if (name == "." || name == "..") continue;
    if (!remove_tree(join_path(path, name), error)) {
      ok = false;
      break;
    }
  }
  ::closedir(dir);
  if (!ok) return false;
  if (::rmdir(path.c_str()) != 0) {
    if (error != nullptr) {
      *error = "cannot remove " + path + ": " + std::strerror(errno);
    }
    return false;
  }
  return true;
}

bool path_is_within(const std::string& child, const std::string& parent) {
  const std::string base = trim_slashes(parent);
  const std::string value = trim_slashes(child);
  return value == base || value.rfind(base + "/", 0) == 0;
}

bool write_marker(const std::string& path, const std::string& title_id,
                  std::string* error) {
  return write_bytes(path, "removed " + title_id + "\n", error);
}

// ---------------------------------------------------------------------------
// Embedded tile art: a near-black PS5 field, a soft blue light, a white H
// monogram and a single blue bridge. Rendering is supersampled and filtered,
// so the shell's rounded mask does not expose jagged edges.

struct Rgba {
  int r = 0;
  int g = 0;
  int b = 0;
  int a = 255;
};

struct Gradient {
  int r0, g0, b0;
  int r1, g1, b1;

  Rgba at(int y, int height) const {
    const double t = height <= 1 ? 0.0
                                 : std::min(1.0, std::max(0.0,
                                       static_cast<double>(y) / (height - 1)));
    return Rgba{static_cast<int>(std::lround(r0 + (r1 - r0) * t)),
                static_cast<int>(std::lround(g0 + (g1 - g0) * t)),
                static_cast<int>(std::lround(b0 + (b1 - b0) * t)), 255};
  }
};

class IconCanvas {
 public:
  IconCanvas(int width, int height)
      : width_(width), height_(height),
        pixels_(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u,
                0) {}

  int width() const { return width_; }
  int height() const { return height_; }
  const std::vector<unsigned char>& pixels() const { return pixels_; }

  void set_pixel(int x, int y, const Rgba& color) {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return;
    const std::size_t index =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
         static_cast<std::size_t>(x)) * 4u;
    pixels_[index] = static_cast<unsigned char>(std::min(255, std::max(0, color.r)));
    pixels_[index + 1] = static_cast<unsigned char>(std::min(255, std::max(0, color.g)));
    pixels_[index + 2] = static_cast<unsigned char>(std::min(255, std::max(0, color.b)));
    pixels_[index + 3] = static_cast<unsigned char>(std::min(255, std::max(0, color.a)));
  }

  void blend(int x, int y, const Rgba& color) {
    if (x < 0 || y < 0 || x >= width_ || y >= height_ || color.a <= 0) return;
    const double alpha = static_cast<double>(std::min(255, color.a)) / 255.0;
    const double inverse = 1.0 - alpha;
    const std::size_t index =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
         static_cast<std::size_t>(x)) * 4u;
    pixels_[index] = static_cast<unsigned char>(std::lround(
        std::min(255, std::max(0, color.r)) * alpha + pixels_[index] * inverse));
    pixels_[index + 1] = static_cast<unsigned char>(std::lround(
        std::min(255, std::max(0, color.g)) * alpha + pixels_[index + 1] * inverse));
    pixels_[index + 2] = static_cast<unsigned char>(std::lround(
        std::min(255, std::max(0, color.b)) * alpha + pixels_[index + 2] * inverse));
    pixels_[index + 3] = static_cast<unsigned char>(std::lround(
        std::min(255, color.a) + pixels_[index + 3] * inverse));
  }

  template <typename ColorAt>
  void fill_round_rect(int x0, int y0, int x1, int y1, int radius,
                       ColorAt color_at) {
    const int left = std::max(0, x0);
    const int right = std::min(width_, x1);
    const int top = std::max(0, y0);
    const int bottom = std::min(height_, y1);
    const int safe_radius = std::max(0, std::min(radius, std::min(x1 - x0, y1 - y0) / 2));
    for (int y = top; y < bottom; ++y) {
      for (int x = left; x < right; ++x) {
        const int cx = std::min(std::max(x, x0 + safe_radius), x1 - safe_radius);
        const int cy = std::min(std::max(y, y0 + safe_radius), y1 - safe_radius);
        const int dx = x - cx;
        const int dy = y - cy;
        if (dx * dx + dy * dy <= safe_radius * safe_radius) {
          blend(x, y, color_at(y));
        }
      }
    }
  }

  template <typename ColorAt>
  void fill_circle(int cx, int cy, int radius, ColorAt color_at) {
    const int left = std::max(0, cx - radius);
    const int right = std::min(width_, cx + radius);
    const int top = std::max(0, cy - radius);
    const int bottom = std::min(height_, cy + radius);
    const int radius_sq = radius * radius;
    for (int y = top; y < bottom; ++y) {
      for (int x = left; x < right; ++x) {
        const int dx = x - cx;
        const int dy = y - cy;
        if (dx * dx + dy * dy <= radius_sq) blend(x, y, color_at(y));
      }
    }
  }

  void arc(int cx, int cy, int radius, int thickness, int start_degrees,
           int end_degrees, const Rgba& color) {
    const int left = std::max(0, cx - radius - thickness);
    const int right = std::min(width_, cx + radius + thickness);
    const int top = std::max(0, cy - radius - thickness);
    const int bottom = std::min(height_, cy + radius + thickness);
    for (int y = top; y < bottom; ++y) {
      for (int x = left; x < right; ++x) {
        const int dx = x - cx;
        const int dy = y - cy;
        const double distance = std::sqrt(static_cast<double>(dx * dx + dy * dy));
        if (std::abs(distance - radius) > thickness / 2.0) continue;
        double angle = std::atan2(static_cast<double>(dy), static_cast<double>(dx)) *
                       180.0 / 3.14159265358979323846;
        if (angle < 0) angle += 360.0;
        if (angle >= start_degrees && angle <= end_degrees) blend(x, y, color);
      }
    }
  }

 private:
  int width_;
  int height_;
  std::vector<unsigned char> pixels_;
};

IconCanvas paint_icon() {
  const int size = kIconSize * kIconScale;
  IconCanvas canvas(size, size);
  const int center_x = size / 2;
  const int center_y = static_cast<int>(size * 0.43);
  const double max_radius = std::sqrt(static_cast<double>(size * size) * 2.0) / 2.0;

  // #0d0e12 ground with a restrained #0070d1 bloom. No flat placeholder fill.
  for (int y = 0; y < size; ++y) {
    const double t = static_cast<double>(y) / (size - 1);
    const double base_r = 6 + (13 - 6) * t;
    const double base_g = 7 + (14 - 7) * t;
    const double base_b = 12 + (18 - 12) * t;
    for (int x = 0; x < size; ++x) {
      const double dx = x + 0.5 - center_x;
      const double dy = y + 0.5 - center_y;
      const double distance = std::sqrt(dx * dx + dy * dy) / max_radius;
      double glow = std::max(0.0, 1.0 - distance);
      glow = glow * glow * 0.24;
      const double tight = std::max(0.0, 1.0 - distance * 2.7);
      const double light = glow + tight * tight * 0.10;
      canvas.blend(x, y, Rgba{static_cast<int>(std::lround(base_r)),
                              static_cast<int>(std::lround(base_g + 112 * light)),
                              static_cast<int>(std::lround(base_b + 209 * light)), 255});
    }
  }

  // Slight vignette keeps the edges from washing out under the shell's glow.
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      const double dx = (x + 0.5 - size / 2.0) / (size / 2.0);
      const double dy = (y + 0.5 - size / 2.0) / (size / 2.0);
      const double distance = std::sqrt(dx * dx + dy * dy);
      const double strength = std::max(0.0, distance - 0.55);
      if (strength <= 0) continue;
      const int alpha = static_cast<int>(std::lround(115.0 * strength * strength));
      if (alpha > 0) canvas.blend(x, y, Rgba{0, 0, 0, alpha});
    }
  }

  // A thin blue arc gives the monogram a system-icon read at small sizes.
  canvas.arc(center_x, center_y, 194 * kIconScale, 3 * kIconScale, 214, 326,
             Rgba{0, 112, 209, 150});

  const int scale = kIconScale;
  const int bar_width = 42 * scale;
  const int left_x = 163 * scale;
  const int right_x = 307 * scale;
  const int top = 126 * scale;
  const int bottom = 386 * scale;
  const int bridge_top = 235 * scale;
  const int bridge_bottom = 277 * scale;
  const int bridge_left = left_x + bar_width - 7 * scale;
  const int bridge_right = right_x + 7 * scale;
  const int radius = 18 * scale;

  const Gradient white{245, 245, 247, 196, 211, 232};
  const Gradient blue{63, 139, 255, 0, 112, 209};
  for (const auto& shadow : std::array<std::pair<int, int>, 3>{
           std::make_pair(10 * scale, 55), std::make_pair(18 * scale, 28),
           std::make_pair(28 * scale, 14)}) {
    const Rgba color{0, 0, 0, shadow.second};
    canvas.fill_round_rect(left_x, top + shadow.first, left_x + bar_width,
                           bottom + shadow.first, radius,
                           [color](int) { return color; });
    canvas.fill_round_rect(right_x, top + shadow.first, right_x + bar_width,
                           bottom + shadow.first, radius,
                           [color](int) { return color; });
    canvas.fill_round_rect(bridge_left, bridge_top + shadow.first, bridge_right,
                           bridge_bottom + shadow.first, radius,
                           [color](int) { return color; });
  }

  canvas.fill_round_rect(left_x, top, left_x + bar_width, bottom, radius,
                         [white](int y) { return white.at(y, size); });
  canvas.fill_round_rect(right_x, top, right_x + bar_width, bottom, radius,
                         [white](int y) { return white.at(y, size); });
  canvas.fill_round_rect(bridge_left, bridge_top, bridge_right, bridge_bottom,
                         radius, [blue](int y) { return blue.at(y, size); });
  canvas.fill_round_rect(left_x + 8 * scale, top + 12 * scale,
                         left_x + 13 * scale, bottom - 12 * scale, 2 * scale,
                         [](int) { return Rgba{255, 255, 255, 110}; });

  IconCanvas output(kIconSize, kIconSize);
  for (int y = 0; y < kIconSize; ++y) {
    for (int x = 0; x < kIconSize; ++x) {
      std::array<int, 4> totals{0, 0, 0, 0};
      for (int oy = 0; oy < kIconScale; ++oy) {
        for (int ox = 0; ox < kIconScale; ++ox) {
          const std::size_t index =
              ((static_cast<std::size_t>(y) * kIconScale + oy) * size +
               static_cast<std::size_t>(x) * kIconScale + ox) * 4u;
          for (int channel = 0; channel < 4; ++channel) {
            totals[static_cast<std::size_t>(channel)] +=
                canvas.pixels()[index + static_cast<std::size_t>(channel)];
          }
        }
      }
      const int count = kIconScale * kIconScale;
      output.set_pixel(x, y,
                       Rgba{totals[0] / count, totals[1] / count,
                            totals[2] / count, totals[3] / count});
    }
  }
  return output;
}

std::uint32_t crc32_value(const std::string& bytes) {
  static std::array<std::uint32_t, 256> table{};
  static bool ready = false;
  if (!ready) {
    for (std::uint32_t i = 0; i < 256; ++i) {
      std::uint32_t value = i;
      for (int bit = 0; bit < 8; ++bit) {
        value = (value & 1u) ? (0xedb88320u ^ (value >> 1u)) : (value >> 1u);
      }
      table[i] = value;
    }
    ready = true;
  }
  std::uint32_t value = 0xffffffffu;
  for (unsigned char byte : bytes) {
    value = table[(value ^ byte) & 0xffu] ^ (value >> 8u);
  }
  return value ^ 0xffffffffu;
}

void append_u32_be(std::string* out, std::uint32_t value) {
  out->push_back(static_cast<char>((value >> 24u) & 0xffu));
  out->push_back(static_cast<char>((value >> 16u) & 0xffu));
  out->push_back(static_cast<char>((value >> 8u) & 0xffu));
  out->push_back(static_cast<char>(value & 0xffu));
}

void png_chunk(std::string* out, const char type[4], const std::string& data) {
  append_u32_be(out, static_cast<std::uint32_t>(data.size()));
  std::string check(type, 4);
  check += data;
  out->append(type, 4);
  out->append(data);
  append_u32_be(out, crc32_value(check));
}

std::uint32_t adler32_value(const std::string& bytes) {
  std::uint32_t a = 1;
  std::uint32_t b = 0;
  for (unsigned char byte : bytes) {
    a = (a + byte) % 65521u;
    b = (b + a) % 65521u;
  }
  return (b << 16u) | a;
}

std::string encode_icon_png(const IconCanvas& canvas) {
  std::string raw;
  raw.reserve(static_cast<std::size_t>(canvas.height()) *
              (1u + static_cast<std::size_t>(canvas.width()) * 4u));
  for (int y = 0; y < canvas.height(); ++y) {
    raw.push_back('\0');
    const std::size_t row = static_cast<std::size_t>(y) *
                            static_cast<std::size_t>(canvas.width()) * 4u;
    raw.append(reinterpret_cast<const char*>(canvas.pixels().data() + row),
               static_cast<std::size_t>(canvas.width()) * 4u);
  }

  std::string compressed;
  compressed.push_back(static_cast<char>(0x78));
  compressed.push_back(static_cast<char>(0x01));
  std::size_t offset = 0;
  while (offset < raw.size()) {
    const std::size_t amount = std::min<std::size_t>(65535u, raw.size() - offset);
    const bool last = offset + amount == raw.size();
    compressed.push_back(static_cast<char>(last ? 1 : 0));
    const std::uint16_t length = static_cast<std::uint16_t>(amount);
    const std::uint16_t inverse = static_cast<std::uint16_t>(~length);
    compressed.push_back(static_cast<char>(length & 0xffu));
    compressed.push_back(static_cast<char>((length >> 8u) & 0xffu));
    compressed.push_back(static_cast<char>(inverse & 0xffu));
    compressed.push_back(static_cast<char>((inverse >> 8u) & 0xffu));
    compressed.append(raw, offset, amount);
    offset += amount;
  }
  append_u32_be(&compressed, adler32_value(raw));

  std::string output("\x89PNG\r\n\x1a\n", 8);
  std::string header;
  append_u32_be(&header, static_cast<std::uint32_t>(canvas.width()));
  append_u32_be(&header, static_cast<std::uint32_t>(canvas.height()));
  header.push_back(8);  // bit depth
  header.push_back(6);  // RGBA
  header.push_back(0);  // deflate
  header.push_back(0);  // adaptive filters
  header.push_back(0);  // no interlace
  png_chunk(&output, "IHDR", header);
  png_chunk(&output, "IDAT", compressed);
  png_chunk(&output, "IEND", std::string());
  return output;
}

}  // namespace

std::string home_tile_icon_png() {
  static const std::string png = encode_icon_png(paint_icon());
  return png;
}

HomeTile::HomeTile(HomeTileConfig config, HttpFn http, LogFn log)
    : config_(std::move(config)), http_(std::move(http)), log_(std::move(log)) {
  config_.homebrew_root = trim_slashes(config_.homebrew_root);
  if (config_.homebrew_root.empty()) config_.homebrew_root = "/data/homebrew";
  if (config_.marker_path.empty()) {
    config_.marker_path = join_path(config_.homebrew_root, ".ps5h-home-tile-removed");
  }
  if (config_.deeplink_port < 1 || config_.deeplink_port > 65535) {
    config_.deeplink_port = 8791;
  }
  deeplink_ = "http://127.0.0.1:" + std::to_string(config_.deeplink_port) + "/";
  std::lock_guard<std::mutex> lock(mutex_);
  refresh_local_locked();
  if (removed_) {
    reason_ = "removed";
    detail_ = "Removed from the home screen. Add it again to restore the tile.";
  } else if (!folder_present_) {
    reason_ = "not_installed";
    detail_ = "No tile folder is present. Use Add to home screen.";
  }
}

std::string HomeTile::path() const {
  return join_path(config_.homebrew_root, config_.title_id);
}

bool HomeTile::owns_path(const std::string& value) const {
  return path_is_within(value, path());
}

void HomeTile::refresh_local_locked() {
  folder_present_ = path_is_dir(path());
  shell_available_ = ::access(config_.web_shell_path.c_str(), R_OK) == 0;
  removed_ = ::access(config_.marker_path.c_str(), F_OK) == 0;
}

void HomeTile::set_pending_locked(const std::string& reason,
                                  const std::string& detail) {
  registered_ = false;
  reason_ = reason;
  detail_ = detail;
}

void HomeTile::set_installed_locked(const std::string& detail) {
  registered_ = true;
  removed_ = false;
  reason_ = "";
  detail_ = detail;
}

Json HomeTile::snapshot_locked() const {
  Json out = Json::object();
  out["title_id"] = config_.title_id;
  out["title_name"] = config_.title_name;
  out["path"] = path();
  out["state"] = registered_ ? "installed" : "pending";
  out["installed"] = registered_;
  out["registered"] = registered_;
  out["removed"] = removed_;
  out["folder_present"] = folder_present_;
  out["web_shell"] = config_.web_shell_path;
  out["shell_available"] = shell_available_;
  out["deeplink"] = deeplink_;
  out["service_reachable"] = service_reachable_;
  out["scan_path_ok"] = scan_path_ok_;
  out["reason"] = reason_;
  out["detail"] = detail_;
  out["checked_at"] = checked_at_;
  out["library_excluded"] = true;
  Json paths = Json::array();
  for (const std::string& scan_path : scan_paths_) paths.push_back(scan_path);
  out["scan_paths"] = paths;
  return out;
}

Json HomeTile::envelope_locked(bool ok, const std::string& error) const {
  Json out = Json::object();
  out["ok"] = ok;
  out["error"] = error;
  out["home_tile"] = snapshot_locked();
  return out;
}

bool HomeTile::post_locked(const std::string& route, const Json& body,
                           HttpResponse* response) {
  if (!http_) return false;
  HttpRequest request;
  request.method = "POST";
  request.url = config_.shadowmount_url + route;
  request.headers.emplace_back("Content-Type", "application/json");
  request.body = body.dump();
  request.timeout_seconds = 5;
  request.max_body_bytes = 256u * 1024u;
  *response = http_(request);
  return response->error.empty();
}

bool HomeTile::verify_locked() {
  refresh_local_locked();
  checked_at_ = now_seconds();
  service_reachable_ = false;
  scan_path_ok_ = false;
  scan_paths_.clear();
  if (removed_) {
    set_pending_locked("removed",
                       "Removed from the home screen. Add it again to restore the tile.");
    return false;
  }
  if (!folder_present_) {
    set_pending_locked("not_installed",
                       "No tile folder is present. Use Add to home screen.");
    return false;
  }
  HttpResponse info;
  Json info_request = Json::object();
  info_request["title_id"] = config_.title_id;
  if (!post_locked("/api/v1/games/info", info_request, &info)) {
    set_pending_locked(
        "registry_unreachable",
        "Nothing is listening on 127.0.0.1:10101. Start ShadowMountPlus, "
        "then press Check status.");
    return false;
  }
  service_reachable_ = true;
  if (info.status == 200) {
    try {
      const Json answer = Json::parse(info.body);
      if (answer["status"].as_number(-1) == 0) {
        set_installed_locked("Registered with ShadowMountPlus.");
        scan_path_ok_ = true;
        return true;
      }
      set_pending_locked("registry_rejected",
                         "ShadowMountPlus answered with status " +
                             std::to_string(static_cast<int>(
                                 answer["status"].as_number(-1))) + ".");
    } catch (const JsonError&) {
      set_pending_locked("registry_error",
                         "ShadowMountPlus returned an unreadable game record.");
    }
    return false;
  }
  if (info.status != 404) {
    set_pending_locked("registry_error",
                       "ShadowMountPlus returned HTTP " +
                           std::to_string(info.status) + " for the tile.");
    return false;
  }

  // A title record is missing. Read the settings route to distinguish a
  // service that has not scanned yet from one configured to ignore the folder.
  HttpResponse settings;
  if (post_locked("/api/v1/settings", Json::object(), &settings) &&
      settings.status == 200) {
    try {
      const Json answer = Json::parse(settings.body);
      const Json& paths = answer["scan_paths"];
      if (paths.is_array()) {
        for (const Json& entry : paths.items()) {
          if (entry.is_string()) scan_paths_.push_back(entry.as_string());
        }
      }
    } catch (const JsonError&) {
      // Older ShadowMount builds may not have the settings route. The info
      // route still proved the service is up, so report a missing registration
      // rather than inventing a scan-path failure.
    }
  }
  if (scan_paths_.empty()) {
    // The API returns only explicit custom roots. Empty means the compile-time
    // defaults are active, which include /data/homebrew.
    scan_path_ok_ = true;
  } else {
    for (const std::string& scan_path : scan_paths_) {
      if (path_is_within(config_.homebrew_root, scan_path)) {
        scan_path_ok_ = true;
        break;
      }
    }
  }
  if (!scan_path_ok_) {
    set_pending_locked(
        "scan_path_missing",
        config_.homebrew_root + " is not in ShadowMountPlus scan paths. "
        "Add it in ShadowMountPlus settings, then press Check status.");
    return false;
  }
  set_pending_locked(
      "not_registered",
      "The folder is written, but ShadowMountPlus has not registered it yet. "
      "Press Check status after its next scan.");
  return false;
}

bool HomeTile::check_locked() {
  refresh_local_locked();
  if (removed_) {
    set_pending_locked("removed",
                       "Removed from the home screen. Add it again to restore the tile.");
    return false;
  }
  if (!folder_present_) {
    set_pending_locked("not_installed",
                       "No tile folder is present. Use Add to home screen.");
    return false;
  }
  return verify_locked();
}

bool HomeTile::scan_locked(std::string* warning) {
  Json request = Json::object();
  request["reset_attempts"] = true;
  HttpResponse response;
  if (!post_locked("/api/v1/scan", request, &response)) {
    if (warning != nullptr) {
      *warning = "ShadowMountPlus is not reachable for a scan.";
    }
    return false;
  }
  if (response.status < 200 || response.status >= 300) {
    if (warning != nullptr) {
      *warning = "ShadowMountPlus scan returned HTTP " +
                 std::to_string(response.status) + ".";
    }
    return false;
  }
  return true;
}

bool HomeTile::install_locked(std::string* error) {
  refresh_local_locked();
  folder_present_ = path_is_dir(path());
  if (!valid_title_id(config_.title_id)) {
    set_pending_locked("invalid_title",
                       "Tile title ID must be four letters and five digits.");
    if (error != nullptr) *error = detail_;
    return false;
  }
  if (!path_is_dir(dirname_of(config_.homebrew_root))) {
    set_pending_locked("homebrew_missing",
                       "The parent of " + config_.homebrew_root +
                           " does not exist. Mount /data and retry.");
    if (error != nullptr) *error = detail_;
    return false;
  }
  if (!make_dir(config_.homebrew_root, error)) {
    set_pending_locked("homebrew_missing",
                       "Cannot use " + config_.homebrew_root +
                           ": " + (error != nullptr ? *error : "unknown error"));
    return false;
  }
  if (::access(config_.web_shell_path.c_str(), R_OK) != 0) {
    set_pending_locked(
        "web_shell_missing",
        "The console's web-app shell is not readable at " +
            config_.web_shell_path + ". Pass --web-shell with the correct path for this firmware.");
    if (error != nullptr) *error = detail_;
    return false;
  }

  const std::string stage = join_path(config_.homebrew_root,
                                      ".ps5h-" + config_.title_id + ".tmp");
  if (!remove_tree(stage, error)) {
    set_pending_locked("write_failed", *error);
    return false;
  }
  if (!make_dir(stage, error)) {
    set_pending_locked("write_failed", *error);
    return false;
  }
  const std::string sce_sys = join_path(stage, "sce_sys");
  if (!make_dir(sce_sys, error)) {
    remove_tree(stage, nullptr);
    set_pending_locked("write_failed", *error);
    return false;
  }

  Json metadata = Json::object();
  metadata["applicationCategoryType"] = 0;
  metadata["attribute"] = 1;
  metadata["attribute2"] = 0;
  metadata["attribute3"] = 4;
  metadata["contentId"] = "IV9999-" + config_.title_id + "_00-" + kContentSuffix;
  metadata["titleId"] = config_.title_id;
  metadata["deeplinkUri"] = deeplink_;
  Json localized = Json::object();
  localized["defaultLanguage"] = "en-US";
  Json english = Json::object();
  english["titleName"] = config_.title_name;
  localized["en-US"] = english;
  metadata["localizedParameters"] = localized;

  if (!write_bytes(join_path(sce_sys, "param.json"), metadata.dump(2) + "\n", error) ||
      !write_bytes(join_path(sce_sys, "icon0.png"), home_tile_icon_png(), error) ||
      !copy_file(config_.web_shell_path, join_path(stage, "eboot.bin"), error)) {
    remove_tree(stage, nullptr);
    set_pending_locked("write_failed", *error);
    return false;
  }
  if (::chmod(stage.c_str(), 0777) != 0 || ::chmod(sce_sys.c_str(), 0777) != 0 ||
      ::chmod(join_path(sce_sys, "param.json").c_str(), 0777) != 0 ||
      ::chmod(join_path(sce_sys, "icon0.png").c_str(), 0777) != 0 ||
      ::chmod(join_path(stage, "eboot.bin").c_str(), 0777) != 0) {
    if (error != nullptr) *error = "cannot set 0777 on the tile files";
    remove_tree(stage, nullptr);
    set_pending_locked("write_failed", *error);
    return false;
  }

  if (!remove_tree(path(), error)) {
    remove_tree(stage, nullptr);
    set_pending_locked("write_failed", *error);
    return false;
  }
  if (::rename(stage.c_str(), path().c_str()) != 0) {
    if (error != nullptr) *error = "cannot publish tile folder: " + std::string(std::strerror(errno));
    remove_tree(stage, nullptr);
    set_pending_locked("write_failed", *error);
    return false;
  }
  std::string marker_warning;
  if (::unlink(config_.marker_path.c_str()) != 0 && errno != ENOENT) {
    marker_warning = "cannot clear removal marker: " +
                     std::string(std::strerror(errno));
  }
  removed_ = false;
  folder_present_ = true;
  registered_ = false;
  std::string warning;
  scan_locked(&warning);

  bool installed = false;
  for (int attempt = 0; attempt < 3; ++attempt) {
    installed = verify_locked();
    if (installed) break;
    // Do not make an unreachable service or a missing scan path slower by
    // repeating the same five-second network timeout.
    if (!service_reachable_ || reason_ != "not_registered") break;
    if (attempt < 2) {
      std::this_thread::sleep_for(std::chrono::milliseconds(350));
    }
  }
  if (!warning.empty()) {
    if (!installed) {
      detail_ += " " + warning;
    } else if (log_) {
      log_("home tile: " + warning);
    }
  }
  if (!marker_warning.empty()) {
    if (!installed) {
      detail_ += " " + marker_warning;
    } else if (log_) {
      log_("home tile: " + marker_warning);
    }
  }
  return true;
}

void HomeTile::start() {
  std::lock_guard<std::mutex> lock(mutex_);
  refresh_local_locked();
  checked_at_ = now_seconds();
  if (!valid_title_id(config_.title_id)) {
    set_pending_locked("invalid_title",
                       "Tile title ID must be four letters and five digits.");
    return;
  }
  if (removed_) {
    set_pending_locked("removed",
                       "Removed from the home screen. Add it again to restore the tile.");
    return;
  }
  if (config_.auto_install) {
    std::string error;
    const bool ok = install_locked(&error);
    if (log_) {
      log_(std::string("home tile: ") + (ok ? detail_ : error));
    }
    return;
  }
  if (folder_present_) {
    verify_locked();
  } else {
    set_pending_locked("not_installed",
                       "No tile folder is present. Use Add to home screen.");
  }
}

Json HomeTile::state() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return snapshot_locked();
}

void HomeTile::refresh_if_due() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (registered_ || removed_ || !folder_present_ || !http_) return;
  // Once the service has refused a connection, leave the retry to the explicit
  // Check status action. A browser state poll must not stall for five seconds.
  if (checked_at_ > 0 && !service_reachable_) return;
  const long long now = now_seconds();
  if (checked_at_ > 0 && now - checked_at_ < 15) return;
  verify_locked();
}

Json HomeTile::check() {
  std::lock_guard<std::mutex> lock(mutex_);
  const bool ok = check_locked();
  return envelope_locked(true, ok ? "" : "");
}

Json HomeTile::install() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::string error;
  const bool ok = install_locked(&error);
  return envelope_locked(ok, error);
}

Json HomeTile::remove() {
  std::lock_guard<std::mutex> lock(mutex_);
  refresh_local_locked();
  if (!valid_title_id(config_.title_id)) {
    set_pending_locked("invalid_title",
                       "Tile title ID must be four letters and five digits.");
    return envelope_locked(false, detail_);
  }
  const bool existed = folder_present_;
  std::string remove_error;
  if (existed && !remove_tree(path(), &remove_error)) {
    set_pending_locked("remove_failed", remove_error);
    return envelope_locked(false, remove_error);
  }
  folder_present_ = false;
  registered_ = false;
  removed_ = true;
  reason_ = "removed";
  std::string warnings;
  std::string marker_error;
  if (!write_marker(config_.marker_path, config_.title_id, &marker_error)) {
    reason_ = "marker_failed";
    warnings += " The removal marker could not be written: " + marker_error +
                ". The tile may be restored on the next start.";
  }

  HttpResponse uninstall;
  Json title = Json::object();
  title["title_id"] = config_.title_id;
  const bool uninstall_sent =
      post_locked("/api/v1/games/uninstall", title, &uninstall);
  service_reachable_ = uninstall_sent;
  if (!uninstall_sent) {
    warnings += " ShadowMountPlus was not reachable for uninstall.";
  } else if (uninstall.status < 200 || uninstall.status >= 300) {
    warnings += " ShadowMountPlus uninstall returned HTTP " +
                std::to_string(uninstall.status) + ".";
  }
  std::string scan_warning;
  scan_locked(&scan_warning);
  if (!scan_warning.empty()) warnings += " " + scan_warning;
  checked_at_ = now_seconds();
  if (!existed) {
    detail_ = "No tile folder was present. The removal marker is set.";
  } else {
    detail_ = "Removed from the home screen. Add it again to restore the tile.";
  }
  detail_ += warnings;
  return envelope_locked(marker_error.empty(), marker_error);
}

}  // namespace ps5h