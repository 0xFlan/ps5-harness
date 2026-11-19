#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "ps5h/core.h"
#include "ps5h/json.h"

namespace ps5h {

struct HomeTileConfig {
  // Four uppercase letters and five digits. Homebrew range, never PPSA/CUSA/NPXS.
  std::string title_id = "HARN00001";
  std::string title_name = "PS5 Harness";
  std::string homebrew_root = "/data/homebrew";
  // The console's own web-app shell. This is the title executable for a
  // deeplink tile; the payload ELF must never be copied here.
  std::string web_shell_path = "/system_ex/app/NPXS40106/eboot.bin";
  // Empty derives <homebrew_root>/.ps5h-home-tile-removed.
  std::string marker_path;
  std::string shadowmount_url = "http://127.0.0.1:10101";
  int deeplink_port = 8791;
  // The console build enables this. Host builds keep it off unless explicitly
  // requested, so --serve never writes a system path on a developer machine.
  bool auto_install = false;
};

// Owns the home-screen tile folder and its registration state. The folder is a
// normal ShadowMountPlus source: eboot.bin is the console's web-app shell,
// param.json carries the deeplink, and icon0.png is generated in-process.
class HomeTile {
 public:
  HomeTile(HomeTileConfig config, HttpFn http, LogFn log);

  // Startup pass. Installs the folder when auto_install is set and no removal
  // marker exists; otherwise records the existing state without writing.
  void start();

  // Cached state for health/state polling. Never performs network I/O.
  Json state() const;

  // Rechecks a pending tile at most once every 15 seconds. Used by /api/state
  // so a tile that finishes registering after its install shows Installed
  // without hammering ShadowMountPlus on every browser poll.
  void refresh_if_due();

  // Writes the folder, queues a scan, and verifies via /api/v1/games/info.
  Json install();

  // Deletes the folder first, then asks ShadowMountPlus to uninstall, then
  // queues a scan. Writes the removal marker so startup does not restore it.
  Json remove();

  // Forces the registration check and returns a compact result envelope.
  Json check();

  bool owns_path(const std::string& path) const;
  std::string path() const;

 private:
  Json snapshot_locked() const;
  Json envelope_locked(bool ok, const std::string& error) const;
  bool check_locked();
  bool verify_locked();
  bool install_locked(std::string* error);
  bool scan_locked(std::string* warning);
  bool post_locked(const std::string& route, const Json& body,
                   HttpResponse* response);
  void set_pending_locked(const std::string& reason, const std::string& detail);
  void set_installed_locked(const std::string& detail);
  void refresh_local_locked();

  mutable std::mutex mutex_;
  HomeTileConfig config_;
  HttpFn http_;
  LogFn log_;
  bool registered_ = false;
  bool removed_ = false;
  bool folder_present_ = false;
  bool shell_available_ = false;
  bool service_reachable_ = false;
  bool scan_path_ok_ = false;
  std::string reason_ = "not_checked";
  std::string detail_ = "Checking the home screen tile.";
  std::string deeplink_;
  std::vector<std::string> scan_paths_;
  long long checked_at_ = 0;
};

// The complete PNG is generated in code and embedded in the binary. The app
// serves these same bytes at /tile-icon.png; there is no file to stage by hand.
std::string home_tile_icon_png();

}  // namespace ps5h