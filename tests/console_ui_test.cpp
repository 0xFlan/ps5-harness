// The console driver is the agent's eyes and hands on its own screen. These
// tests drive a deterministic surface with no server and no model: navigation,
// field entry, activation plumbing, and the verified flow runner.
#include <cassert>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "ps5h/console_ui.h"

using namespace ps5h;

namespace {

ConsoleScreen sample_screen() {
  ConsoleScreen view;
  view.build = "0.1.0-test";
  view.model = "gpt-test";
  view.provider = "api.example.com";
  view.transcript = {"user: hello", "assistant: hi"};
  view.activity = {"model switch -> gpt-test"};
  view.tools.push_back({"fs_list", "list a directory"});
  view.tools.push_back({"env_info", "report the environment"});
  view.home_tile["reason"] = "not_checked";
  view.home_tile["detail"] = "Checking the home screen tile.";
  return view;
}

struct Call {
  std::string id;
  Json value;
};

std::shared_ptr<ConsoleUi> make_ui(std::vector<Call>* calls, bool accept = true) {
  auto ui = std::make_shared<ConsoleUi>();
  ui->set_build("0.1.0-test");
  ui->set_view([] { return sample_screen(); });
  ui->set_activate([calls, accept](const std::string& id, const Json& value) {
    calls->push_back(Call{id, value});
    Json out = Json::object();
    out["ok"] = accept;
    if (!accept) out["error"] = "provider rejected the change";
    return out;
  });
  return ui;
}

std::vector<std::string> rows_of(const std::string& frame) {
  std::vector<std::string> rows;
  std::string current;
  for (char c : frame) {
    if (c == '\n') {
      rows.push_back(current);
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  rows.push_back(current);
  return rows;
}

void test_frame_is_bounded_ascii() {
  std::vector<Call> calls;
  auto ui = make_ui(&calls);
  const std::string frame = ui->frame(72, 20);
  const std::vector<std::string> rows = rows_of(frame);
  assert(rows.size() == 20);
  for (const std::string& row : rows) {
    assert(row.size() == 72);
    for (char c : row) {
      assert(static_cast<unsigned char>(c) >= 32 && static_cast<unsigned char>(c) <= 126);
    }
  }
  assert(frame.find("PS5 HARNESS 0.1.0-test") != std::string::npos);
  assert(frame.find("Activity") != std::string::npos);
  assert(frame.find("gpt-test") != std::string::npos);
  // Clamping keeps a caller from asking for a frame the model cannot use.
  const std::string tiny = ui->frame(1, 1);
  assert(rows_of(tiny).size() == 14);
  assert(rows_of(tiny)[0].size() == 48);
}

void test_navigation() {
  std::vector<Call> calls;
  auto ui = make_ui(&calls);
  assert(ui->panel() == "activity");
  assert(ui->focus() == "rail:activity");

  // Down walks the current panel: its rail tab, then its own controls.
  assert(ui->press("down")["focus"].as_string() == "activity:log");
  assert(ui->press("down")["focus"].as_string() == "rail:activity");

  // Shoulder buttons move between panels and land on that panel's tab.
  Json result = ui->press("r1");
  assert(result["panel"].as_string() == "tools");
  assert(result["focus"].as_string() == "rail:tools");
  assert(ui->press("down")["focus"].as_string() == "tools:list");
  assert(ui->press("down")["focus"].as_string() == "tool:fs_list");
  assert(ui->press("down")["focus"].as_string() == "tool:env_info");
  assert(ui->press("l1")["panel"].as_string() == "activity");
  assert(ui->press("left")["panel"].as_string() == "chat");
  assert(ui->press("right")["panel"].as_string() == "activity");

  // Circle is back to the chat screen; triangle jumps to the composer.
  ui->press("r1");
  assert(ui->panel() == "tools");
  ui->press("circle");
  assert(ui->panel() == "chat");
  ui->press("r1");
  assert(ui->press("triangle")["focus"].as_string() == "composer:field");

  // Square reports the browser-only surface instead of pretending to type.
  result = ui->press("square");
  assert(result["ok"].as_bool(false));
  assert(result["status"].as_string().find("browser surface") != std::string::npos);

  // An unknown button is an error, not a silent no-op.
  result = ui->press("banana");
  assert(!result["ok"].as_bool(true));
  assert(result["error"].as_string().find("unknown button") != std::string::npos);
}

void test_focus_and_typing() {
  std::vector<Call> calls;
  auto ui = make_ui(&calls);
  Json result = ui->focus_element("model:id");
  assert(result["ok"].as_bool(false));
  assert(ui->panel() == "model");
  assert(ui->focus() == "model:id");

  result = ui->type_text("qwen3.5-0.8b", true);
  assert(result["ok"].as_bool(false));
  assert(result["value"].as_string() == "qwen3.5-0.8b");
  assert(ui->type_text("x", false)["value"].as_string() == "qwen3.5-0.8bx");

  // A field is not a button: cross tells the model how to fill it.
  ui->press("cross");
  result = ui->press("cross");
  assert(!result["ok"].as_bool(true));
  assert(result["error"].as_string().find("screen_type") != std::string::npos);

  // Typing onto a button is refused rather than silently ignored.
  ui->focus_element("model:apply");
  result = ui->type_text("oops", true);
  assert(!result["ok"].as_bool(true));
  assert(result["error"].as_string().find("not a field") != std::string::npos);

  // Unknown ids report what to do next.
  result = ui->focus_element("nope:nothing");
  assert(!result["ok"].as_bool(true));
  assert(result["error"].as_string().find("screen_find") != std::string::npos);
}

void test_model_apply_reaches_the_host() {
  std::vector<Call> calls;
  auto ui = make_ui(&calls);
  ui->focus_element("model:id");
  ui->type_text("qwen-test", true);
  ui->focus_element("model:base_url");
  ui->type_text("https://api.example.com/v1", true);
  ui->focus_element("model:key-env");
  ui->type_text("PS5H_TEST_KEY", true);
  ui->focus_element("model:apply");
  const Json result = ui->press("cross");

  assert(result["ok"].as_bool(false));
  assert(calls.size() == 1);
  assert(calls.back().id == "model:apply");
  assert(calls.back().value["model"].as_string() == "qwen-test");
  assert(calls.back().value["base_url"].as_string() == "https://api.example.com/v1");
  assert(calls.back().value["api_key_env"].as_string() == "PS5H_TEST_KEY");
  assert(calls.back().value["local"].as_bool(true) == false);
  assert(calls.back().value["temperature"].as_number() > 0.19);
  assert(calls.back().value["temperature"].as_number() < 0.21);
  assert(ui->status().find("model applied") != std::string::npos);

  // A rejected apply surfaces the host's reason instead of claiming success.
  std::vector<Call> refused;
  auto rejecting = make_ui(&refused, false);
  rejecting->focus_element("model:id");
  rejecting->type_text("qwen-test", true);
  rejecting->focus_element("model:apply");
  const Json rejected = rejecting->press("cross");
  assert(!rejected["ok"].as_bool(true));
  assert(rejected["error"].as_string() == "provider rejected the change");
  assert(rejecting->status().find("model apply failed") != std::string::npos);

  // No id and no digits are both refused before anything is sent.
  auto bare = make_ui(&refused);
  bare->focus_element("model:apply");
  assert(!bare->press("cross")["ok"].as_bool(true));
  bare->focus_element("model:id");
  bare->type_text("some-model", true);
  bare->focus_element("model:temperature");
  bare->type_text("hot", true);
  bare->focus_element("model:apply");
  const Json bad_temperature = bare->press("cross");
  assert(!bad_temperature["ok"].as_bool(true));
  assert(bad_temperature["error"].as_string().find("not a number") != std::string::npos);
}

void test_composer_and_missing_listener() {
  std::vector<Call> calls;
  auto ui = make_ui(&calls);
  ui->focus_element("composer:field");
  ui->type_text("check the homebrew folder", true);
  assert(ui->composer() == "check the homebrew folder");
  ui->focus_element("composer:send");
  const Json sent = ui->press("cross");
  assert(sent["ok"].as_bool(false));
  assert(calls.size() == 1);
  assert(calls.back().id == "chat:send");
  assert(calls.back().value["message"].as_string() == "check the homebrew folder");
  assert(ui->composer().empty());

  // An empty message is refused locally.
  const Json empty = ui->press("cross");
  assert(!empty["ok"].as_bool(true));
  assert(empty["error"].as_string().find("empty") != std::string::npos);

  // A surface with no listener says so instead of dropping the action.
  ConsoleUi detached;
  detached.set_view([] { return sample_screen(); });
  detached.focus_element("composer:field");
  detached.type_text("hello", true);
  detached.focus_element("composer:send");
  const Json dropped = detached.press("cross");
  assert(!dropped["ok"].as_bool(true));
  assert(dropped["error"].as_string().find("no listener") != std::string::npos);
}

void test_flow_runs_and_stops_on_mismatch() {
  std::vector<Call> calls;
  auto ui = make_ui(&calls);
  Json flow = Json::array();
  auto add = [&flow](const char* key, const Json& value) {
    Json step = Json::object();
    step[key] = value;
    flow.push_back(step);
  };
  add("press", Json("r1"));
  add("expect_panel", Json("tools"));
  add("focus", Json("model:id"));
  add("type", Json("qwen-test"));
  add("expect_text", Json("qwen-test"));
  add("focus", Json("model:apply"));
  add("press", Json("cross"));
  add("expect_status", Json("model applied"));
  Json shot = Json::object();
  shot["shot"] = true;
  flow.push_back(shot);

  const Json ok = ui->run_flow(flow);
  assert(ok["ok"].as_bool(false));
  assert(ok["steps_run"].as_number() == 9);
  assert(calls.size() == 1);
  bool saw_frame = false;
  for (const Json& entry : ok["log"].items()) {
    if (entry["frame"].is_string()) saw_frame = true;
  }
  assert(saw_frame);

  // The object form with a steps array works too.
  Json wrapped = Json::object();
  Json one = Json::object();
  one["expect_panel"] = "model";
  wrapped["steps"] = Json::array();
  wrapped["steps"].push_back(one);
  assert(ui->run_flow(wrapped)["ok"].as_bool(false));

  // A mismatch stops the flow at its own index and reports what was seen.
  Json failing = Json::array();
  Json first = Json::object();
  first["expect_panel"] = "tools";
  failing.push_back(first);
  Json second = Json::object();
  second["press"] = "cross";
  failing.push_back(second);
  const Json bad = ui->run_flow(failing);
  assert(!bad["ok"].as_bool(true));
  assert(bad["failed_step"].as_number() == 0);
  assert(bad["error"].as_string().find("tools") != std::string::npos);
  assert(bad["frame"].as_string().find("PS5 HARNESS") != std::string::npos);
  assert(calls.size() == 1);  // the later press never ran

  // Malformed and oversized flows are refused before any step runs.
  const Json nonsense = ui->run_flow(Json("not a flow"));
  assert(!nonsense["ok"].as_bool(true));
  Json many = Json::array();
  for (int i = 0; i < 65; ++i) {
    Json step = Json::object();
    step["note"] = "x";
    many.push_back(step);
  }
  const Json too_long = ui->run_flow(many);
  assert(!too_long["ok"].as_bool(true));
  assert(too_long["error"].as_string().find("64") != std::string::npos);

  Json unknown = Json::object();
  Json unknown_step = Json::object();
  unknown_step["teleport"] = "somewhere";
  unknown["steps"] = Json::array();
  unknown["steps"].push_back(unknown_step);
  const Json unknown_result = ui->run_flow(unknown);
  assert(!unknown_result["ok"].as_bool(true));
  assert(unknown_result["error"].as_string().find("no action") != std::string::npos);
}

void test_elements_and_driving_flag() {
  std::vector<Call> calls;
  auto ui = make_ui(&calls);
  assert(!ui->driving());
  const Json elements = ui->elements();
  assert(elements.size() >= 6);
  bool found_rail = false;
  int focused = 0;
  for (const Json& element : elements.items()) {
    if (element["id"].as_string() == "rail:activity") found_rail = true;
    if (element["focused"].as_bool(false)) ++focused;
  }
  assert(found_rail);
  assert(focused == 1);

  ui->press("down");
  assert(ui->driving());
  const Json state = ui->state();
  assert(state["driving"].as_bool(false));
  assert(state["panel"].as_string() == "activity");
  assert(state["buttons"].size() == 11);
  ui->settle();
  assert(!ui->driving());

  // The browser tells the surface which panel a person is looking at.
  ui->set_panel("model", false);
  assert(ui->panel() == "model");
  assert(ui->focus() == "rail:model");
  ui->set_panel("nonsense", false);
  assert(ui->panel() == "model");
}

void test_tools_register() {
  std::vector<Call> calls;
  auto ui = make_ui(&calls);
  ToolRegistry registry;
  register_console_tools(registry, ui);
  for (const char* name : {"screen_shot", "screen_find", "screen_press",
                           "screen_focus", "screen_type", "screen_flow"}) {
    assert(registry.has(name));
  }
  const Tool* shot = registry.find("screen_shot");
  const Json frame = shot->handler(Json::object());
  assert(frame["ok"].as_bool(false));
  assert(frame["frame"].as_string().find("PS5 HARNESS") != std::string::npos);

  const Tool* press = registry.find("screen_press");
  Json arguments = Json::object();
  arguments["button"] = "r1";
  assert(press->handler(arguments)["panel"].as_string() == "tools");

  const Tool* find = registry.find("screen_find");
  Json query = Json::object();
  query["text"] = "apply";
  const Json matches = find->handler(query);
  assert(matches["ok"].as_bool(false));
  assert(matches["matches"].size() >= 1);
}

}  // namespace

int main() {
  test_frame_is_bounded_ascii();
  test_navigation();
  test_focus_and_typing();
  test_model_apply_reaches_the_host();
  test_composer_and_missing_listener();
  test_flow_runs_and_stops_on_mismatch();
  test_elements_and_driving_flag();
  test_tools_register();
  std::printf("console_ui_test: ok\n");
  return 0;
}
