#include "ps5h/console_ui.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <memory>

namespace ps5h {
namespace {

constexpr int kMinWidth = 48;
constexpr int kMaxWidth = 160;
constexpr int kDefaultWidth = 88;
constexpr int kMinHeight = 14;
constexpr int kMaxHeight = 60;
constexpr int kDefaultHeight = 26;
constexpr int kMainColumn = 14;
constexpr int kDrivingSeconds = 10;
constexpr std::size_t kMaxFlowSteps = 64;
constexpr std::size_t kMaxFlowBytes = 16u * 1024u;
constexpr std::size_t kMaxTypeChars = 1024;
constexpr std::size_t kMaxValueChars = 200;

// Everything the model reads is plain ASCII: frames travel through JSON, logs
// and terminals, and a console terminal is not guaranteed any more than that.
std::string ascii_of(const std::string& value, std::size_t limit) {
  std::string out;
  out.reserve(std::min(value.size(), limit));
  for (char c : value) {
    if (out.size() >= limit) break;
    const unsigned char u = static_cast<unsigned char>(c);
    if (c == '\n' || c == '\r') {
      out.push_back(' ');
    } else if (u < 32 || u == 127) {
      out.push_back('.');
    } else if (u > 126) {
      out.push_back('?');
    } else {
      out.push_back(c);
    }
  }
  return out;
}

std::string title_case(const std::string& value) {
  std::string out = value;
  if (!out.empty()) {
    out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
  }
  return out;
}

std::string normalise_button(const std::string& raw) {
  std::string b;
  for (char c : raw) {
    if (c == ' ' || c == '_') continue;
    b.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  if (b == "a" || b == "enter" || b == "return" || b == "activate") return "cross";
  if (b == "b" || b == "escape" || b == "esc" || b == "back") return "circle";
  if (b == "x") return "square";
  if (b == "y") return "triangle";
  if (b == "tab" || b == "dpad-down" || b == "dpaddown" || b == "next") return "down";
  if (b == "backtab" || b == "shift-tab" || b == "dpad-up" || b == "dpadup" ||
      b == "prev") {
    return "up";
  }
  if (b == "dpad-left" || b == "dpadleft") return "left";
  if (b == "dpad-right" || b == "dpadright") return "right";
  if (b == "l1" || b == "lb" || b == "leftbumper") return "l1";
  if (b == "r1" || b == "rb" || b == "rightbumper") return "r1";
  if (b == "options" || b == "start") return "options";
  if (b == "cross" || b == "circle" || b == "square" || b == "triangle" ||
      b == "up" || b == "down" || b == "left" || b == "right") {
    return b;
  }
  return std::string();
}

struct Line {
  std::string id;
  std::string text;
};

struct Canvas {
  Canvas(int width, int height) : width_(width), rows_(height, std::string(width, ' ')) {}

  void put(int row, int col, const std::string& text) {
    if (row < 0 || row >= static_cast<int>(rows_.size())) return;
    for (char c : text) {
      if (col >= width_) break;
      if (col >= 0) {
        const unsigned char u = static_cast<unsigned char>(c);
        rows_[row][col] = (u < 32 || u > 126) ? ' ' : c;
      }
      ++col;
    }
  }
  void rule(int row) { put(row, 0, std::string(width_, '-')); }
  std::string text() const {
    std::string out;
    for (std::size_t i = 0; i < rows_.size(); ++i) {
      if (i != 0) out.push_back('\n');
      out += rows_[i];
    }
    return out;
  }

 private:
  int width_;
  std::vector<std::string> rows_;
};

int clamp_int(double value, int low, int high) {
  const int rounded = static_cast<int>(value + 0.5);
  return std::max(low, std::min(high, rounded));
}

bool number_from(const std::string& text, double* out) {
  if (text.empty()) return false;
  char* end = nullptr;
  const double value = std::strtod(text.c_str(), &end);
  if (end == text.c_str() || *end != '\0') return false;
  *out = value;
  return true;
}

}  // namespace

void ConsoleUi::set_view(ConsoleViewFn view) {
  std::lock_guard<std::mutex> lock(mutex_);
  view_ = std::move(view);
}

void ConsoleUi::set_activate(ConsoleActivateFn activate) {
  std::lock_guard<std::mutex> lock(mutex_);
  activate_ = std::move(activate);
}

void ConsoleUi::set_build(const std::string& build) {
  std::lock_guard<std::mutex> lock(mutex_);
  build_ = ascii_of(build, 64);
}

std::string ConsoleUi::panel() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return st_.panel;
}

std::string ConsoleUi::focus() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return st_.focus;
}

std::string ConsoleUi::status() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return st_.status;
}

std::string ConsoleUi::composer() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return st_.composer;
}

bool ConsoleUi::driving() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return std::chrono::steady_clock::now() < driving_until_;
}

void ConsoleUi::set_panel(const std::string& panel, bool driving) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::string name = ascii_of(panel, 32);
  for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (name != "chat" && name != "activity" && name != "tools" && name != "model" &&
      name != "home") {
    return;
  }
  if (name == st_.panel) return;  // an echo must not cancel a running flow
  st_.panel = name;
  st_.focus = "rail:" + name;
  st_.driving = driving;
  if (driving) {
    driving_until_ = std::chrono::steady_clock::now() +
                     std::chrono::seconds(kDrivingSeconds);
  } else {
    driving_until_ = {};
  }
}

void ConsoleUi::settle() {
  std::lock_guard<std::mutex> lock(mutex_);
  st_.driving = false;
  driving_until_ = {};
}

ConsoleScreen ConsoleUi::view_snapshot() const {
  ConsoleViewFn view;
  std::string build;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    view = view_;
    build = build_;
  }
  ConsoleScreen screen = view ? view() : ConsoleScreen{};
  if (screen.build.empty()) screen.build = build;
  return screen;
}

std::vector<ConsoleElement> ConsoleUi::layout(const ConsoleScreen& view,
                                              const ConsoleUiState& state) const {
  std::vector<ConsoleElement> out;
  auto add = [&out](std::string id, std::string role, std::string label,
                    std::string value = std::string()) {
    ConsoleElement element;
    element.id = std::move(id);
    element.role = std::move(role);
    element.label = ascii_of(label, kMaxValueChars);
    element.value = ascii_of(value, kMaxValueChars);
    out.push_back(std::move(element));
  };

  for (const char* name : {"chat", "activity", "tools", "model", "home"}) {
    add(std::string("rail:") + name, "tab", title_case(name),
        state.panel == name ? "selected" : "");
  }
  add("header:model", "button", "Model",
      state.model_id.empty() ? view.model : state.model_id);

  if (state.panel == "chat") {
    add("chat:transcript", "region", "Transcript",
        std::to_string(view.transcript.size()) + " lines");
    add("composer:field", "field", "Message", state.composer);
    add("composer:send", "button", "Send");
    add("composer:reset", "button", "Clear message");
    add("chat:reset", "button", "New session");
  } else if (state.panel == "activity") {
    add("activity:log", "region", "Activity",
        std::to_string(view.activity.size()) + " events");
  } else if (state.panel == "tools") {
    add("tools:list", "region", "Tool catalogue",
        std::to_string(view.tools.size()) + " tools");
    std::size_t shown = 0;
    for (const auto& tool : view.tools) {
      if (shown++ >= 16) break;
      add("tool:" + tool.first, "listitem", tool.first, tool.second);
    }
  } else if (state.panel == "model") {
    add("model:local", "button", "On-console model",
        state.local_model ? "on" : "off");
    add("model:base_url", "field", "Base URL", state.base_url);
    add("model:id", "field", "Model ID", state.model_id);
    add("model:key-env", "field", "Key environment", state.key_env);
    add("model:key-file", "field", "Key file on console", state.key_file);
    add("model:temperature", "field", "Temperature", state.temperature);
    add("model:max_tokens", "field", "Max tokens", state.max_tokens);
    add("model:apply", "button", "Apply to next turn",
        view.model + " @ " + view.provider);
  } else {
    add("home:check", "button", "Check tile", view.home_tile["reason"].as_string());
    add("home:add", "button", "Add to home screen");
    add("home:remove", "button", "Remove tile");
  }
  return out;
}

void ConsoleUi::ensure_focus(const std::vector<ConsoleElement>& elements) {
  for (const ConsoleElement& element : elements) {
    if (element.id == st_.focus) return;
  }
  st_.focus = "rail:" + st_.panel;
  if (elements.empty()) st_.focus.clear();
}

Json ConsoleUi::state() const {
  std::lock_guard<std::mutex> lock(mutex_);
  Json out = Json::object();
  out["panel"] = st_.panel;
  out["focus"] = st_.focus;
  out["composer"] = st_.composer;
  out["status"] = st_.status;
  out["driving"] = std::chrono::steady_clock::now() < driving_until_;
  Json fields = Json::object();
  fields["base_url"] = st_.base_url;
  fields["model"] = st_.model_id;
  fields["key_env"] = st_.key_env;
  fields["key_file"] = st_.key_file;
  fields["temperature"] = st_.temperature;
  fields["max_tokens"] = st_.max_tokens;
  fields["local"] = st_.local_model;
  out["fields"] = fields;
  Json buttons = Json::array();
  for (const char* name : {"cross", "circle", "square", "triangle", "up", "down",
                           "left", "right", "l1", "r1", "options"}) {
    buttons.push_back(name);
  }
  out["buttons"] = buttons;
  return out;
}

Json ConsoleUi::elements() const {
  const ConsoleScreen view = view_snapshot();
  std::lock_guard<std::mutex> lock(mutex_);
  const std::vector<ConsoleElement> elements = layout(view, st_);
  Json out = Json::array();
  for (const ConsoleElement& element : elements) {
    Json item = Json::object();
    item["id"] = element.id;
    item["role"] = element.role;
    item["label"] = element.label;
    item["value"] = element.value;
    item["focused"] = element.id == st_.focus;
    out.push_back(item);
  }
  return out;
}

// Every control on every panel, so a search does not depend on which panel is
// open. Rail tabs are listed once, against the open panel.
Json ConsoleUi::all_elements() const {
  const ConsoleScreen view = view_snapshot();
  std::lock_guard<std::mutex> lock(mutex_);
  Json out = Json::array();
  for (const std::string& panel : {"chat", "activity", "tools", "model", "home"}) {
    ConsoleUiState probe = st_;
    probe.panel = panel;
    for (const ConsoleElement& element : layout(view, probe)) {
      const bool chrome = element.role == "tab" || element.id == "header:model";
      if (chrome && panel != st_.panel) continue;
      Json item = Json::object();
      item["id"] = element.id;
      item["role"] = element.role;
      item["label"] = element.label;
      item["value"] = element.value;
      item["focused"] = element.id == st_.focus;
      item["panel"] = panel;
      out.push_back(item);
    }
  }
  return out;
}

std::string ConsoleUi::frame(int width, int height) const {
  const ConsoleScreen view = view_snapshot();
  std::lock_guard<std::mutex> lock(mutex_);
  const int w = std::max(kMinWidth, std::min(kMaxWidth, width));
  const int h = std::max(kMinHeight, std::min(kMaxHeight, height));
  const int content_rows = h - 7;
  const std::vector<ConsoleElement> elements = layout(view, st_);

  Canvas canvas(w, h);
  canvas.put(0, 0, "PS5 HARNESS " + ascii_of(view.build, 32) + "  mode " + view.mode);
  if (std::chrono::steady_clock::now() < driving_until_) {
    const std::string tag = "[agent driving]";
    canvas.put(0, w - static_cast<int>(tag.size()) - 1, tag);
  }
  canvas.put(1, 0, "model " + ascii_of(view.model, 48) + " @ " +
                       ascii_of(view.provider, 40) +
                       (view.busy ? "  (turn running)" : ""));
  canvas.rule(2);

  // The rail is the page's left column; the main column is whatever panel is
  // open. Both are drawn from the same element list the tools report.
  std::size_t rail_index = 0;
  std::vector<Line> main_lines;
  for (const ConsoleElement& element : elements) {
    if (element.role == "tab") {
      const std::string marker = (element.id == st_.focus) ? ">" : " ";
      const std::string active = element.value == "selected" ? "*" : " ";
      if (rail_index < static_cast<std::size_t>(content_rows)) {
        canvas.put(3 + static_cast<int>(rail_index), 0,
                   marker + active + " " + ascii_of(element.label, 9));
      }
      ++rail_index;
      continue;
    }
    if (element.id == "header:model") continue;
    const std::string prefix = (element.id == st_.focus) ? ">" : " ";
    std::string text;
    if (element.role == "field") {
      text = prefix + element.label + ": " + element.value;
    } else if (element.role == "button") {
      text = prefix + "[ " + element.label + " ]";
      if (!element.value.empty()) text += "  " + element.value;
    } else if (element.role == "region") {
      text = prefix + element.label + " (" + element.value + ")";
    } else {
      text = prefix + element.label + "  " + element.value;
    }
    main_lines.push_back(Line{element.id, text});
  }

  // Panel-specific body lines that are data rather than controls.
  std::vector<Line> body;
  if (st_.panel == "chat") {
    for (const std::string& line : view.transcript) body.push_back(Line{"", line});
    if (body.empty()) body.push_back(Line{"", "no conversation yet"});
    if (body.size() > 8) body.erase(body.begin(), body.end() - 8);
  } else if (st_.panel == "activity") {
    for (const std::string& line : view.activity) body.push_back(Line{"", line});
    if (body.empty()) body.push_back(Line{"", "no activity yet"});
    if (body.size() > 8) body.erase(body.begin(), body.end() - 8);
  } else if (st_.panel == "home") {
    const Json& tile = view.home_tile;
    body.push_back(Line{"", "reason: " + tile["reason"].as_string("unknown")});
    body.push_back(Line{"", "detail: " + tile["detail"].as_string("")});
    const std::string deeplink = tile["deeplink"].as_string();
    if (!deeplink.empty()) body.push_back(Line{"", "deeplink: " + deeplink});
  }
  // Regions come first, then their data, then the controls below them.
  auto is_region = [](const std::string& id) {
    return id == "chat:transcript" || id == "activity:log" ||
           id == "tools:list" || id.rfind("tool:", 0) == 0;
  };
  std::vector<Line> ordered;
  bool body_placed = false;
  for (const Line& line : main_lines) {
    if (!is_region(line.id)) continue;
    ordered.push_back(line);
    if (!body_placed) {
      for (const Line& extra : body) ordered.push_back(extra);
      body_placed = true;
    }
  }
  for (const Line& line : main_lines) {
    if (is_region(line.id)) continue;
    ordered.push_back(line);
  }
  if (!body_placed) {
    for (const Line& extra : body) ordered.push_back(extra);
  }

  for (int row = 0; row < content_rows; ++row) {
    if (static_cast<std::size_t>(row) >= ordered.size()) break;
    canvas.put(3 + row, kMainColumn, ordered[static_cast<std::size_t>(row)].text);
  }

  canvas.rule(h - 5);
  canvas.put(h - 4, 0, "message: " + ascii_of(st_.composer, w - 12));
  const std::string keys =
      "focus " + ascii_of(st_.focus, 28) +
      "  |  cross activate  circle back  up/down move  l1/r1 panel  triangle composer";
  canvas.put(h - 3, 0, keys);
  canvas.put(h - 2, 0, "status: " + ascii_of(st_.status, w - 12));
  canvas.put(h - 1, 0, "");
  return canvas.text();
}

Json ConsoleUi::ok_state(const std::string& detail) const {
  Json out = Json::object();
  out["ok"] = true;
  out["panel"] = st_.panel;
  out["focus"] = st_.focus;
  out["status"] = detail.empty() ? st_.status : detail;
  return out;
}

Json ConsoleUi::failure(const std::string& message) const {
  Json out = Json::object();
  out["ok"] = false;
  out["panel"] = st_.panel;
  out["focus"] = st_.focus;
  out["error"] = message;
  return out;
}

Json ConsoleUi::press(const std::string& button) {
  const ConsoleScreen view = view_snapshot();
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string b = normalise_button(button);
  if (b.empty()) {
    return failure("unknown button '" + ascii_of(button, 32) +
                   "'; use cross, circle, square, triangle, up, down, left, "
                   "right, l1, r1 or options");
  }
  const std::vector<ConsoleElement> elements = layout(view, st_);
  ensure_focus(elements);
  st_.driving = true;
  driving_until_ = std::chrono::steady_clock::now() +
                   std::chrono::seconds(kDrivingSeconds);

  if (b == "cross") {
    Json result = activate_locked(st_.focus, Json::object());
    result["button"] = "cross";
    return result;
  }
  if (b == "circle") {
    if (st_.panel == "chat") {
      st_.status = "already at the chat screen";
      return ok_state(st_.status);
    }
    st_.panel = "chat";
    st_.focus = "rail:chat";
    st_.status = "back to chat";
    return ok_state(st_.status);
  }
  if (b == "triangle") {
    st_.panel = "chat";
    st_.focus = "composer:field";
    st_.status = "composer focused";
    return ok_state(st_.status);
  }
  if (b == "square") {
    st_.status =
        "the on-screen keyboard is a browser surface; use screen_type on a field";
    return ok_state(st_.status);
  }
  if (b == "options") {
    st_.status =
        "cross activates, circle goes back, up/down walk controls, l1/r1 switch panel";
    return ok_state(st_.status);
  }
  if (b == "l1" || b == "r1" || b == "left" || b == "right") {
    static const char* order[] = {"chat", "activity", "tools", "model", "home"};
    int index = 0;
    for (int i = 0; i < 5; ++i) {
      if (st_.panel == order[i]) index = i;
    }
    const bool forward = (b == "r1" || b == "right");
    index = (index + (forward ? 1 : -1) + 5) % 5;
    st_.panel = order[index];
    st_.focus = "rail:" + st_.panel;
    st_.status = "panel " + st_.panel;
    return ok_state(st_.status);
  }
  // up / down walk the open panel: its rail tab first, then its own controls.
  // The other rail tabs belong to l1/r1, so a D-pad press never skips panels.
  std::vector<std::string> ring;
  for (const ConsoleElement& element : elements) {
    if (element.id == "rail:" + st_.panel) {
      ring.push_back(element.id);
    } else if (element.role != "tab" && element.id != "header:model") {
      ring.push_back(element.id);
    }
  }
  if (ring.empty()) return failure("the screen has no controls");
  std::size_t index = 0;
  for (std::size_t i = 0; i < ring.size(); ++i) {
    if (ring[i] == st_.focus) index = i;
  }
  const std::size_t count = ring.size();
  index = (index + ((b == "down") ? 1 : count - 1)) % count;
  st_.focus = ring[index];
  st_.status = "focus " + st_.focus;
  return ok_state(st_.status);
}

Json ConsoleUi::activate_focused() {
  const ConsoleScreen view = view_snapshot();
  std::lock_guard<std::mutex> lock(mutex_);
  const std::vector<ConsoleElement> elements = layout(view, st_);
  ensure_focus(elements);
  st_.driving = true;
  driving_until_ = std::chrono::steady_clock::now() +
                   std::chrono::seconds(kDrivingSeconds);
  return activate_locked(st_.focus, Json::object());
}

Json ConsoleUi::activate_locked(const std::string& id, const Json& payload) {
  if (id.rfind("rail:", 0) == 0) {
    st_.panel = id.substr(5);
    st_.focus = id;
    st_.status = "panel " + st_.panel;
    return ok_state(st_.status);
  }
  if (id == "header:model") {
    st_.panel = "model";
    st_.focus = "rail:model";
    st_.status = "panel model";
    return ok_state(st_.status);
  }
  if (id.rfind("tool:", 0) == 0) {
    st_.status = "tool " + id.substr(5) + " is listed; details are in the tools panel";
    return ok_state(st_.status);
  }
  if (id == "activity:log" || id == "tools:list" || id == "chat:transcript") {
    st_.status = id + " is a read-only region; use screen_shot to read it";
    return ok_state(st_.status);
  }
  if (id == "composer:field" || id == "model:base_url" || id == "model:id" ||
      id == "model:key-env" || id == "model:key-file" ||
      id == "model:temperature" || id == "model:max_tokens") {
    return failure("'" + id + "' is a field; use screen_type to fill it");
  }
  if (id == "composer:reset") {
    st_.composer.clear();
    st_.status = "message cleared";
    return ok_state(st_.status);
  }
  if (id == "composer:send") {
    if (st_.composer.empty()) return failure("the message field is empty");
    Json value = Json::object();
    value["message"] = st_.composer;
    if (!activate_) {
      return failure(
          "no listener is attached to this surface; start the harness with "
          "--serve to send a turn");
    }
    Json result = activate_("chat:send", value);
    if (result["ok"].as_bool(false)) {
      st_.status = "message sent";
      st_.composer.clear();
    }
    return result;
  }
  if (id == "chat:reset") {
    if (!activate_) {
      return failure("no listener is attached to this surface");
    }
    return activate_("session:reset", Json::object());
  }
  if (id == "model:local") {
    st_.local_model = !st_.local_model;
    st_.status = st_.local_model ? "on-console model selected"
                                 : "remote provider selected";
    return ok_state(st_.status);
  }
  if (id == "model:apply") {
    if (st_.model_id.empty()) {
      return failure("model:id is empty; focus it with screen_focus and fill it "
                     "with screen_type first");
    }
    double temperature = 0.0;
    double max_tokens = 0.0;
    if (!number_from(st_.temperature, &temperature)) {
      return failure("model:temperature is not a number: '" + st_.temperature + "'");
    }
    if (!number_from(st_.max_tokens, &max_tokens)) {
      return failure("model:max_tokens is not a number: '" + st_.max_tokens + "'");
    }
    Json value = Json::object();
    value["model"] = st_.model_id;
    value["temperature"] = temperature;
    value["max_tokens"] = static_cast<long long>(max_tokens);
    value["local"] = st_.local_model;
    if (!st_.base_url.empty()) value["base_url"] = st_.base_url;
    if (!st_.key_env.empty()) value["api_key_env"] = st_.key_env;
    if (!st_.key_file.empty()) value["api_key_file"] = st_.key_file;
    if (!activate_) {
      return failure("no listener is attached to this surface; the model panel "
                     "needs a running harness");
    }
    Json result = activate_("model:apply", value);
    st_.status = result["ok"].as_bool(false)
                     ? "model applied: " + st_.model_id
                     : "model apply failed: " + result["error"].as_string("unknown");
    result["status"] = st_.status;
    return result;
  }
  if (id == "home:add" || id == "home:check" || id == "home:remove") {
    if (!activate_) {
      return failure("no listener is attached to this surface");
    }
    Json result = activate_(id, payload);
    st_.status = result["ok"].as_bool(false)
                     ? id + " done"
                     : id + " failed: " + result["error"].as_string("unknown");
    result["status"] = st_.status;
    return result;
  }
  return failure("'" + id + "' is not an activatable control");
}

Json ConsoleUi::focus_element(const std::string& id) {
  const ConsoleScreen view = view_snapshot();
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string wanted = ascii_of(id, 96);
  bool found = false;
  std::string panel_of = st_.panel;
  for (const std::string& panel : {"chat", "activity", "tools", "model", "home"}) {
    ConsoleUiState probe = st_;
    probe.panel = panel;
    for (const ConsoleElement& element : layout(view, probe)) {
      if (element.id == wanted) {
        found = true;
        panel_of = panel;
      }
    }
    if (found) break;
  }
  if (!found) {
    return failure("no control with id '" + wanted + "'; use screen_find or "
                   "screen_shot to see the available controls");
  }
  // Focusing a control on another panel switches there first, the same way a
  // person would tab over before pressing.
  st_.panel = panel_of;
  st_.focus = wanted;
  st_.driving = true;
  driving_until_ = std::chrono::steady_clock::now() +
                   std::chrono::seconds(kDrivingSeconds);
  st_.status = "focus " + wanted;
  return ok_state(st_.status);
}

Json ConsoleUi::type_text(const std::string& text, bool clear) {
  const ConsoleScreen view = view_snapshot();
  std::lock_guard<std::mutex> lock(mutex_);
  const std::vector<ConsoleElement> elements = layout(view, st_);
  ensure_focus(elements);
  const ConsoleElement* target = nullptr;
  for (const ConsoleElement& element : elements) {
    if (element.id == st_.focus) target = &element;
  }
  if (target == nullptr) return failure("nothing is focused");
  if (target->role != "field") {
    return failure("'" + target->id + "' is a " + target->role +
                   ", not a field; use screen_focus on a field first");
  }
  const std::string typed = ascii_of(text, kMaxTypeChars);
  const std::string value = clear ? typed : target->value + typed;
  const std::string& id = target->id;
  if (id == "composer:field") st_.composer = value;
  else if (id == "model:base_url") st_.base_url = value;
  else if (id == "model:id") st_.model_id = value;
  else if (id == "model:key-env") st_.key_env = value;
  else if (id == "model:key-file") st_.key_file = value;
  else if (id == "model:temperature") st_.temperature = value;
  else if (id == "model:max_tokens") st_.max_tokens = value;
  else return failure("'" + id + "' does not accept typed text");
  st_.driving = true;
  driving_until_ = std::chrono::steady_clock::now() +
                   std::chrono::seconds(kDrivingSeconds);
  st_.status = "typed into " + id;
  Json out = ok_state(st_.status);
  out["field"] = id;
  out["value"] = value;
  return out;
}

Json ConsoleUi::run_flow(const Json& flow) {
  auto fail = [](const std::string& message, int step) {
    Json out = Json::object();
    out["ok"] = false;
    out["failed_step"] = step;
    out["error"] = message;
    return out;
  };
  const Json* steps = nullptr;
  if (flow.is_array()) {
    steps = &flow;
  } else if (flow.is_object() && flow.contains("steps")) {
    if (!flow["steps"].is_array()) return fail("flow.steps must be an array", -1);
    steps = &flow["steps"];
  } else {
    return fail("flow must be an array of steps or an object with a steps array",
                -1);
  }
  if (steps->size() > kMaxFlowSteps) {
    return fail("flow has more than 64 steps", -1);
  }
  if (steps->dump().size() > kMaxFlowBytes) {
    return fail("flow is larger than 16384 bytes", -1);
  }

  Json log = Json::array();
  for (std::size_t i = 0; i < steps->size(); ++i) {
    const Json& step = (*steps)[i];
    const int index = static_cast<int>(i);
    if (!step.is_object()) return fail("step " + std::to_string(index) +
                                           " must be an object", index);
    bool ran = false;
    Json outcome = Json::object();
    std::string label;
    auto compare = [&](bool passed, const std::string& what,
                       const std::string& want, const std::string& got) {
      Json out = Json::object();
      out["ok"] = passed;
      if (passed) {
        out["status"] = what + " is " + got;
      } else {
        out["error"] = what + " expected '" + want + "' but was '" + got + "'";
      }
      return out;
    };

    if (step.contains("press")) {
      ran = true;
      label = "press " + step["press"].as_string();
      outcome = press(step["press"].as_string());
    } else if (step.contains("focus")) {
      ran = true;
      label = "focus " + step["focus"].as_string();
      outcome = focus_element(step["focus"].as_string());
    } else if (step.contains("type")) {
      ran = true;
      label = "type";
      outcome = type_text(step["type"].as_string(),
                          step.contains("clear") ? step["clear"].as_bool(true) : true);
    } else if (step.contains("expect_panel")) {
      ran = true;
      const std::string want = step["expect_panel"].as_string();
      label = "expect_panel " + want;
      outcome = compare(panel() == want, "panel", want, panel());
    } else if (step.contains("expect_focus")) {
      ran = true;
      const std::string want = step["expect_focus"].as_string();
      label = "expect_focus " + want;
      outcome = compare(focus() == want, "focus", want, focus());
    } else if (step.contains("expect_status")) {
      ran = true;
      const std::string want = step["expect_status"].as_string();
      label = "expect_status " + want;
      const std::string got = status();
      outcome = compare(got.find(want) != std::string::npos, "status", want, got);
    } else if (step.contains("expect_text")) {
      ran = true;
      const std::string want = step["expect_text"].as_string();
      label = "expect_text " + want;
      const std::string screen = frame(kDefaultWidth, kDefaultHeight);
      Json out = Json::object();
      if (screen.find(want) != std::string::npos) {
        out["ok"] = true;
        out["status"] = "text '" + want + "' is on screen";
      } else {
        out["ok"] = false;
        out["error"] = "text '" + want + "' is not on the screen";
      }
      outcome = out;
    } else if (step.contains("note")) {
      ran = true;
      label = "note";
      outcome["ok"] = true;
      outcome["status"] = ascii_of(step["note"].as_string(), 200);
    } else if (step.contains("shot")) {
      ran = true;
      label = "shot";
      outcome["ok"] = true;
      outcome["status"] = "frame captured";
      Json entry = Json::object();
      entry["step"] = index;
      entry["frame"] = frame(kDefaultWidth, kDefaultHeight);
      log.push_back(entry);
    }
    if (!ran) {
      return fail("step " + std::to_string(index) +
                      " has no action (press, focus, type, expect_panel, "
                      "expect_focus, expect_status, expect_text, note, shot)",
                  index);
    }
    Json entry = Json::object();
    entry["step"] = index;
    entry["action"] = label;
    entry["ok"] = outcome["ok"].as_bool(false);
    entry["detail"] = outcome["ok"].as_bool(false)
                          ? outcome["status"].as_string()
                          : outcome["error"].as_string();
    log.push_back(entry);
    if (!outcome["ok"].as_bool(false)) {
      Json out = fail(outcome["error"].as_string("step failed"), index);
      out["log"] = log;
      out["frame"] = frame(kDefaultWidth, kDefaultHeight);
      return out;
    }
  }
  Json out = Json::object();
  out["ok"] = true;
  out["steps_run"] = static_cast<long long>(steps->size());
  out["log"] = log;
  out["frame"] = frame(kDefaultWidth, kDefaultHeight);
  return out;
}

void register_console_tools(ToolRegistry& registry, std::shared_ptr<ConsoleUi> ui) {
  if (!ui) return;

  {
    Tool tool;
    tool.name = "screen_shot";
    tool.description =
        "Look at the console screen you are driving. Returns an ASCII frame of the "
        "harness workspace plus the focused control id. This is the eyes of the "
        "loop: call it before and after pressing buttons, and after a flow.";
    Json properties = Json::object();
    properties["width"] = Json::object();
    properties["width"]["type"] = "integer";
    properties["width"]["description"] = "Frame width in characters (48..160, default 88).";
    properties["height"] = Json::object();
    properties["height"]["type"] = "integer";
    properties["height"]["description"] = "Frame height in rows (14..60, default 26).";
    tool.parameters = Json::object();
    tool.parameters["type"] = "object";
    tool.parameters["properties"] = properties;
    tool.handler = [ui](const Json& arguments) -> Json {
      const int width =
          clamp_int(arguments["width"].as_number(kDefaultWidth), kMinWidth, kMaxWidth);
      const int height =
          clamp_int(arguments["height"].as_number(kDefaultHeight), kMinHeight, kMaxHeight);
      Json out = Json::object();
      out["ok"] = true;
      out["panel"] = ui->panel();
      out["focus"] = ui->focus();
      out["frame"] = ui->frame(width, height);
      return out;
    };
    registry.add(std::move(tool));
  }

  {
    Tool tool;
    tool.name = "screen_find";
    tool.description =
        "Find controls on the console screen by id, label or value. Use it when "
        "you know what you want to press but not its exact id.";
    Json properties = Json::object();
    properties["text"] = Json::object();
    properties["text"]["type"] = "string";
    properties["text"]["description"] = "Case-insensitive text to match.";
    tool.parameters = Json::object();
    tool.parameters["type"] = "object";
    tool.parameters["properties"] = properties;
    tool.parameters["required"] = Json::array();
    tool.parameters["required"].push_back("text");
    tool.handler = [ui](const Json& arguments) -> Json {
      std::string needle = arguments["text"].as_string();
      for (char& c : needle) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
      const Json elements = ui->all_elements();
      Json matches = Json::array();
      for (const Json& element : elements.items()) {
        std::string haystack = element["id"].as_string() + " " +
                               element["label"].as_string() + " " +
                               element["value"].as_string();
        for (char& c : haystack) {
          c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (needle.empty() || haystack.find(needle) != std::string::npos) {
          if (matches.size() >= 20) break;
          matches.push_back(element);
        }
      }
      Json out = Json::object();
      out["ok"] = true;
      out["panel"] = ui->panel();
      out["matches"] = matches;
      return out;
    };
    registry.add(std::move(tool));
  }

  {
    Tool tool;
    tool.name = "screen_press";
    tool.description =
        "Press one button on the console: cross activates the focused control, "
        "circle goes back, up/down move focus, l1/r1 switch panel, triangle focuses "
        "the composer. Returns the new panel, focus and status.";
    Json properties = Json::object();
    properties["button"] = Json::object();
    properties["button"]["type"] = "string";
    properties["button"]["description"] =
        "cross, circle, square, triangle, up, down, left, right, l1, r1 or options.";
    tool.parameters = Json::object();
    tool.parameters["type"] = "object";
    tool.parameters["properties"] = properties;
    tool.parameters["required"] = Json::array();
    tool.parameters["required"].push_back("button");
    tool.handler = [ui](const Json& arguments) -> Json {
      return ui->press(arguments["button"].as_string());
    };
    registry.add(std::move(tool));
  }

  {
    Tool tool;
    tool.name = "screen_focus";
    tool.description =
        "Move focus to a control by id, switching panel when needed. Use this "
        "instead of walking with the D-pad when you already know the target.";
    Json properties = Json::object();
    properties["id"] = Json::object();
    properties["id"]["type"] = "string";
    properties["id"]["description"] =
        "Element id from screen_shot, screen_find or the ids listed in the frame.";
    tool.parameters = Json::object();
    tool.parameters["type"] = "object";
    tool.parameters["properties"] = properties;
    tool.parameters["required"] = Json::array();
    tool.parameters["required"].push_back("id");
    tool.handler = [ui](const Json& arguments) -> Json {
      return ui->focus_element(arguments["id"].as_string());
    };
    registry.add(std::move(tool));
  }

  {
    Tool tool;
    tool.name = "screen_type";
    tool.description =
        "Type into the focused field. Works for the message composer and the model "
        "fields. Replaces the field value unless clear is false.";
    Json properties = Json::object();
    properties["text"] = Json::object();
    properties["text"]["type"] = "string";
    properties["text"]["description"] = "Text to enter (up to 1024 characters).";
    properties["clear"] = Json::object();
    properties["clear"]["type"] = "boolean";
    properties["clear"]["description"] = "Replace the field (default true) or append.";
    tool.parameters = Json::object();
    tool.parameters["type"] = "object";
    tool.parameters["properties"] = properties;
    tool.parameters["required"] = Json::array();
    tool.parameters["required"].push_back("text");
    tool.handler = [ui](const Json& arguments) -> Json {
      return ui->type_text(arguments["text"].as_string(),
                           arguments.contains("clear")
                               ? arguments["clear"].as_bool(true)
                               : true);
    };
    registry.add(std::move(tool));
  }

  {
    Tool tool;
    tool.name = "screen_flow";
    tool.description =
        "Run a bounded multi-step UI flow in one call. Each step presses, focuses, "
        "types or asserts what is on screen; the first mismatch stops the flow and "
        "reports its index, so a setup sequence is verified rather than assumed. "
        "Steps: press, focus, type (+clear), expect_panel, expect_focus, "
        "expect_status, expect_text, note, shot.";
    Json properties = Json::object();
    properties["steps"] = Json::object();
    properties["steps"]["type"] = "array";
    properties["steps"]["description"] = "Up to 64 step objects.";
    properties["steps"]["items"] = Json::object();
    properties["steps"]["items"]["type"] = "object";
    tool.parameters = Json::object();
    tool.parameters["type"] = "object";
    tool.parameters["properties"] = properties;
    tool.parameters["required"] = Json::array();
    tool.parameters["required"].push_back("steps");
    tool.handler = [ui](const Json& arguments) -> Json {
      Json flow = Json::object();
      flow["steps"] = arguments["steps"];
      return ui->run_flow(flow);
    };
    tool.timeout_ms = 60000;
    registry.add(std::move(tool));
  }
}

}  // namespace ps5h
