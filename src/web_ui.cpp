#include "ps5h/web_ui.h"

namespace ps5h {

const char* web_ui_html() {
  return R"PS5HUI(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="theme-color" content="#060a12">
<title>PS5 Harness</title>
<style>
:root{
  --bg:#05080e; --panel:#0a101b; --panel-2:#0d1624; --line:#1a2940;
  --text:#eef4ff; --muted:#8ea3bf; --dim:#5f7592; --blue:#2f8cff;
  --blue-soft:#12345d; --green:#42d392; --red:#ff6b7d; --radius:16px;
}
*{box-sizing:border-box}
html,body{margin:0;height:100%;overflow:hidden}
body{
  color:var(--text); font:14px/1.55 -apple-system,BlinkMacSystemFont,"Segoe UI",
  "Helvetica Neue",Arial,sans-serif; background:
  radial-gradient(100% 80% at 24% -10%,#12294a 0%,transparent 48%),
  radial-gradient(80% 70% at 100% 100%,#0b2039 0%,transparent 45%),var(--bg);
}
button,textarea{font:inherit}
button{color:inherit;cursor:pointer}
:focus-visible{outline:2px solid #66b7ff;outline-offset:2px}
#app{display:grid;grid-template-columns:248px minmax(0,1fr) 304px;height:100vh}
.sidebar,.inspector{background:rgba(9,15,25,.82);backdrop-filter:blur(18px);min-height:0}
.sidebar{border-right:1px solid var(--line);display:flex;flex-direction:column;padding:22px 16px}
.brand{display:flex;align-items:center;gap:12px;padding:0 8px 22px}
.brand-mark{width:38px;height:38px;border-radius:11px;display:grid;place-items:center;
  background:linear-gradient(145deg,#12345d,#07111f);border:1px solid #27466d;
  box-shadow:inset 0 1px 0 rgba(255,255,255,.12),0 8px 24px rgba(0,0,0,.35);
  font-weight:800;letter-spacing:-.08em;color:#f4f8ff}
.brand-mark span{color:var(--blue)}
.brand-name{font-size:12px;font-weight:700;letter-spacing:.18em;color:#cbd9ec}
.brand-sub{font-size:10px;letter-spacing:.16em;color:var(--dim);margin-top:2px}
.new-session{width:100%;border:1px solid #2b4d78;background:linear-gradient(180deg,#14345a,#0d2038);
  border-radius:11px;padding:10px 12px;text-align:left;font-size:13px;font-weight:600;
  box-shadow:inset 0 1px 0 rgba(255,255,255,.08);margin-bottom:20px}
.new-session:hover{border-color:#3d6c9f}
.nav{display:flex;flex-direction:column;gap:3px}
.nav button{background:none;border:0;border-radius:9px;padding:9px 11px;text-align:left;
  color:var(--muted);font-size:13px;display:flex;align-items:center;gap:10px}
.nav button:hover{background:#0d1929;color:var(--text)}
.nav button.active{background:#10233c;color:var(--text)}
.nav .ico{width:16px;text-align:center;color:#75a9e6;font-size:12px}
.section-label{font-size:10px;letter-spacing:.16em;color:var(--dim);margin:24px 10px 9px}
.session-item{background:#0c1727;border:1px solid #1b3556;border-radius:10px;padding:10px 11px}
.session-title{font-size:12px;font-weight:600;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.session-meta{font-size:10px;color:var(--dim);margin-top:4px}
.sidebar-foot{margin-top:auto;padding:16px 8px 0;border-top:1px solid var(--line);
  font-size:11px;color:var(--dim);display:flex;align-items:center;gap:8px}
.dot{width:7px;height:7px;border-radius:50%;background:var(--green);box-shadow:0 0 10px #42d392}
.main{min-width:0;display:flex;flex-direction:column;min-height:0}
.topbar{height:72px;border-bottom:1px solid var(--line);display:flex;align-items:center;
  justify-content:space-between;padding:0 26px;gap:20px;background:rgba(6,10,17,.35)}
.crumb{font-size:10px;letter-spacing:.14em;color:var(--dim)}
h1{font-size:16px;margin:4px 0 0;font-weight:600}
.status{display:flex;align-items:center;gap:12px;font-size:11px;color:var(--muted)}
.pill{border:1px solid #24466e;background:#0b1a2c;border-radius:999px;padding:5px 9px;
  font-size:10px;letter-spacing:.09em;color:#a9c5e7;white-space:nowrap}
.pill.full{border-color:#2d8cff55;color:#9dccff}
.ctx{width:96px;height:5px;background:#132136;border-radius:99px;overflow:hidden}
.ctx i{display:block;height:100%;width:0;background:linear-gradient(90deg,#2f8cff,#70c1ff)}
.messages{flex:1;overflow:auto;padding:30px clamp(20px,5vw,70px) 20px;scroll-behavior:smooth}
.hero{max-width:760px;margin:5vh auto 0;text-align:center}
.hero-mark{width:64px;height:64px;margin:0 auto 20px;border-radius:18px;display:grid;place-items:center;
  background:linear-gradient(145deg,#153b68,#08121f);border:1px solid #2a507d;
  box-shadow:0 20px 60px rgba(0,0,0,.4),inset 0 1px 0 rgba(255,255,255,.12);
  font-size:26px;font-weight:800;letter-spacing:-.1em}
.hero h2{font-size:27px;letter-spacing:-.03em;margin:0 0 8px}
.hero p{color:var(--muted);margin:0 auto;max-width:520px}
.chips{display:flex;gap:8px;justify-content:center;flex-wrap:wrap;margin-top:24px}
.chip{border:1px solid #244568;background:#0b1727;color:#b9cce4;border-radius:999px;
  padding:8px 12px;font-size:11px}
.chip:hover{border-color:#3b76b3;color:white}
.msg{max-width:820px;margin:0 auto 18px;display:flex;gap:12px;align-items:flex-start}
.msg .avatar{width:28px;height:28px;border-radius:9px;flex:0 0 auto;display:grid;place-items:center;
  font-size:11px;font-weight:700;background:#12243a;border:1px solid #29486d;color:#a9cdf7}
.msg.user .avatar{background:#1b3150;color:#fff}
.msg .body{min-width:0;padding-top:3px}
.msg .who{font-size:10px;letter-spacing:.13em;color:var(--dim);margin-bottom:4px}
.msg .text{color:#dbe8f8;overflow-wrap:anywhere}
.msg.user .text{color:#fff}
.msg .text p{margin:0 0 10px}
.msg .text pre{background:#060c15;border:1px solid #1c2d45;border-radius:10px;padding:13px 15px;
  overflow:auto;font:12px/1.5 ui-monospace,SFMono-Regular,Menlo,monospace;color:#c8dbf3}
.msg .text code{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:.92em}
.thinking{display:inline-flex;gap:5px;align-items:center;color:var(--muted);font-size:12px}
.thinking i{width:5px;height:5px;border-radius:50%;background:#5b9fe8;animation:b 1.1s infinite}
.thinking i:nth-child(2){animation-delay:.15s}.thinking i:nth-child(3){animation-delay:.3s}
@keyframes b{0%,70%,100%{opacity:.25;transform:translateY(0)}35%{opacity:1;transform:translateY(-3px)}}
.composer-wrap{padding:0 clamp(20px,5vw,70px) 24px}
.composer{max-width:820px;margin:0 auto;border:1px solid #27476d;background:rgba(10,18,30,.94);
  border-radius:16px;padding:12px 12px 9px;box-shadow:0 18px 60px rgba(0,0,0,.32)}
.composer:focus-within{border-color:#3f7fbe;box-shadow:0 0 0 3px rgba(47,140,255,.09),0 18px 60px rgba(0,0,0,.32)}
.composer textarea{width:100%;min-height:48px;max-height:190px;resize:none;border:0;outline:0;
  background:transparent;color:var(--text);padding:4px 5px;font-size:14px;line-height:1.5}
.composer textarea::placeholder{color:#637a98}
.composer-row{display:flex;align-items:center;justify-content:space-between;gap:10px;padding:5px 4px 0}
.composer-hint{font-size:10px;color:var(--dim)}
.send{border:1px solid #3f8fe8;background:linear-gradient(180deg,#2f8cff,#1d67c5);border-radius:9px;
  padding:7px 13px;font-size:12px;font-weight:700;color:white;box-shadow:0 5px 15px #0c4b9c55}
.send:disabled{opacity:.45;cursor:default}
.inspector{border-left:1px solid var(--line);display:flex;flex-direction:column;min-height:0}
.tabs{display:flex;gap:4px;padding:16px 14px 0}
.tabs button{border:0;background:none;color:var(--dim);padding:7px 9px;font-size:11px;
  letter-spacing:.07em;border-bottom:2px solid transparent}
.tabs button.active{color:#dcecff;border-color:var(--blue)}
.panel{display:none;padding:17px 17px 10px;overflow:auto;flex:1;min-height:0}
.panel.active{display:block}
.panel h3{font-size:10px;letter-spacing:.16em;color:var(--dim);font-weight:600;margin:0 0 10px}
.activity{display:flex;flex-direction:column;gap:8px}
.event{font:11px/1.45 ui-monospace,SFMono-Regular,Menlo,monospace;color:#8ea6c4;
  border-left:2px solid #234568;padding:2px 0 2px 9px;overflow-wrap:anywhere}
.event:last-child{border-color:var(--blue);color:#c4dcf8}
.tool{border:1px solid #1a2c45;border-radius:9px;padding:9px 10px;margin-bottom:7px;background:#091422}
.tool:hover{border-color:#2e5a89}
.tool-name{font:11px ui-monospace,SFMono-Regular,Menlo,monospace;color:#b9d9ff}
.tool-desc{font-size:10px;color:var(--dim);margin-top:3px;line-height:1.4}
.mobile-nav{display:none}
.stats{border-top:1px solid var(--line);padding:15px 17px 18px;font-size:11px;color:var(--muted)}
.stat{display:flex;justify-content:space-between;padding:3px 0}
.stat b{color:#dcecff;font-weight:600}
.safety{margin-top:12px;padding:9px 10px;border:1px solid #1f3552;border-radius:8px;
  color:var(--dim);font-size:10px;line-height:1.45}
#login{position:fixed;inset:0;background:rgba(3,6,11,.86);backdrop-filter:blur(15px);
  display:grid;place-items:center;z-index:20}
#login.hidden{display:none}
.login-card{width:min(380px,calc(100vw - 40px));background:#0a1320;border:1px solid #27476d;
  border-radius:18px;padding:26px;box-shadow:0 30px 100px #000}
.login-card h2{margin:0 0 7px;font-size:20px}
.login-card p{margin:0 0 18px;color:var(--muted);font-size:12px}
.login-card input{width:100%;background:#060d17;border:1px solid #284b72;border-radius:9px;
  color:white;padding:11px 12px;outline:0;margin-bottom:10px}
.login-card button{width:100%;border:1px solid #3f8fe8;background:#1d67c5;border-radius:9px;
  padding:10px;font-weight:700}
.login-error{color:var(--red);font-size:11px;min-height:16px;margin-top:8px}
#toast{position:fixed;right:18px;bottom:18px;background:#10233a;border:1px solid #2d5a8b;
  border-radius:10px;padding:10px 13px;font-size:12px;color:#dcecff;opacity:0;
  transform:translateY(8px);transition:.2s;pointer-events:none;z-index:30}
#toast.show{opacity:1;transform:none}
button.pill{font:inherit;cursor:pointer}
.input-pill{border-color:#2b5c8e;color:#a9d2ff;background:#0b1a2c}
.input-pill.on{border-color:#42d392;color:#b8ffe0;background:#0b241e}
.composer-tools{display:flex;align-items:center;gap:6px;flex-wrap:wrap;justify-content:flex-end}
.icon-btn{border:1px solid #27476d;background:#0b1727;color:#c4dcf8;border-radius:8px;
  min-width:32px;height:30px;padding:0 8px;font-size:13px}
.icon-btn:hover,.icon-btn:focus-visible{border-color:#4b91d8;background:#122741}
.icon-btn.listening{border-color:#ff9f6b;color:#ffe0cf;background:#2a1711;
  animation:micPulse 1.1s ease-in-out infinite}
@keyframes micPulse{50%{box-shadow:0 0 0 4px #ff9f6b22}}
.mic-status{font-size:10px;color:var(--dim);max-width:180px;overflow:hidden;
  text-overflow:ellipsis;white-space:nowrap}
.osk{position:fixed;left:50%;bottom:112px;transform:translateX(-50%);z-index:25;
  width:min(780px,calc(100vw - 32px));background:rgba(8,15,26,.98);border:1px solid #2c5684;
  border-radius:16px;padding:12px;box-shadow:0 24px 90px #000c,inset 0 1px 0 #ffffff12}
.osk.hidden,.input-help.hidden{display:none}
.osk-head{display:flex;align-items:center;justify-content:space-between;gap:10px;
  margin:0 2px 9px;font-size:10px;letter-spacing:.12em;color:#9bb9dc}
.osk-head b{color:#e3f0ff;font-weight:600;letter-spacing:.04em}
.osk-close{border:0;background:none;color:#8ea3bf;font-size:11px;padding:2px 4px}
.osk-grid{display:grid;grid-template-columns:repeat(10,minmax(0,1fr));gap:5px}
.osk-key{min-height:34px;border:1px solid #27476d;border-radius:7px;background:#0c1a2b;
  color:#dcecff;font-size:12px;line-height:1;padding:2px;touch-action:manipulation}
.osk-key:hover,.osk-key:focus-visible{border-color:#4b91d8;background:#173252}
.osk-key.space{grid-column:span 3}.osk-key.wide{grid-column:span 2}
.osk-actions{display:grid;grid-template-columns:repeat(5,minmax(0,1fr));gap:5px;margin-top:7px}
.osk-actions .osk-key{font-size:10px}
#virtual-pointer{position:fixed;left:0;top:0;width:18px;height:18px;z-index:60;
  border:2px solid #d9efff;border-radius:50%;background:#2f8cff55;pointer-events:none;
  box-shadow:0 0 0 3px #05080eaa,0 0 18px #2f8cffcc;opacity:0;transform:translate(-50%,-50%)}
#virtual-pointer.visible{opacity:1}
#virtual-pointer:after{content:"";position:absolute;left:7px;top:7px;width:2px;height:2px;
  border-radius:50%;background:#fff}
.input-help{position:fixed;inset:0;z-index:35;display:grid;place-items:center;
  background:#03060bb8;backdrop-filter:blur(12px);padding:20px}
.help-card{width:min(600px,calc(100vw - 32px));max-height:calc(100vh - 40px);overflow:auto;
  background:#0a1320;border:1px solid #2b527e;border-radius:18px;padding:24px;
  box-shadow:0 30px 100px #000}
.help-card h2{margin:0 0 7px;font-size:20px}
.help-card p{color:var(--muted);font-size:12px;margin:0 0 14px}
.help-card dl{display:grid;grid-template-columns:auto 1fr;gap:7px 14px;margin:0 0 16px;font-size:12px}
.help-card dt{color:#cfe7ff;font-weight:600;white-space:nowrap}
.help-card dd{margin:0;color:var(--muted)}
.help-card .help-note{border-left:2px solid #2f8cff;padding:2px 0 2px 10px;color:#9fbbdb}
.help-card button{border:1px solid #3f8fe8;background:#1d67c5;border-radius:9px;padding:9px 14px;
  font-weight:700;margin-top:13px}
@media (max-width:1150px){
  #app{grid-template-columns:220px minmax(0,1fr)}
  .inspector{display:none;position:fixed;right:0;top:72px;bottom:0;width:min(340px,100vw);z-index:12;box-shadow:-18px 0 50px rgba(0,0,0,.35)}
  .inspector.mobile-open{display:flex}
  .mobile-nav{display:flex;position:fixed;left:50%;bottom:12px;transform:translateX(-50%);z-index:14;
    background:#0a1422;border:1px solid #27486e;border-radius:12px;padding:4px;gap:3px;
    box-shadow:0 12px 35px rgba(0,0,0,.45)}
  .mobile-nav button{border:0;background:transparent;color:var(--muted);border-radius:8px;padding:8px 13px;font-size:11px}
  .mobile-nav button.active{background:#173354;color:#e9f4ff}
  .messages{padding-bottom:82px}
  .composer-wrap{padding-bottom:70px}
}
@media (max-width:760px){#app{grid-template-columns:1fr}.sidebar{display:none}
  .topbar{padding:0 16px}.messages{padding:22px 16px 82px}.composer-wrap{padding:0 12px 70px}
  .status .pill{display:none}
  .inspector{top:72px;left:0;width:100vw}
  .composer-hint{display:none}
  .composer-tools{width:100%;justify-content:space-between}
  .mic-status{max-width:130px}
  .osk{left:10px;right:10px;bottom:78px;transform:none;width:auto;padding:9px}
  .osk-grid{grid-template-columns:repeat(7,minmax(0,1fr));gap:4px}
  .osk-key{min-height:36px;font-size:11px}
  .osk-actions{grid-template-columns:repeat(3,minmax(0,1fr))}
  .help-card{padding:18px}.help-card dl{grid-template-columns:1fr;gap:2px}
  .help-card dd{margin-bottom:8px}}
@media (min-width:1151px){.mobile-nav{display:none}}
</style>
</head>
<body>
<div id="app">
  <aside class="sidebar">
    <div class="brand">
      <div class="brand-mark">H<span>.</span></div>
      <div><div class="brand-name">PS5 HARNESS</div><div class="brand-sub">CONSOLE WORKSPACE</div></div>
    </div>
    <button class="new-session" id="new-session">+  New session</button>
    <nav class="nav">
      <button class="active"><span class="ico">◈</span>Chat</button>
      <button id="show-activity"><span class="ico">⌁</span>Activity</button>
      <button id="show-tools"><span class="ico">⌘</span>Tools</button>
    </nav>
    <div class="section-label">CURRENT SESSION</div>
    <div class="session-item">
      <div class="session-title" id="session-title">Console workspace</div>
      <div class="session-meta" id="session-meta">new conversation</div>
    </div>
    <div class="sidebar-foot"><span class="dot" id="dot"></span><span id="conn">Connecting</span></div>
  </aside>

  <main class="main">
    <header class="topbar">
      <div><div class="crumb">PS5 HARNESS / CHAT</div><h1>Console workspace</h1></div>
      <div class="status">
        <span class="pill full" id="mode">FULL ACCESS</span>
        <button class="pill input-pill" id="input-pill" type="button" title="Keyboard, controller and voice input help">KEYBOARD + MOUSE</button>
        <span class="pill" id="model">model</span>
        <span class="ctx"><i id="ctxbar"></i></span>
      </div>
    </header>
    <section class="messages" id="messages">
      <div class="hero" id="hero">
        <div class="hero-mark">H<span style="color:#2f8cff">.</span></div>
        <h2>What are we researching?</h2>
        <p>Map the console, inspect a binary, write a tool, or carry a long investigation across turns. Every tool result stays in this session until it is compacted.</p>
        <div class="chips">
          <button class="chip" data-prompt="Map the PS5 architecture and filesystem layout. Start with env_info, then inspect the most important mounts and binaries.">Map the console</button>
          <button class="chip" data-prompt="Find the most interesting binaries under /data and identify their formats, imports and strings.">Audit binaries</button>
          <button class="chip" data-prompt="Write a new local tool or plugin for this harness, then explain how to load it.">Build a tool</button>
          <button class="chip" data-prompt="Plan a long research mission over the console, with explicit evidence and stop conditions.">Plan a mission</button>
        </div>
      </div>
    </section>
    <div class="composer-wrap">
      <nav class="mobile-nav" aria-label="Workspace panels">
        <button data-mobile="chat" class="active">Chat</button>
        <button data-mobile="activity">Activity</button>
        <button data-mobile="tools">Tools</button>
      </nav>
      <form class="composer" id="composer">
        <textarea id="prompt" rows="2" placeholder="Ask the harness to inspect, build, edit or investigate…"></textarea>
        <div class="composer-row">
          <span class="composer-hint">Enter to send · Shift+Enter for a new line · Ctrl/⌘K focuses</span>
          <div class="composer-tools">
            <span class="mic-status" id="mic-status" aria-live="polite"></span>
            <button class="icon-btn" id="osk-button" type="button" title="Open the on-screen keyboard" aria-label="Open the on-screen keyboard">⌨</button>
            <button class="icon-btn" id="mic-button" type="button" title="Dictate with the browser microphone" aria-label="Dictate with the browser microphone" aria-pressed="false">🎙</button>
            <button class="icon-btn" id="input-help-button" type="button" title="Input help" aria-label="Input help">?</button>
            <button class="send" id="send" type="submit">Send</button>
          </div>
        </div>
      </form>
    </div>
  </main>

  <aside class="inspector">
    <div class="tabs">
      <button class="active" data-tab="activity">ACTIVITY</button>
      <button data-tab="tools">TOOLS</button>
    </div>
    <div class="panel active" id="panel-activity">
      <h3>LIVE TOOL LOG</h3>
      <div class="activity" id="activity"><div class="event">Waiting for a turn…</div></div>
    </div>
    <div class="panel" id="panel-tools">
      <h3>REGISTERED TOOLS</h3>
      <div id="tools"><div class="event">Loading…</div></div>
    </div>
    <div class="stats">
      <div class="stat"><span>Context</span><b id="ctx-text">0 / 120k</b></div>
      <div class="stat"><span>Messages</span><b id="msg-count">0</b></div>
      <div class="stat"><span>Compactions</span><b id="compactions">0</b></div>
      <div class="stat"><span>Tokens</span><b id="tokens">0</b></div>
      <div class="stat"><span>Steps</span><b id="steps">0</b></div>
      <div class="safety">Runs on your console. Paths and network calls still obey the configured roots and URL policy.</div>
    </div>
  </aside>
</div>

<div id="osk" class="osk hidden" role="dialog" aria-modal="true" aria-labelledby="osk-title">
  <div class="osk-head">
    <span><b id="osk-title">On-screen keyboard</b> · <span id="osk-target">composer</span></span>
    <button class="osk-close" id="osk-close" type="button">Close</button>
  </div>
  <div class="osk-grid" id="osk-grid"></div>
  <div class="osk-actions" id="osk-actions"></div>
</div>
<div id="input-help" class="input-help hidden" role="dialog" aria-modal="true" aria-labelledby="input-help-title">
  <div class="help-card">
    <h2 id="input-help-title">Keyboard, controller and voice</h2>
    <p>Use whichever input is in front of you. The composer stays a normal text field.</p>
    <dl>
      <dt>Keyboard + mouse</dt><dd>Type in the composer. Enter sends, Shift+Enter adds a line, Ctrl/⌘K returns to the composer. Every button, panel and tool chip is clickable.</dd>
      <dt>Controller buttons</dt><dd>D-pad or left stick moves focus; ✕ activates; ○ goes back; □ opens the keyboard; △ focuses the composer; Options toggles the keyboard; Create opens this help; L1/R1 switch Activity and Tools.</dd>
      <dt>Touchpad + pointer</dt><dd>Touch moves the pointer when the browser exposes gamepad touch surfaces; touchpad click, R3 or the touchpad button clicks. If coordinates are not exposed, use the right stick as a pointer and L3 to show it.</dd>
      <dt>Dictation</dt><dd>The mic button uses browser speech recognition and inserts final phrases at the cursor. The device running this page must expose the microphone.</dd>
    </dl>
    <p class="help-note">Speech recognition needs HTTPS or localhost. The default LAN HTTP address will show that requirement instead of silently failing.</p>
    <p class="help-note">A headset paired to the console is not visible to a remote browser. Select the controller or headset as the audio input on the device running this page. On a browser without Web Speech, use that device's own dictation key.</p>
    <button id="help-close" type="button">Close</button>
  </div>
</div>
<div id="virtual-pointer" aria-hidden="true"></div>

<div id="login" class="hidden">
  <form class="login-card" id="login-form">
    <h2>Connect to the harness</h2>
    <p>Enter the bearer token printed in the console log when the server started.</p>
    <input id="token-input" type="password" autocomplete="off" placeholder="Access token">
    <button type="submit">Connect</button>
    <div class="login-error" id="login-error"></div>
  </form>
</div>
<div id="toast"></div>

<script>
const $ = (id) => document.getElementById(id);
const state = { token: '', busy: false, polling: false, history: [] };
const AUTH_KEY = 'ps5h-token';

function esc(value) {
  return String(value == null ? '' : value)
    .replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;')
    .replace(/"/g,'&quot;').replace(/'/g,'&#39;');
}
function fmt(value) {
  const text = String(value == null ? '' : value);
  const parts = text.split('```');
  return parts.map((part, i) => {
    if (i % 2 === 1) {
      let body = part, lang = '';
      const nl = body.indexOf('\n');
      if (nl >= 0) { lang = body.slice(0, nl).trim(); body = body.slice(nl + 1); }
      return '<pre><code data-lang="' + esc(lang) + '">' + esc(body) + '</code></pre>';
    }
    return esc(part).replace(/\n/g, '<br>');
  }).join('');
}
function toast(message) {
  const el = $('toast'); el.textContent = message; el.classList.add('show');
  clearTimeout(toast.timer); toast.timer = setTimeout(() => el.classList.remove('show'), 3000);
}
function setConnected(ok) {
  $('dot').style.background = ok ? 'var(--green)' : 'var(--red)';
  $('dot').style.boxShadow = '0 0 10px ' + (ok ? '#42d392' : '#ff6b7d');
  $('conn').textContent = ok ? 'Connected to console' : 'Disconnected';
}
function setToken(token) {
  state.token = token || '';
  if (state.token) sessionStorage.setItem(AUTH_KEY, state.token);
  else sessionStorage.removeItem(AUTH_KEY);
}
function authHeaders() {
  return state.token ? { 'Authorization': 'Bearer ' + state.token } : {};
}
class AuthError extends Error {}
async function api(path, options = {}) {
  const response = await fetch(path, {
    ...options,
    headers: { ...(options.headers || {}), ...authHeaders() }
  });
  if (response.status === 401) throw new AuthError('unauthorized');
  const text = await response.text();
  let data = {};
  try { data = text ? JSON.parse(text) : {}; } catch (_) { data = { error: text }; }
  if (!response.ok) throw new Error(data.error || ('HTTP ' + response.status));
  return data;
}
function showLogin(message) {
  $('login').classList.remove('hidden');
  $('login-error').textContent = message || '';
  setTimeout(() => $('token-input').focus(), 30);
}
function hideLogin() { $('login').classList.add('hidden'); }

function addMessage(role, content, pending) {
  const hero = $('hero'); if (hero) hero.remove();
  const wrap = document.createElement('div');
  wrap.className = 'msg ' + (role === 'user' ? 'user' : 'assistant');
  if (pending) wrap.id = 'pending-message';
  wrap.innerHTML = '<div class="avatar">' + (role === 'user' ? 'YOU' : 'H') + '</div>' +
    '<div class="body"><div class="who">' + (role === 'user' ? 'YOU' : 'HARNESS') + '</div>' +
    '<div class="text">' + (pending
      ? '<span class="thinking"><i></i><i></i><i></i> working</span>'
      : fmt(content)) + '</div></div>';
  $('messages').appendChild(wrap);
  $('messages').scrollTop = $('messages').scrollHeight;
  return wrap;
}
function clearMessages() { $('messages').innerHTML = ''; }
function renderHistory(messages) {
  clearMessages();
  let shown = 0;
  for (const message of (messages || [])) {
    const role = message && message.role;
    const content = message && message.content;
    if ((role === 'user' || role === 'assistant') && content) {
      addMessage(role, content, false); shown++;
    }
  }
  if (!shown) {
    $('messages').innerHTML = '<div class="hero" id="hero"><div class="hero-mark">H<span style="color:#2f8cff">.</span></div><h2>What are we researching?</h2><p>Map the console, inspect a binary, write a tool, or carry a long investigation across turns. Every tool result stays in this session until it is compacted.</p><div class="chips"><button class="chip" data-prompt="Map the PS5 architecture and filesystem layout. Start with env_info, then inspect the most important mounts and binaries.">Map the console</button><button class="chip" data-prompt="Find the most interesting binaries under /data and identify their formats, imports and strings.">Audit binaries</button><button class="chip" data-prompt="Write a new local tool or plugin for this harness, then explain how to load it.">Build a tool</button><button class="chip" data-prompt="Plan a long research mission over the console, with explicit evidence and stop conditions.">Plan a mission</button></div></div>';
    bindChips();
  }
  $('messages').scrollTop = $('messages').scrollHeight;
}
function bindChips() {
  document.querySelectorAll('[data-prompt]').forEach((button) => {
    button.onclick = () => { $('prompt').value = button.dataset.prompt; $('prompt').focus(); };
  });
}
function renderState(data) {
  $('model').textContent = data.model || 'model';
  $('mode').textContent = data.mode === 'plan' ? 'PLAN · READ ONLY' : 'FULL ACCESS';
  $('mode').classList.toggle('full', data.mode !== 'plan');
  const context = data.context || {};
  const used = Number(context.chars || 0), limit = Number(context.limit || 1);
  $('ctxbar').style.width = Math.min(100, Math.round(used / Math.max(1, limit) * 100)) + '%';
  $('ctx-text').textContent = Math.round(used / 1000) + 'k / ' + Math.round(limit / 1000) + 'k';
  $('msg-count').textContent = context.messages || 0;
  $('compactions').textContent = context.compactions || 0;
  const usage = data.usage || {};
  $('tokens').textContent = Number(usage.total_tokens || 0).toLocaleString();
  $('steps').textContent = usage.steps || 0;
  const activity = $('activity');
  const events = data.events || [];
  activity.innerHTML = events.length ? events.map((line) => '<div class="event">' + esc(line) + '</div>').join('')
    : '<div class="event">Waiting for a turn…</div>';
  activity.scrollTop = activity.scrollHeight;
  if (data.tools) {
    $('tools').innerHTML = data.tools.map((tool) =>
      '<button class="tool" style="width:100%;text-align:left" data-tool="' + esc(tool.name) + '">' +
      '<div class="tool-name">' + esc(tool.name) + '</div>' +
      '<div class="tool-desc">' + esc(tool.description || '') + '</div></button>').join('');
    document.querySelectorAll('[data-tool]').forEach((button) => {
      button.onclick = () => {
        $('prompt').value = 'Use ' + button.dataset.tool + ' to ';
        $('prompt').focus();
      };
    });
  }
  if (data.session_title) $('session-title').textContent = data.session_title;
  if (data.context) {
    $('session-meta').textContent = (data.context.messages || 0) + ' messages · ' +
      (data.context.compactions || 0) + ' compactions';
  }
  setConnected(true);
}
async function refreshState() {
  if (state.polling || !state.token) return;
  state.polling = true;
  try { renderState(await api('/api/state')); }
  catch (error) {
    if (error instanceof AuthError) showLogin('Token rejected. Enter the current token.');
    else setConnected(false);
  } finally { state.polling = false; }
}
async function loadHistory() {
  try {
    const data = await api('/api/history');
    state.history = data.messages || [];
    renderHistory(state.history);
  } catch (error) {
    if (error instanceof AuthError) showLogin('Token rejected. Enter the current token.');
  }
}
async function boot() {
  const url = new URL(window.location.href);
  const queryToken = url.searchParams.get('token');
  if (queryToken) { setToken(queryToken); url.searchParams.delete('token'); history.replaceState({}, '', url); }
  else setToken(sessionStorage.getItem(AUTH_KEY) || '');
  if (!state.token) { showLogin(''); return; }
  hideLogin();
  await loadHistory();
  await refreshState();
  setInterval(refreshState, 1600);
}
$('login-form').addEventListener('submit', async (event) => {
  event.preventDefault();
  setToken($('token-input').value.trim());
  if (!state.token) { $('login-error').textContent = 'Enter a token.'; return; }
  try {
    await api('/api/state');
    hideLogin(); await loadHistory(); await refreshState();
    setInterval(refreshState, 1600);
  } catch (error) {
    showLogin(error instanceof AuthError ? 'Token rejected.' : String(error.message || error));
  }
});
$('new-session').addEventListener('click', async () => {
  if (state.busy) return;
  if (!confirm('Start a new session? The current transcript will be cleared.')) return;
  try { await api('/api/reset', { method: 'POST' }); state.history = []; renderHistory([]); toast('New session'); await refreshState(); }
  catch (error) { toast(String(error.message || error)); }
});
$('show-activity').addEventListener('click', () => selectTab('activity'));
$('show-tools').addEventListener('click', () => selectTab('tools'));
function selectTab(name) {
  document.querySelectorAll('.tabs button').forEach((b) => b.classList.toggle('active', b.dataset.tab === name));
  $('panel-activity').classList.toggle('active', name === 'activity');
  $('panel-tools').classList.toggle('active', name === 'tools');
  if (window.innerWidth <= 1150) {
    document.querySelector('.inspector').classList.add('mobile-open');
    document.body.classList.add('mobile-panel');
    document.querySelectorAll('[data-mobile]').forEach((b) => b.classList.toggle('active', b.dataset.mobile === name));
  }
}
function closeMobilePanel() {
  document.querySelector('.inspector').classList.remove('mobile-open');
  document.body.classList.remove('mobile-panel');
  document.querySelectorAll('[data-mobile]').forEach((b) => b.classList.toggle('active', b.dataset.mobile === 'chat'));
}
document.querySelectorAll('[data-mobile]').forEach((button) => {
  button.onclick = () => {
    if (button.dataset.mobile === 'chat') closeMobilePanel();
    else selectTab(button.dataset.mobile);
  };
});
document.querySelectorAll('.tabs button').forEach((button) => {
  button.onclick = () => selectTab(button.dataset.tab);
});
$('composer').addEventListener('submit', async (event) => {
  event.preventDefault();
  if (state.busy) return;
  const message = $('prompt').value.trim();
  if (!message) return;
  state.busy = true;
  $('send').disabled = true;
  $('prompt').value = '';
  addMessage('user', message, false);
  addMessage('assistant', '', true);
  try {
    const data = await api('/api/chat', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ message })
    });
    const pending = $('pending-message'); if (pending) pending.remove();
    if (data.ok) addMessage('assistant', data.answer || '(no content)', false);
    else addMessage('assistant', 'Error: ' + (data.error || 'turn failed'), false);
    await refreshState();
  } catch (error) {
    const pending = $('pending-message'); if (pending) pending.remove();
    if (error instanceof AuthError) showLogin('Token rejected. Enter the current token.');
    else addMessage('assistant', 'Error: ' + String(error.message || error), false);
  } finally {
    state.busy = false; $('send').disabled = false; $('prompt').focus();
  }
});
$('prompt').addEventListener('keydown', (event) => {
  if (event.key === 'Enter' && !event.shiftKey) { event.preventDefault(); $('composer').requestSubmit(); }
});

/* ---- input layer: keyboard, mouse, controller, touchpad and dictation ---- */
const inputState = {
  pad: null, padIndex: null, padId: '', lastButtons: [], lastDir: '', nextMove: 0,
  pointer: { x: window.innerWidth / 2, y: window.innerHeight / 2 },
  pointerVisible: false, touchActive: false, touchAt: 0, touchX: 0, touchY: 0,
  osk: false, oskViaController: false, oskTarget: null,
  caret: { start: 0, end: 0 }, shift: false,
  listening: false, micStarting: false, micRetries: 0, recognition: null
};
const OSK_ROWS = [
  ['q','w','e','r','t','y','u','i','o','p'],
  ['a','s','d','f','g','h','j','k','l'],
  ['z','x','c','v','b','n','m','-','_','.'],
  ['1','2','3','4','5','6','7','8','9','0'],
  ['@','#','$','%','&','*','(',')','+','=']
];
function isEditable(el) {
  return !!el && (el.tagName === 'TEXTAREA' || el.tagName === 'INPUT' || el.isContentEditable);
}
function isVisible(el) {
  if (!el || el.closest('.hidden')) return false;
  const css = getComputedStyle(el);
  return css.display !== 'none' && css.visibility !== 'hidden' && el.getClientRects().length > 0;
}
function visibleFocusables() {
  return Array.from(document.querySelectorAll(
    'button:not([disabled]),textarea:not([disabled]),input:not([disabled]),a[href],[tabindex]:not([tabindex="-1"])'
  )).filter(isVisible);
}
function saveCaret(target) {
  if (!isEditable(target)) return;
  const start = Number.isInteger(target.selectionStart) ? target.selectionStart : target.value.length;
  const end = Number.isInteger(target.selectionEnd) ? target.selectionEnd : start;
  inputState.caret = { start: start, end: end };
}
function focusMove(direction) {
  const items = visibleFocusables();
  if (!items.length) return;
  const active = document.activeElement;
  if (!isVisible(active) || items.indexOf(active) < 0) {
    const preferred = inputState.osk ? items.find((item) => item.closest('#osk')) : null;
    (preferred || items[0]).focus({ preventScroll: true });
    return;
  }
  const from = active.getBoundingClientRect();
  const cx = from.left + from.width / 2, cy = from.top + from.height / 2;
  let best = null, bestScore = Infinity;
  for (const item of items) {
    if (item === active) continue;
    const box = item.getBoundingClientRect();
    const x = box.left + box.width / 2, y = box.top + box.height / 2;
    const dx = x - cx, dy = y - cy;
    let primary = 0, secondary = 0;
    if (direction === 'right' && dx > 3) { primary = dx; secondary = Math.abs(dy); }
    else if (direction === 'left' && dx < -3) { primary = -dx; secondary = Math.abs(dy); }
    else if (direction === 'down' && dy > 3) { primary = dy; secondary = Math.abs(dx); }
    else if (direction === 'up' && dy < -3) { primary = -dy; secondary = Math.abs(dx); }
    if (!primary) continue;
    const score = primary + secondary * 2.2;
    if (score < bestScore) { bestScore = score; best = item; }
  }
  if (best) {
    best.focus({ preventScroll: true });
    best.scrollIntoView({ block: 'nearest', inline: 'nearest' });
  }
}
function activateFocused() {
  const active = document.activeElement;
  if (!active || active === document.body) { $('prompt').focus(); return; }
  if (isEditable(active)) {
    active.focus();
    openOSK(true, active);
    return;
  }
  if (typeof active.click === 'function') active.click();
}
function controllerBack() {
  if (inputState.osk) { closeOSK(); return; }
  if (!$('input-help').classList.contains('hidden')) { toggleHelp(false); return; }
  if (document.body.classList.contains('mobile-panel')) { closeMobilePanel(); return; }
  if (document.activeElement && document.activeElement.blur) document.activeElement.blur();
}
function scrollMessages(delta) {
  const messages = $('messages');
  if (messages) messages.scrollTop += delta;
  else window.scrollBy(0, delta);
}
function setPointer(x, y) {
  inputState.pointer.x = Math.max(8, Math.min(window.innerWidth - 8, x));
  inputState.pointer.y = Math.max(8, Math.min(window.innerHeight - 8, y));
  inputState.pointerVisible = true;
  const pointer = $('virtual-pointer');
  pointer.style.left = inputState.pointer.x + 'px';
  pointer.style.top = inputState.pointer.y + 'px';
  pointer.classList.add('visible');
  pointer.style.transform = 'translate(-50%,-50%)';
}
function movePointer(dx, dy) {
  setPointer(inputState.pointer.x + dx, inputState.pointer.y + dy);
}
function clickPointer() {
  setPointer(inputState.pointer.x, inputState.pointer.y);
  const target = document.elementFromPoint(inputState.pointer.x, inputState.pointer.y);
  if (!target || target === $('virtual-pointer')) return;
  const control = target.closest('button,a[href],textarea,input,[role="button"]') || target;
  if (typeof control.focus === 'function') control.focus({ preventScroll: true });
  if (typeof control.click === 'function') control.click();
}
function pointerFromTouch(touch) {
  const pos = touch && touch.position;
  if (!pos) return false;
  setPointer((Number(pos.x) + 1) * window.innerWidth / 2,
    (Number(pos.y) + 1) * window.innerHeight / 2);
  return true;
}
function handleTouchpad(pad, now) {
  const touches = pad.touches || [];
  if (touches.length) {
    const touch = touches[0];
    const moved = pointerFromTouch(touch);
    if (moved && !inputState.touchActive) {
      inputState.touchActive = true;
      inputState.touchAt = now;
      inputState.touchX = inputState.pointer.x;
      inputState.touchY = inputState.pointer.y;
    }
  } else if (inputState.touchActive) {
    const held = now - inputState.touchAt;
    const moved = Math.hypot(inputState.pointer.x - inputState.touchX,
      inputState.pointer.y - inputState.touchY);
    inputState.touchActive = false;
    if (held < 280 && moved < 28) clickPointer();
  }
}
function padLabel(pad) {
  const id = String(pad && pad.id || '').toLowerCase();
  if (id.indexOf('dualsense') >= 0) return 'DUALSENSE';
  if (id.indexOf('dualshock') >= 0) return 'DUALSHOCK';
  if (id.indexOf('wireless controller') >= 0) return 'WIRELESS CONTROLLER';
  if (id.indexOf('xbox') >= 0) return 'XBOX PAD';
  return 'CONTROLLER';
}
function setPad(pad) {
  if (pad && (inputState.padIndex !== pad.index || inputState.padId !== pad.id)) {
    inputState.padIndex = pad.index;
    inputState.padId = pad.id;
    inputState.lastButtons = [];
    inputState.lastDir = '';
  }
  inputState.pad = pad;
  $('input-pill').textContent = pad ? ('INPUT · ' + padLabel(pad)) : 'KEYBOARD + MOUSE';
  $('input-pill').classList.toggle('on', !!pad);
}
function held(pad, index) {
  const button = pad && pad.buttons && pad.buttons[index];
  return !!(button && (button.pressed || button.value > 0.5));
}
function edge(pad, index) {
  return held(pad, index) && !inputState.lastButtons[index];
}
function axis(pad, index) {
  return pad && pad.axes && Number.isFinite(Number(pad.axes[index])) ? Number(pad.axes[index]) : 0;
}
function padDirection(pad) {
  const x = axis(pad, 0), y = axis(pad, 1);
  if (Math.abs(x) > 0.55 || Math.abs(y) > 0.55) {
    if (Math.abs(x) > Math.abs(y)) return x > 0 ? 'right' : 'left';
    return y > 0 ? 'down' : 'up';
  }
  if (held(pad, 12)) return 'up';
  if (held(pad, 13)) return 'down';
  if (held(pad, 14)) return 'left';
  if (held(pad, 15)) return 'right';
  return '';
}
function handlePad(pad, now) {
  handleTouchpad(pad, now);
  if (!inputState.touchActive) {
    const rx = axis(pad, 2), ry = axis(pad, 3);
    if (Math.abs(rx) > 0.18 || Math.abs(ry) > 0.18) {
      movePointer(rx * 12, ry * 12);
    }
  }
  const direction = padDirection(pad);
  if (direction) {
    const previous = inputState.lastDir;
    if (direction !== previous || now >= inputState.nextMove) {
      focusMove(direction);
      inputState.lastDir = direction;
      inputState.nextMove = now + (previous === direction ? 160 : 300);
    }
  } else { inputState.lastDir = ''; }
  if (edge(pad, 0)) activateFocused();
  if (edge(pad, 1)) controllerBack();
  if (edge(pad, 2)) {
    if (inputState.osk) backspaceOSK();
    else openOSK(true, isEditable(document.activeElement) ? document.activeElement : $('prompt'));
  }
  if (edge(pad, 3)) { $('prompt').focus(); toast('Composer focused'); }
  if (edge(pad, 4)) selectTab('activity');
  if (edge(pad, 5)) selectTab('tools');
  if (edge(pad, 6)) scrollMessages(-Math.round(window.innerHeight * 0.7));
  if (edge(pad, 7)) scrollMessages(Math.round(window.innerHeight * 0.7));
  if (edge(pad, 8)) toggleHelp();
  if (edge(pad, 9)) toggleOSK(true);
  if (edge(pad, 10)) { inputState.pointerVisible = !inputState.pointerVisible; $('virtual-pointer').classList.toggle('visible', inputState.pointerVisible); }
  if (edge(pad, 11) || edge(pad, 17)) clickPointer();
  if (edge(pad, 16)) toggleHelp();
}
function inputFrame(now) {
  const pads = navigator.getGamepads ? Array.from(navigator.getGamepads()).filter((pad) => pad && pad.connected) : [];
  const pad = pads.length ? pads[0] : null;
  if (pad) {
    setPad(pad);
    handlePad(pad, now);
    inputState.lastButtons = (pad.buttons || []).map((button) => !!button && (button.pressed || button.value > 0.5));
  } else if (inputState.pad) {
    setPad(null);
    inputState.lastButtons = [];
    inputState.lastDir = '';
  }
  requestAnimationFrame(inputFrame);
}

function buildOSK() {
  const grid = $('osk-grid');
  OSK_ROWS.forEach((row) => row.forEach((key) => {
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'osk-key';
    button.dataset.oskKey = key;
    button.textContent = key;
    grid.appendChild(button);
  }));
  const actions = [
    ['shift', 'Shift'], ['backspace', '⌫'], ['space', 'Space'],
    ['enter', 'Enter'], ['close', 'Close']
  ];
  actions.forEach(([action, label]) => {
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'osk-key' + (action === 'space' ? ' space' : '');
    button.dataset.oskAction = action;
    button.textContent = label;
    $('osk-actions').appendChild(button);
  });
}
function refreshOSKLetters() {
  document.querySelectorAll('[data-osk-key]').forEach((button) => {
    const key = button.dataset.oskKey;
    button.textContent = inputState.shift && /^[a-z]$/.test(key) ? key.toUpperCase() : key;
  });
  const shift = document.querySelector('[data-osk-action="shift"]');
  if (shift) { shift.textContent = inputState.shift ? 'SHIFT ON' : 'Shift'; shift.setAttribute('aria-pressed', String(inputState.shift)); }
}
function openOSK(viaController, target) {
  if (!$('input-help').classList.contains('hidden')) toggleHelp(false);
  const field = target && isEditable(target) ? target : $('prompt');
  inputState.oskTarget = field;
  saveCaret(field);
  $('osk-target').textContent = field.id === 'prompt' ? 'message' : (field.id || 'field');
  $('osk').classList.remove('hidden');
  inputState.osk = true;
  inputState.oskViaController = !!viaController;
  if (viaController) {
    setTimeout(() => {
      const first = $('osk-grid').querySelector('button');
      if (first) first.focus({ preventScroll: true });
    }, 0);
  } else {
    field.focus({ preventScroll: true });
  }
}
function closeOSK() {
  const target = inputState.oskTarget;
  $('osk').classList.add('hidden');
  inputState.osk = false;
  inputState.oskTarget = null;
  if (target && isVisible(target)) target.focus({ preventScroll: true });
}
function toggleOSK(viaController) {
  if (inputState.osk) closeOSK();
  else openOSK(!!viaController, isEditable(document.activeElement) ? document.activeElement : $('prompt'));
}
function insertText(text) {
  const target = inputState.oskTarget || (isEditable(document.activeElement) ? document.activeElement : $('prompt'));
  if (!target) return;
  let start = inputState.caret.start, end = inputState.caret.end;
  if (document.activeElement === target && Number.isInteger(target.selectionStart)) {
    start = target.selectionStart; end = target.selectionEnd;
  }
  start = Math.max(0, Math.min(target.value.length, start));
  end = Math.max(start, Math.min(target.value.length, end));
  target.setRangeText(text, start, end, 'end');
  inputState.caret = { start: start + text.length, end: start + text.length };
  target.dispatchEvent(new Event('input', { bubbles: true }));
}
function backspaceOSK() {
  const target = inputState.oskTarget || (isEditable(document.activeElement) ? document.activeElement : $('prompt'));
  if (!target) return;
  let start = inputState.caret.start, end = inputState.caret.end;
  if (document.activeElement === target && Number.isInteger(target.selectionStart)) {
    start = target.selectionStart; end = target.selectionEnd;
  }
  if (start === end && start > 0) start -= 1;
  if (end <= start) return;
  target.setRangeText('', start, end, 'end');
  inputState.caret = { start: start, end: start };
  target.dispatchEvent(new Event('input', { bubbles: true }));
}
function submitPrompt() {
  const target = inputState.oskTarget;
  closeOSK();
  if (target && target.id !== 'prompt' && target.form) { target.form.requestSubmit(); return; }
  $('composer').requestSubmit();
}
function oskAction(action) {
  if (action === 'shift') { inputState.shift = !inputState.shift; refreshOSKLetters(); }
  else if (action === 'backspace') backspaceOSK();
  else if (action === 'space') insertText(' ');
  else if (action === 'enter') {
    if (inputState.shift) insertText('\n');
    else submitPrompt();
  } else if (action === 'close') closeOSK();
}
function toggleHelp(force) {
  const help = $('input-help');
  const show = force === undefined ? help.classList.contains('hidden') : !!force;
  if (show && inputState.osk) closeOSK();
  help.classList.toggle('hidden', !show);
  if (show) { const close = $('help-close'); if (close) close.focus({ preventScroll: true }); }
}

function setMicStatus(message) { $('mic-status').textContent = message || ''; }
function stopDictation() {
  inputState.listening = false;
  inputState.micStarting = false;
  inputState.micRetries = 0;
  if (inputState.recognition) {
    try { inputState.recognition.abort(); } catch (_) {}
    inputState.recognition = null;
  }
  $('mic-button').classList.remove('listening');
  $('mic-button').setAttribute('aria-pressed', 'false');
  setMicStatus('Dictation stopped');
}
async function startDictation() {
  if (inputState.listening) { stopDictation(); return; }
  if (inputState.micStarting) return;
  if (!inputState.oskTarget) saveCaret($('prompt'));
  if (!window.isSecureContext) {
    setMicStatus('HTTPS or localhost required');
    toast('Dictation needs HTTPS or localhost. Open this page through a trusted HTTPS tunnel or localhost.');
    toggleHelp(true);
    return;
  }
  const Recognition = window.SpeechRecognition || window.webkitSpeechRecognition;
  if (!Recognition) {
    setMicStatus('Browser has no speech recognition');
    toast('This browser has no Web Speech recognition. Use its keyboard dictation key or another browser.');
    return;
  }
  inputState.micStarting = true;
  $('mic-button').classList.add('listening');
  $('mic-button').setAttribute('aria-pressed', 'true');
  setMicStatus('Preparing microphone…');
  try {
    if (navigator.mediaDevices && navigator.mediaDevices.getUserMedia) {
      const stream = await navigator.mediaDevices.getUserMedia({ audio: true });
      const devices = navigator.mediaDevices.enumerateDevices
        ? await navigator.mediaDevices.enumerateDevices() : [];
      stream.getTracks().forEach((track) => track.stop());
      const inputs = devices.filter((device) => device.kind === 'audioinput');
      const chosen = inputs.find((device) => /controller|headset|wireless|dualsense|pulse|microphone|mic/i.test(device.label || '')) ||
        inputs.find((device) => device.deviceId === 'default') || inputs[0];
      setMicStatus(chosen && chosen.label ? ('Mic: ' + chosen.label) : 'Mic ready');
    }
    const recognition = new Recognition();
    recognition.lang = navigator.language || 'en-US';
    recognition.continuous = true;
    recognition.interimResults = true;
    recognition.maxAlternatives = 1;
    inputState.recognition = recognition;
    inputState.listening = true;
    inputState.micRetries = 0;
    recognition.onresult = (event) => {
      inputState.micRetries = 0;
      let finalText = '', interimText = '';
      for (let i = event.resultIndex; i < event.results.length; i++) {
        const result = event.results[i];
        const phrase = result[0] ? result[0].transcript : '';
        if (result.isFinal) finalText += phrase; else interimText += phrase;
      }
      if (finalText.trim()) {
        const before = inputState.oskTarget ? inputState.oskTarget.value : $('prompt').value;
        const spacer = before && !/\s$/.test(before) ? ' ' : '';
        insertText(spacer + finalText.trim() + ' ');
      }
      if (interimText.trim()) setMicStatus('Listening: ' + interimText.trim());
    };
    recognition.onerror = (event) => {
      if (event.error === 'not-allowed' || event.error === 'service-not-allowed') {
        stopDictation();
        setMicStatus('Microphone permission denied');
        toast('Microphone permission was denied.');
      } else if (event.error !== 'no-speech' && event.error !== 'aborted') {
        setMicStatus('Dictation: ' + event.error);
      }
    };
    recognition.onend = () => {
      if (!inputState.listening) return;
      if (inputState.micRetries++ < 4) {
        setTimeout(() => {
          if (!inputState.listening || !inputState.recognition) return;
          try { inputState.recognition.start(); } catch (_) { stopDictation(); }
        }, 250);
      } else stopDictation();
    };
    recognition.start();
    setMicStatus('Listening…');
  } catch (error) {
    inputState.micStarting = false;
    inputState.listening = false;
    $('mic-button').classList.remove('listening');
    $('mic-button').setAttribute('aria-pressed', 'false');
    const name = error && error.name ? error.name : '';
    setMicStatus(name === 'NotAllowedError' ? 'Microphone permission denied' : 'Microphone unavailable');
    toast(name === 'NotAllowedError' ? 'Microphone permission was denied.' : 'Could not open the microphone: ' + String(error.message || error));
  } finally { inputState.micStarting = false; }
}

$('osk-grid').addEventListener('click', (event) => {
  const key = event.target.closest('[data-osk-key]');
  if (key) {
    const value = inputState.shift && /^[a-z]$/.test(key.dataset.oskKey) ? key.dataset.oskKey.toUpperCase() : key.dataset.oskKey;
    insertText(value);
    if (inputState.shift) { inputState.shift = false; refreshOSKLetters(); }
  }
});
$('osk-actions').addEventListener('click', (event) => {
  const action = event.target.closest('[data-osk-action]');
  if (action) oskAction(action.dataset.oskAction);
});
$('osk-button').addEventListener('click', () => toggleOSK(false));
$('osk-close').addEventListener('click', closeOSK);
$('mic-button').addEventListener('click', startDictation);
$('input-pill').addEventListener('click', () => toggleHelp());
$('input-help-button').addEventListener('click', () => toggleHelp(true));
$('help-close').addEventListener('click', () => toggleHelp(false));
$('prompt').addEventListener('select', () => saveCaret($('prompt')));
$('prompt').addEventListener('click', () => saveCaret($('prompt')));
$('prompt').addEventListener('input', () => saveCaret($('prompt')));
document.addEventListener('keydown', (event) => {
  const active = document.activeElement;
  if (event.key === 'Escape') {
    if (inputState.osk) { closeOSK(); event.preventDefault(); return; }
    if (!$('input-help').classList.contains('hidden')) { toggleHelp(false); event.preventDefault(); return; }
  }
  if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 'k') {
    event.preventDefault(); $('prompt').focus(); return;
  }
  if (event.key === '?' && !isEditable(active) && !event.ctrlKey && !event.metaKey && !event.altKey) {
    event.preventDefault(); toggleHelp(); return;
  }
  if (inputState.osk && !isEditable(active) && !event.ctrlKey && !event.metaKey && !event.altKey) {
    if (event.key.length === 1) { event.preventDefault(); insertText(event.key); }
    else if (event.key === 'Backspace') { event.preventDefault(); backspaceOSK(); }
    else if (event.key === 'Enter') { event.preventDefault(); oskAction('enter'); }
  }
});
window.addEventListener('gamepadconnected', (event) => {
  const pad = event.gamepad;
  if (pad) {
    setPad(pad);
    handlePad(pad, performance.now());
    inputState.lastButtons = (pad.buttons || []).map((button) => !!button && (button.pressed || button.value > 0.5));
  }
  toast('Controller connected: ' + padLabel(pad));
});
window.addEventListener('gamepaddisconnected', () => {
  setPad(null);
  inputState.lastButtons = [];
  inputState.lastDir = '';
  toast('Controller disconnected');
});
buildOSK();
refreshOSKLetters();
setPad(null);
requestAnimationFrame(inputFrame);

bindChips();
boot();
</script>
</body>
</html>)PS5HUI";
}

}  // namespace ps5h
