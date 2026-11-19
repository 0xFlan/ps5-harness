#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "ps5h/home_tile.h"
#include "ps5h/json.h"

using namespace ps5h;

namespace {

std::string read_file(const std::string& path) {
  FILE* file = std::fopen(path.c_str(), "rb");
  assert(file != nullptr);
  std::string out;
  char buffer[4096];
  std::size_t got = 0;
  while ((got = std::fread(buffer, 1, sizeof buffer, file)) > 0) {
    out.append(buffer, got);
  }
  std::fclose(file);
  return out;
}

bool mode_is_0777(const std::string& path) {
  struct stat info {};
  assert(::lstat(path.c_str(), &info) == 0);
  return (info.st_mode & 0777) == 0777;
}

struct FakeRegistry {
  bool reachable = true;
  bool registered = false;
  bool uninstall_after_delete = false;
  std::string tile_path;
  std::vector<std::string> scan_paths{"/data/homebrew"};
  std::vector<std::string> calls;

  HttpFn http() {
    return [this](const HttpRequest& request) -> HttpResponse {
      HttpResponse response;
      if (!reachable) {
        response.error = "Failed to connect to 127.0.0.1 port 10101";
        return response;
      }
      if (request.url.find("/api/v1/games/info") != std::string::npos) {
        calls.push_back("info");
        if (registered) {
          response.status = 200;
          response.body = "{\"status\":0,\"title_id\":\"HARN00001\"}";
        } else {
          response.status = 404;
          response.body = "{\"status\":2,\"error\":\"not found\"}";
        }
        return response;
      }
      if (request.url.find("/api/v1/settings") != std::string::npos) {
        calls.push_back("settings");
        Json body = Json::object();
        body["status"] = 0;
        Json paths = Json::array();
        for (const std::string& path : scan_paths) paths.push_back(path);
        body["scan_paths"] = paths;
        response.status = 200;
        response.body = body.dump();
        return response;
      }
      if (request.url.find("/api/v1/scan") != std::string::npos) {
        calls.push_back("scan");
        response.status = 200;
        response.body = "{\"status\":0}";
        return response;
      }
      if (request.url.find("/api/v1/games/uninstall") != std::string::npos) {
        calls.push_back("uninstall");
        if (!tile_path.empty() && ::access((tile_path + "/eboot.bin").c_str(), F_OK) != 0) {
          uninstall_after_delete = true;
        }
        registered = false;
        response.status = 200;
        response.body = "{\"status\":0}";
        return response;
      }
      response.status = 404;
      response.body = "{\"status\":2,\"error\":\"unknown route\"}";
      return response;
    };
  }
};

std::string make_root() {
  char template_path[] = "/tmp/ps5h-home-tile-XXXXXX";
  char* root = ::mkdtemp(template_path);
  assert(root != nullptr);
  return root;
}

HomeTileConfig config_for(const std::string& root, const std::string& shell,
                          bool auto_install = false) {
  HomeTileConfig config;
  config.homebrew_root = root + "/homebrew";
  config.web_shell_path = shell;
  config.auto_install = auto_install;
  config.shadowmount_url = "http://127.0.0.1:10101";
  return config;
}

void write_shell(const std::string& path) {
  const std::string bytes = "fake web-app shell bytes";
  FILE* file = std::fopen(path.c_str(), "wb");
  assert(file != nullptr);
  assert(std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size());
  std::fclose(file);
}

void test_icon() {
  const std::string png = home_tile_icon_png();
  assert(png.size() > 1000);
  assert(static_cast<unsigned char>(png[0]) == 0x89);
  assert(png.compare(1, 3, "PNG") == 0);
  const auto byte = [&png](std::size_t index) -> unsigned int {
    return static_cast<unsigned char>(png[index]);
  };
  const unsigned int width = (byte(16) << 24) | (byte(17) << 16) |
                             (byte(18) << 8) | byte(19);
  const unsigned int height = (byte(20) << 24) | (byte(21) << 16) |
                              (byte(22) << 8) | byte(23);
  assert(width == 512);
  assert(height == 512);
  assert(byte(24) == 8);   // bit depth
  assert(byte(25) == 6);   // RGBA
  assert(png.find("IEND") != std::string::npos);
}

void test_install_and_registration() {
  const std::string root = make_root();
  const std::string shell = root + "/shell";
  write_shell(shell);
  FakeRegistry fake;
  const HomeTileConfig config = config_for(root, shell);
  fake.scan_paths = {config.homebrew_root};
  fake.tile_path = config.homebrew_root + "/HARN00001";
  HomeTile tile(config, fake.http(), LogFn{});
  tile.start();
  HomeTileConfig moved = config;
  moved.deeplink_port = 9123;
  HomeTile moved_tile(moved, fake.http(), LogFn{});
  assert(moved_tile.state()["deeplink"].as_string() ==
         "http://127.0.0.1:9123/");
  assert(tile.state()["state"].as_string() == "pending");
  assert(!tile.state()["folder_present"].as_bool());

  const Json installed = tile.install();
  assert(installed["ok"].as_bool());
  assert(installed["home_tile"]["state"].as_string() == "pending");
  assert(installed["home_tile"]["reason"].as_string() == "not_registered");
  assert(tile.state()["folder_present"].as_bool());

  const std::string tile_path = fake.tile_path;
  assert(mode_is_0777(tile_path));
  assert(mode_is_0777(tile_path + "/sce_sys"));
  assert(mode_is_0777(tile_path + "/eboot.bin"));
  assert(mode_is_0777(tile_path + "/sce_sys/param.json"));
  assert(mode_is_0777(tile_path + "/sce_sys/icon0.png"));
  assert(read_file(tile_path + "/eboot.bin") == "fake web-app shell bytes");

  const Json param = Json::parse(read_file(tile_path + "/sce_sys/param.json"));
  assert(param["applicationCategoryType"].as_number() == 0);
  assert(param["attribute"].as_number() == 1);
  assert(param["attribute2"].as_number() == 0);
  assert(param["attribute3"].as_number() == 4);
  assert(param["titleId"].as_string() == "HARN00001");
  assert(param["deeplinkUri"].as_string() == "http://127.0.0.1:8791/");
  assert(param["localizedParameters"]["en-US"]["titleName"].as_string() ==
         "PS5 Harness");
  const std::string content_id = param["contentId"].as_string();
  const std::size_t suffix = content_id.find("_00-");
  assert(suffix != std::string::npos);
  assert(content_id.size() - suffix - 4 == 16);
  assert(content_id.rfind("IV9999-HARN00001_00-", 0) == 0);

  const std::string icon = read_file(tile_path + "/sce_sys/icon0.png");
  assert(icon == home_tile_icon_png());
  assert(tile.state()["library_excluded"].as_bool());
  assert(tile.owns_path(tile_path + "/sce_sys/icon0.png"));
  assert(!tile.owns_path(tile_path + "X"));

  // The folder is written before the scan request and the scan precedes info.
  const auto scan_it = std::find(fake.calls.begin(), fake.calls.end(), "scan");
  assert(scan_it != fake.calls.end());
  assert(scan_it + 1 < fake.calls.end());
  assert(*(scan_it + 1) == "info");

  fake.registered = true;
  const Json checked = tile.check();
  assert(checked["home_tile"]["state"].as_string() == "installed");
  assert(checked["home_tile"]["installed"].as_bool());
  assert(checked["home_tile"]["registered"].as_bool());
  assert(checked["home_tile"]["detail"].as_string().find("Registered") !=
         std::string::npos);

  fake.registered = true;
  const Json removed = tile.remove();
  assert(removed["ok"].as_bool());
  assert(removed["home_tile"]["state"].as_string() == "pending");
  assert(removed["home_tile"]["removed"].as_bool());
  assert(::access(tile_path.c_str(), F_OK) != 0);
  assert(::access((config.homebrew_root + "/.ps5h-home-tile-removed").c_str(), F_OK) == 0);
  assert(fake.uninstall_after_delete);
  const auto uninstall_it =
      std::find(fake.calls.rbegin(), fake.calls.rend(), "uninstall");
  assert(uninstall_it != fake.calls.rend());
  const auto last_scan_it = std::find(fake.calls.rbegin(), fake.calls.rend(), "scan");
  assert(last_scan_it != fake.calls.rend());
  assert(std::distance(fake.calls.rbegin(), uninstall_it) >
         std::distance(fake.calls.rbegin(), last_scan_it));

  // A new process must not silently restore a tile the operator removed.
  fake.registered = false;
  HomeTile next(config, fake.http(), LogFn{});
  next.start();
  assert(next.state()["state"].as_string() == "pending");
  assert(next.state()["removed"].as_bool());
  assert(::access(tile_path.c_str(), F_OK) != 0);

  // Explicit Add clears the marker and restores the tile.
  fake.registered = true;
  const Json restored = next.install();
  assert(restored["ok"].as_bool());
  assert(restored["home_tile"]["state"].as_string() == "installed");
  assert(::access((config.homebrew_root + "/.ps5h-home-tile-removed").c_str(), F_OK) != 0);

  std::filesystem::remove_all(root);
}

void test_pending_reasons() {
  const std::string root = make_root();
  const std::string shell = root + "/shell";
  write_shell(shell);

  FakeRegistry offline;
  offline.reachable = false;
  const HomeTileConfig offline_config = config_for(root, shell);
  HomeTile no_service(offline_config, offline.http(), LogFn{});
  const Json no_service_result = no_service.install();
  assert(no_service_result["ok"].as_bool());
  assert(no_service_result["home_tile"]["reason"].as_string() ==
         "registry_unreachable");
  assert(no_service_result["home_tile"]["detail"].as_string().find("10101") !=
         std::string::npos);

  FakeRegistry wrong_root;
  wrong_root.scan_paths = {"/mnt/usb0"};
  HomeTile wrong_scan(offline_config, wrong_root.http(), LogFn{});
  const Json wrong_result = wrong_scan.install();
  assert(wrong_result["home_tile"]["state"].as_string() == "pending");
  assert(wrong_result["home_tile"]["reason"].as_string() == "scan_path_missing");
  assert(wrong_result["home_tile"]["detail"].as_string().find(
             offline_config.homebrew_root) != std::string::npos);

  const std::string shellless_root = make_root();
  const HomeTileConfig no_shell_config =
      config_for(shellless_root, shellless_root + "/missing-shell");
  HomeTile no_shell(no_shell_config, wrong_root.http(), LogFn{});
  const Json no_shell_result = no_shell.install();
  assert(no_shell_result["home_tile"]["reason"].as_string() ==
         "web_shell_missing");
  assert(!no_shell_result["home_tile"]["folder_present"].as_bool());

  std::filesystem::remove_all(root);
  std::filesystem::remove_all(shellless_root);
}

void test_invalid_title_never_deletes_the_root() {
  const std::string root = make_root();
  const std::string shell = root + "/shell";
  write_shell(shell);
  HomeTileConfig config = config_for(root, shell);
  config.title_id.clear();
  std::filesystem::create_directories(config.homebrew_root);
  const std::string sentinel = config.homebrew_root + "/keep.txt";
  {
    FILE* file = std::fopen(sentinel.c_str(), "wb");
    assert(file != nullptr);
    assert(std::fputs("keep\n", file) >= 0);
    std::fclose(file);
  }
  FakeRegistry fake;
  HomeTile tile(config, fake.http(), LogFn{});
  const Json removed = tile.remove();
  assert(!removed["ok"].as_bool());
  assert(removed["home_tile"]["reason"].as_string() == "invalid_title");
  assert(::access(sentinel.c_str(), F_OK) == 0);
  assert(fake.calls.empty());
  std::filesystem::remove_all(root);
}

}  // namespace

int main() {
  test_icon();
  test_install_and_registration();
  test_pending_reasons();
  test_invalid_title_never_deletes_the_root();
  std::printf("home_tile_test: ok\n");
  return 0;
}