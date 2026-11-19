#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "ps5h/core.h"
#include "ps5h/json.h"

namespace ps5h {

// The payload has no native window: its screen is the workspace it serves. The
// console driver keeps a headless model of that screen so the agent can look at
// it and press the same buttons the browser's gamepad layer maps. A browser
// attached to the same process sees the result.
struct ConsoleScreen {
  std::string build;
  std::string model;
  std::string provider;
  std::string mode = "full";
  bool busy = false;
  std::vector<std::string> transcript;
  std::vector<std::string> activity;
  std::vector<std::pair<std::string, std::string>> tools;  // name, description
  Json home_tile = Json::object();
};

struct ConsoleElement {
  std::string id;
  std::string role = "button";  // tab | button | field | region | listitem
  std::string label;
  std::string value;
};

// Mutable surface state. Kept in one struct so a frame, an element list and a
// flow step all read the same snapshot under one lock.
struct ConsoleUiState {
  std::string panel = "activity";
  std::string focus = "rail:activity";
  std::string composer;
  std::string status = "idle";
  std::string base_url;
  std::string model_id;
  std::string key_env;
  std::string key_file;
  std::string temperature = "0.2";
  std::string max_tokens = "0";
  bool local_model = false;
  bool driving = false;
};

using ConsoleViewFn = std::function<ConsoleScreen()>;
// Activation that reaches past the surface itself: a provider switch, a turn, a
// home-screen action. The server fills this in. A bare command-line run leaves
// it empty and such an activation fails loudly instead of pretending.
using ConsoleActivateFn =
    std::function<Json(const std::string& id, const Json& value)>;

// Drives the harness's own UI without a browser. Perception is `frame`, an
// ASCII raster of the same controls the page renders; actuation is `press`,
// which applies DualSense semantics to that surface.
class ConsoleUi {
 public:
  void set_view(ConsoleViewFn view);
  void set_activate(ConsoleActivateFn activate);
  void set_build(const std::string& build);

  std::string panel() const;
  std::string focus() const;
  std::string status() const;
  std::string composer() const;
  bool driving() const;

  // Called by the browser when a person changes panel: the agent's screen
  // follows the person, and driving stops so the page is not yanked back.
  void set_panel(const std::string& panel, bool driving);
  void settle();

  std::string frame(int width, int height) const;
  // Controls on the open panel, and controls on every panel (each tagged with
  // the panel it lives on) so a search does not depend on what is open.
  Json elements() const;
  Json all_elements() const;
  Json state() const;

  // Buttons: cross, circle, square, triangle, up, down, left, right, l1, r1,
  // options. Keyboard aliases: enter, escape, tab, backtab.
  Json press(const std::string& button);
  Json focus_element(const std::string& id);
  Json type_text(const std::string& text, bool clear);
  Json activate_focused();

  // A bounded, verified step list. Stops at the first failed expectation and
  // reports the step index, so a setup flow is a check, not a wish.
  Json run_flow(const Json& flow);

 private:
  ConsoleScreen view_snapshot() const;
  std::vector<ConsoleElement> layout(const ConsoleScreen& view,
                                    const ConsoleUiState& state) const;
  void ensure_focus(const std::vector<ConsoleElement>& elements);
  Json ok_state(const std::string& detail) const;
  Json failure(const std::string& message) const;
  Json activate_locked(const std::string& id, const Json& payload);

  mutable std::mutex mutex_;
  ConsoleViewFn view_;
  ConsoleActivateFn activate_;
  ConsoleUiState st_;
  std::string build_;
  std::chrono::steady_clock::time_point driving_until_{};
};

// Registers screen_shot, screen_find, screen_press, screen_focus, screen_type
// and screen_flow. The handlers keep the surface alive for their own lifetime.
void register_console_tools(ToolRegistry& registry,
                            std::shared_ptr<ConsoleUi> ui);

}  // namespace ps5h
