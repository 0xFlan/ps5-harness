#include "ps5h/web_ui.h"

namespace ps5h {

const char* web_ui_html() {
  return R"PS5HUI(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="theme-color" content="#0a0c10">
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
.model-pill{cursor:pointer;max-width:230px;overflow:hidden;text-overflow:ellipsis}
.model-pill:hover{border-color:#3d6c9f;color:#dcecff}
.model-current{font-size:11px;line-height:1.45;color:#b9d9ff;border:1px solid #1f3552;border-radius:9px;
  padding:9px 10px;background:#091422;overflow-wrap:anywhere}
.model-form{display:flex;flex-direction:column;gap:9px}
.model-field{display:flex;flex-direction:column;gap:4px;font-size:10px;letter-spacing:.08em;color:var(--dim)}
.model-field input,.model-field select{width:100%;background:#060d17;border:1px solid #284b72;border-radius:8px;
  color:var(--text);padding:8px 9px;font-size:11px;outline:0}
.model-field input:focus,.model-field select:focus{border-color:#3f7fbe}
.model-field.model-toggle{flex-direction:row;align-items:center;gap:8px;letter-spacing:.02em;color:#b9d9ff}
.model-field.model-toggle input{width:auto;margin:0}
.model-grid{display:grid;grid-template-columns:1fr 1fr;gap:8px}
.model-actions{display:flex;gap:7px;margin-top:2px}
.model-apply{border:1px solid #3f8fe8;background:#1d67c5;border-radius:8px;padding:8px 10px;
  color:white;font-size:11px;font-weight:700}
.model-apply:disabled{opacity:.45;cursor:default}
.model-error{min-height:14px;color:var(--red);font-size:10px;line-height:1.4}
.model-note{color:var(--dim);font-size:10px;line-height:1.45;margin:4px 0 0}
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
<style id="ps5-native-theme">
/* ============================================================================
   PS5 SYSTEM-SOFTWARE THEME  (re-skin layer — IDs & behaviour untouched)
   Modelled directly on the PS5 Settings / Game Base / Messages screens:
   one continuous spotlit particle backdrop, large airy thin type, fully
   monochrome, transparent lists divided by hairlines, and the signature
   thin rounded-outline focus box (never a filled accent pill).
   ========================================================================== */
/* Genuine PlayStation system typeface (SST). On a real PS5 "SST" already
   resolves to the installed system font. For the browser preview / other
   devices, drop LICENSED SST web fonts at /fonts/ and these faces pick them
   up automatically; when absent the browser silently falls back to the
   closest humanist sans in --ps-font. SST is Sony/Monotype proprietary and
   is intentionally NOT bundled here. */
@font-face{font-family:"SST";font-weight:300;font-style:normal;font-display:swap;
  src:local("Sony SST Light"),local("SST Light"),url("/fonts/SST-Light.woff2") format("woff2")}
@font-face{font-family:"SST";font-weight:400;font-style:normal;font-display:swap;
  src:local("Sony SST"),local("SST Roman"),local("SST"),url("/fonts/SST-Roman.woff2") format("woff2")}
@font-face{font-family:"SST";font-weight:500;font-style:normal;font-display:swap;
  src:local("Sony SST Medium"),local("SST Medium"),url("/fonts/SST-Medium.woff2") format("woff2")}
:root{
  --ps-ink:#f3f5f8;          /* primary label            */
  --ps-ink-2:#c7ccd6;        /* value / body             */
  --ps-ink-3:#9a9fab;        /* secondary                */
  --ps-ink-4:#6c7079;        /* dim / inactive / kicker  */
  --ps-hair:rgba(255,255,255,.11);
  --ps-hair-soft:rgba(255,255,255,.06);
  --ps-focus-line:rgba(255,255,255,.62);
  --ps-focus-fill:rgba(255,255,255,.045);
  --ps-r:9px;                /* focus-box radius         */
  --ps-font:"Sony SST","SST","SST Light","PlayStation","SF Pro Display",-apple-system,BlinkMacSystemFont,"Segoe UI",Frutiger,"Frutiger Neue","Helvetica Neue",Arial,sans-serif;
}

/* ---- Backdrop: deep neutral black + top-centre stage spotlight + floor --- */
html{background:#060708}
body{
  color:var(--ps-ink);
  font:16px/1.5 var(--ps-font);
  letter-spacing:.006em;
  -webkit-font-smoothing:antialiased;
  background:
    radial-gradient(58% 48% at 50% -8%,rgba(247,244,240,.26) 0%,rgba(150,154,164,.09) 34%,transparent 62%),
    radial-gradient(120% 80% at 50% -16%,rgba(170,180,205,.08) 0%,transparent 58%),
    radial-gradient(90% 60% at 50% 120%,rgba(150,170,205,.06) 0%,transparent 60%),
    linear-gradient(180deg,#0f1114 0%,#090a0d 52%,#050608 100%);
}
/* a faint cool wash + soft side vignette so the eye settles centre-stage */
body::before{
  content:"";position:fixed;inset:0;z-index:0;pointer-events:none;
  background:
    radial-gradient(70% 36% at 50% -2%,rgba(255,255,255,.07),transparent 60%),
    radial-gradient(120% 100% at 50% 40%,transparent 58%,rgba(0,0,0,.4) 100%);
}
#ps5-bg{position:fixed;inset:0;z-index:0;display:block;pointer-events:none}
#app{position:relative;z-index:1;grid-template-columns:300px minmax(0,1fr) 372px;background:transparent}

/* All three columns sit on the ONE shared backdrop — no panels, no fills. */
.sidebar,.inspector,.main{background:transparent;backdrop-filter:none}
.sidebar{border-right:1px solid var(--ps-hair-soft);padding:30px 22px 20px}
.inspector{border-left:1px solid var(--ps-hair-soft)}

/* ---- Brand lockup ------------------------------------------------------ */
.brand{padding:2px 4px 30px;gap:14px;align-items:center}
.brand-mark,.topbar-mark{display:flex;align-items:center;justify-content:center}
.brand-mark{width:34px;height:34px;color:#c7ccd6;border:0;background:none;box-shadow:none}
.brand-mark svg,.topbar-mark svg{width:100%;height:100%;display:block}
.brand-name{font-size:16px;font-weight:400;letter-spacing:.01em;color:#f3f5f8}
.brand-sub{font-size:11px;letter-spacing:.02em;color:var(--ps-ink-4);margin-top:3px;font-weight:300}

/* ---- New session: the light "focused action" pill (PS5 OK / Create) ---- */
.new-session{
  border:0;border-radius:999px;padding:12px 18px;margin-bottom:28px;
  font-size:14px;font-weight:500;letter-spacing:.01em;color:#16181d;
  background:linear-gradient(180deg,#f4f5f7,#d4d7dd);
  box-shadow:0 6px 18px rgba(0,0,0,.35),inset 0 1px 0 rgba(255,255,255,.7);
  transition:filter .15s,transform .12s;
}
.new-session:hover{filter:brightness(1.04)}
.new-session:active{transform:translateY(1px)}

/* ---- Left rail: PS5 category list (plain text, dim/bright, outline focus) */
.nav{gap:2px}
.nav button{
  position:relative;min-height:52px;padding:11px 15px;border:1px solid transparent;border-radius:var(--ps-r);
  color:var(--ps-ink-3);font-size:16px;font-weight:400;letter-spacing:.008em;
  display:flex;align-items:center;gap:15px;background:transparent;
  transition:color .14s,background .14s,border-color .14s;
}
.nav button:hover{color:var(--ps-ink)}
.nav button::after{content:"›";margin-left:auto;font-size:19px;line-height:1;color:var(--ps-ink-4);opacity:.55;transition:opacity .14s,color .14s}
.nav button:hover::after{opacity:.8;color:var(--ps-ink-3)}
.nav button.active::after{color:var(--ps-ink-2);opacity:.9}
.nav button.active{
  color:#fff;background:var(--ps-focus-fill);
  border-color:var(--ps-focus-line);
  box-shadow:0 0 22px rgba(0,0,0,.35);
}
.nav .ico{width:24px;flex:0 0 24px;text-align:center;font-size:16px;color:currentColor;background:none;border:0;opacity:.9}

.section-label{font-size:12px;letter-spacing:.01em;color:var(--ps-ink-4);margin:32px 15px 10px;font-weight:300;text-transform:none}

/* Current-session entry — a quiet list row, not a card. */
.session-item{background:transparent;border:0;border-radius:var(--ps-r);padding:11px 15px}
.session-title{font-size:15px;font-weight:400;color:var(--ps-ink)}
.session-meta{font-size:11px;color:var(--ps-ink-4);margin-top:5px}

.sidebar-foot{border-color:var(--ps-hair-soft);color:var(--ps-ink-3);font-size:11px;letter-spacing:.02em;padding-top:18px}
.dot{width:7px;height:7px;background:#4cd08a;box-shadow:0 0 10px rgba(76,208,138,.8)}

/* ---- Top bar ----------------------------------------------------------- */
.topbar{
  height:auto;min-height:84px;padding:20px 40px;border-bottom:1px solid var(--ps-hair-soft);
  background:transparent;backdrop-filter:none;
}
.topbar-head{display:flex;align-items:center;gap:14px;min-width:0}
.crumb{display:none}
h1{font-size:28px;font-weight:200;letter-spacing:.01em;margin-top:5px;color:#f4f6f9}
.status{gap:17px;font-size:12px;flex-wrap:wrap;justify-content:flex-end;max-width:54%;align-items:center}
.pill{
  border:0;background:transparent;border-radius:0;padding:0;
  font-size:12.5px;letter-spacing:.01em;color:var(--ps-ink-4);white-space:nowrap;font-weight:300;
}
.pill.full{color:var(--ps-ink-3)}
.input-pill{color:var(--ps-ink-4)}
.input-pill:hover{color:#fff}
.input-pill.on{color:#9fd9b8}
.clock{font-size:16px;color:var(--ps-ink-2);font-weight:300;letter-spacing:.03em;font-variant-numeric:tabular-nums}
.ctx{width:72px;height:3px;background:rgba(255,255,255,.14);border-radius:99px;overflow:hidden}
.ctx i{background:linear-gradient(90deg,rgba(255,255,255,.55),#fff);box-shadow:none}

/* ---- Message area ------------------------------------------------------ */
.messages{padding:42px clamp(28px,6vw,92px) 24px;scrollbar-width:thin;scrollbar-color:rgba(255,255,255,.2) transparent;overscroll-behavior:contain}
.messages::-webkit-scrollbar{width:10px}
.messages::-webkit-scrollbar-thumb{background:rgba(255,255,255,.16);border-radius:9px;border:3px solid transparent;background-clip:padding-box}

/* Empty state — a clean settings landing: one title, one line, a list. */
.hero{max-width:960px;margin:6vh 0 0;padding:0;text-align:left}
.hero-status{display:none}
.hero-system{display:none}
.hero-mark{display:none}
.hero h2{font-size:clamp(40px,5vw,62px);font-weight:150;letter-spacing:.012em;margin:0 0 16px;color:#f6f7fa;text-align:left;line-height:1.04}
.hero p{font-size:17px;line-height:1.65;color:var(--ps-ink-3);max-width:680px;margin:0;text-align:left;font-weight:300}

/* Prompt shortcuts rendered as PS5 setting rows (label + description + chevron). */
.chips{display:flex;flex-direction:column;gap:0;max-width:820px;margin-top:40px;border-top:1px solid var(--ps-hair)}
.chip{
  position:relative;display:flex;flex-direction:column;align-items:flex-start;text-align:left;gap:5px;
  width:100%;padding:21px 46px 21px 16px;border:1px solid transparent;border-radius:var(--ps-r);
  background:transparent;border-bottom:1px solid var(--ps-hair);color:var(--ps-ink);
  transition:background .14s,border-color .14s;
}
.chip:hover,.chip:focus-visible{background:var(--ps-focus-fill);border-color:var(--ps-focus-line);outline:none}
.chip::after{content:"›";position:absolute;right:18px;top:50%;transform:translateY(-50%);font-size:23px;line-height:1;color:var(--ps-ink-4);opacity:.5}
.chip:hover::after{opacity:.85;color:var(--ps-ink-2)}
.chip-kicker{display:none}
.chip-title{font-size:19px;font-weight:300;color:#f1f4f8;line-height:1.25}
.chip-desc{font-size:13.5px;color:var(--ps-ink-3);line-height:1.45;font-weight:300}

/* Chat transcript — PS5 Messages: thin outlined pills, transparent fill. */
.msg{width:auto;max-width:min(74%,680px);margin:0 0 16px;padding:0;display:flex;align-items:flex-end;gap:11px;background:none;border:0;box-shadow:none;border-radius:0}
.msg.user{max-width:min(68%,600px);margin-left:auto;flex-direction:row-reverse}
.msg .avatar{
  width:32px;height:32px;flex:0 0 32px;margin:0 0 3px;border-radius:50%;font-size:9px;letter-spacing:.02em;
  background:transparent;border:1px solid rgba(255,255,255,.28);color:var(--ps-ink-2);
}
.msg.user .avatar{background:transparent;border-color:rgba(255,255,255,.5);color:#fff}
.msg .body{
  max-width:100%;min-width:0;padding:12px 17px 13px;border-radius:20px;
  background:rgba(255,255,255,.028);border:1px solid var(--ps-hair);box-shadow:none;
}
.msg.user .body{border-radius:20px;background:rgba(255,255,255,.07);border-color:rgba(255,255,255,.3)}
.msg .who{font-size:11px;letter-spacing:.01em;color:var(--ps-ink-4);margin:0 0 5px;font-weight:300}
.msg.user .who{color:var(--ps-ink-3)}
.msg .text{font-size:15px;line-height:1.6;color:var(--ps-ink);overflow-wrap:anywhere;font-weight:300}
.msg.user .text{color:#fff}
.msg .text pre{margin:9px 0 2px;padding:13px 15px;border-radius:12px;background:rgba(0,0,0,.4);border:1px solid var(--ps-hair)}
.msg .text code{font-family:ui-monospace,SFMono-Regular,Menlo,monospace}
.thinking{color:var(--ps-ink-3)}
.thinking i{background:#fff;opacity:.8}

/* ---- Composer — minimal: hairline field, icon buttons, light send ------ */
.composer-wrap{padding:0 clamp(28px,6vw,92px) 30px;background:linear-gradient(180deg,transparent,rgba(6,7,8,.75) 44%)}
.composer{
  position:relative;display:flex;align-items:flex-end;gap:10px;min-height:56px;max-width:none;
  border:0;border-bottom:1px solid var(--ps-hair);border-radius:0;padding:8px 2px 12px;
  background:transparent;box-shadow:none;
}
.composer:focus-within{border-bottom-color:var(--ps-focus-line);box-shadow:0 1px 0 var(--ps-focus-line)}
.composer textarea{flex:1 1 auto;width:auto;min-width:0;min-height:26px;max-height:120px;padding:8px 0;font-size:17px;line-height:1.45;color:#f4f6fa;background:transparent;border:0;resize:none;font-weight:300}
.composer textarea::placeholder{color:var(--ps-ink-4)}
.composer-row{display:flex;align-items:center;flex:0 0 auto;width:auto;gap:4px;padding:0 0 2px;margin:0}
.composer-hint{display:none}
.composer-tools{display:flex;align-items:center;gap:6px;flex-wrap:nowrap}
.icon-btn{
  width:38px;height:38px;min-width:38px;border-radius:50%;padding:0;font-size:15px;
  border:1px solid var(--ps-hair);background:transparent;color:var(--ps-ink-2);
  transition:border-color .14s,color .14s,background .14s;
}
.icon-btn:hover,.icon-btn:focus-visible{border-color:var(--ps-focus-line);color:#fff;background:var(--ps-focus-fill)}
.icon-btn svg{width:17px;height:17px;display:block}
.icon-btn.listening{border-color:#ff9f6b;color:#ffddcb;background:rgba(60,28,16,.5);animation:micPulse 1.1s ease-in-out infinite}
.mic-status{position:absolute;left:0;bottom:calc(100% + 6px);max-width:calc(100% - 90px);padding:4px 10px;border-radius:999px;background:rgba(255,255,255,.06);border:1px solid var(--ps-hair);font-size:10.5px;color:var(--ps-ink-2);white-space:nowrap;overflow:hidden;text-overflow:ellipsis;pointer-events:none}
.mic-status:empty{display:none}
.composer .send{
  position:relative;width:42px;height:42px;min-width:42px;padding:0;border:0;border-radius:50%;
  font-size:0;text-indent:-999px;overflow:hidden;display:block;
  background:linear-gradient(180deg,#f4f5f7,#d4d7dd);box-shadow:0 5px 15px rgba(0,0,0,.4),inset 0 1px 0 rgba(255,255,255,.7);
}
.composer .send::after{content:"↑";position:absolute;inset:0;display:grid;place-items:center;text-indent:0;color:#16181d;font-size:19px;font-weight:700}
.composer .send:hover{filter:brightness(1.04)}
.composer .send:disabled{opacity:.4;filter:none}

/* ---- Inspector --------------------------------------------------------- */
.tabs{padding:24px 24px 0;gap:4px;border-bottom:1px solid var(--ps-hair)}
.tabs button{font-size:14px;letter-spacing:.01em;padding:10px 14px 15px;color:var(--ps-ink-4);border-bottom:2px solid transparent;transition:color .14s,border-color .14s;text-transform:none;font-weight:300}
.tabs button:hover{color:var(--ps-ink-2)}
.tabs button.active{color:#fff;border-color:#fff}
.panel{padding:22px 24px 12px}
.panel h3{font-size:12px;letter-spacing:.02em;color:var(--ps-ink-4);font-weight:300;margin-bottom:16px;text-transform:none}
.activity{gap:0}
.event{border-left:1px solid var(--ps-hair);color:var(--ps-ink-3);font-size:13px;line-height:1.6;padding:7px 0 7px 13px;font-weight:300;font-family:var(--ps-font)}
.event:last-child{border-color:#fff;color:var(--ps-ink)}
.tool{border:1px solid transparent;border-bottom:1px solid var(--ps-hair);border-radius:var(--ps-r);padding:14px 13px;background:transparent;margin-bottom:0;transition:background .14s,border-color .14s}
.tool:hover{border-color:var(--ps-focus-line);background:var(--ps-focus-fill)}
.tool-name{font-size:14px;color:var(--ps-ink);font-weight:400;font-family:var(--ps-font)}
.tool-desc{font-size:12px;color:var(--ps-ink-3);margin-top:4px;font-weight:300}

/* ---- Model and provider: PS5 setting rows, not a second theme ---------- */
.model-pill{max-width:300px}
.model-current{
  margin:0 0 18px;padding:0 0 16px;border:0;border-bottom:1px solid var(--ps-hair);
  border-radius:0;background:transparent;color:var(--ps-ink-2);
  font-size:13px;line-height:1.6;font-weight:300;overflow-wrap:anywhere;
}
.model-form{gap:0}
.model-field{
  display:block;padding:14px 0;border-bottom:1px solid var(--ps-hair);
  font-size:11px;letter-spacing:.04em;color:var(--ps-ink-4);font-weight:300;
}
.model-field input,.model-field select{
  width:100%;margin-top:8px;padding:10px 12px;border:1px solid var(--ps-hair);
  border-radius:var(--ps-r);background:rgba(255,255,255,.035);color:var(--ps-ink);
  font:300 13px/1.4 var(--ps-font);outline:0;transition:border-color .14s,background .14s;
}
.model-field input::placeholder{color:var(--ps-ink-4)}
.model-field input:focus,.model-field select:focus{
  border-color:var(--ps-focus-line);background:var(--ps-focus-fill);
}
.model-field.model-toggle{display:flex;align-items:center;gap:10px;padding:14px 0;color:var(--ps-ink-2)}
.model-field.model-toggle input{width:auto;margin:0}
.model-field select option{background:#111419;color:var(--ps-ink)}
.model-grid{gap:10px}
.model-actions{margin-top:18px}
.model-apply{
  width:100%;min-height:46px;border:1px solid var(--ps-hair);border-radius:999px;
  background:transparent;color:var(--ps-ink);font-size:14px;font-weight:400;
  letter-spacing:.01em;transition:background .14s,border-color .14s,transform .12s;
}
.model-apply:hover{border-color:var(--ps-focus-line);background:var(--ps-focus-fill)}
.model-apply:active{transform:translateY(1px)}
.model-apply:disabled{opacity:.4;cursor:default;transform:none}
.model-error{color:#ff9aa8;font-size:11px}
.model-note{color:var(--ps-ink-4);font-size:12px;line-height:1.6;font-weight:300;margin:14px 0 0}
.stats{border-color:var(--ps-hair);padding:18px 24px 20px;background:transparent}
.stat{font-size:13px;padding:7px 0;color:var(--ps-ink-3);border-bottom:1px solid var(--ps-hair-soft);font-weight:300}
.stat:last-of-type{border-bottom:0}
.stat b{color:#eef2f7;font-weight:400}
.safety{margin-top:16px;border:0;border-top:1px solid var(--ps-hair);background:transparent;border-radius:0;color:var(--ps-ink-4);font-size:11px;line-height:1.55;padding:14px 0 0;font-weight:300}

/* ---- Overlays ---------------------------------------------------------- */
#login{background:rgba(6,7,9,.78);backdrop-filter:blur(26px)}
.login-card{background:rgba(22,25,30,.72);border:1px solid var(--ps-hair);border-radius:20px;box-shadow:0 40px 120px rgba(0,0,0,.7),inset 0 1px 0 rgba(255,255,255,.07);padding:32px;backdrop-filter:blur(20px)}
.login-card h2{font-size:26px;font-weight:200}
.login-card p{color:var(--ps-ink-3);font-size:13px;font-weight:300}
.login-card input{border:0;border-bottom:1px solid var(--ps-hair);border-radius:0;background:transparent;color:#fff;padding:12px 2px;font-size:16px;font-weight:300}
.login-card input:focus{outline:0;border-bottom-color:var(--ps-focus-line)}
.login-card button{border:0;border-radius:999px;background:linear-gradient(180deg,#f4f5f7,#d4d7dd);color:#16181d;box-shadow:0 6px 18px rgba(0,0,0,.4);font-weight:500;padding:13px;margin-top:8px}
.login-error{color:#ff9aa8;font-weight:300}

.osk{background:rgba(22,25,30,.86);border:1px solid var(--ps-hair);border-radius:20px;padding:15px;box-shadow:0 30px 100px rgba(0,0,0,.8),inset 0 1px 0 rgba(255,255,255,.06);backdrop-filter:blur(22px)}
.osk-head{color:var(--ps-ink-3);font-size:11px}
.osk-head b{color:#eef3fa;font-weight:400}
.osk-close{color:var(--ps-ink-3)}
.osk-key{border:1px solid var(--ps-hair);border-radius:10px;background:transparent;color:var(--ps-ink);min-height:40px;transition:background .1s,border-color .1s}
.osk-key:hover,.osk-key:focus-visible{border-color:var(--ps-focus-line);background:var(--ps-focus-fill)}

.input-help{background:rgba(5,6,8,.72);backdrop-filter:blur(22px)}
.help-card{background:rgba(22,25,30,.82);border:1px solid var(--ps-hair);border-radius:20px;box-shadow:0 40px 120px rgba(0,0,0,.7);backdrop-filter:blur(20px)}
.help-card h2{font-size:26px;font-weight:200}
.help-card p{color:var(--ps-ink-3);font-size:13px;font-weight:300}
.help-card dt{color:#e4ebf4;font-weight:400}
.help-card dd{color:var(--ps-ink-3);font-weight:300}
.help-card .help-note{border-color:#fff;color:var(--ps-ink-2)}
.help-head{display:flex;align-items:flex-start;justify-content:space-between;gap:16px}
.help-head h2{flex:1;min-width:0}
.help-head #help-close{flex:0 0 auto;margin:0;padding:9px 15px;font-size:12px;border:1px solid var(--ps-hair);background:transparent;border-radius:999px;color:var(--ps-ink)}
.help-card>button{border:0;border-radius:999px;background:linear-gradient(180deg,#f4f5f7,#d4d7dd);color:#16181d;font-weight:500}

#toast{background:rgba(22,25,30,.9);border:1px solid var(--ps-hair);border-radius:12px;box-shadow:0 14px 40px rgba(0,0,0,.6);color:#eef3fa;backdrop-filter:blur(16px)}
#virtual-pointer{border-color:#fff;background:rgba(255,255,255,.3);box-shadow:0 0 0 3px rgba(5,6,8,.85),0 0 20px rgba(255,255,255,.7)}

:focus-visible{outline:2px solid rgba(255,255,255,.75);outline-offset:3px}

.topbar-mark{display:none;width:30px;height:30px;flex:0 0 30px;color:#c7ccd6;background:none;border:0}

/* ============================================================================
   HOME SCREEN detail — a PS5 settings detail page (transparent rows).
   ========================================================================== */
.home-panel{background:transparent;color:var(--ps-ink);padding:0 0 22px}
.home-tab .stats{display:none}
.home-shell{padding:24px 24px 0}
.home-kicker{display:none}
.panel h3.home-title{font-size:30px;font-weight:200;line-height:1.14;letter-spacing:.01em;color:#f4f6f9;margin:0 0 22px;text-transform:none}
.home-card{display:flex;align-items:center;gap:16px;padding:0 0 20px;border:0;border-bottom:1px solid var(--ps-hair);border-radius:0;background:transparent}
.home-icon{width:78px;height:78px;flex:0 0 78px;border-radius:18px;object-fit:cover;background:rgba(255,255,255,.04);border:1px solid var(--ps-hair)}
.home-identity{min-width:0}
.home-identity b{display:block;color:#f3f5f8;font-size:18px;font-weight:400;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.home-identity span{display:block;margin-top:5px;color:var(--ps-ink-3);font-size:12px;font-weight:300;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.home-badge{display:inline-flex;align-items:center;width:max-content;margin-top:11px;border-radius:999px;padding:4px 12px;font-size:10.5px;font-weight:400;letter-spacing:.04em;border:1px solid var(--ps-hair);color:var(--ps-ink-2);background:transparent}
.home-badge.installed{border-color:rgba(118,214,166,.5);color:#bfe9d2}
.home-detail{margin:18px 0 0;color:var(--ps-ink-3);font-size:13px;line-height:1.6;font-weight:300}
.home-rows{margin-top:6px;border-top:0}
.home-row{padding:16px 14px;border-bottom:1px solid var(--ps-hair);border-radius:var(--ps-r);border-top:1px solid transparent;border-left:1px solid transparent;border-right:1px solid transparent;transition:background .14s,border-color .14s;position:relative}
.home-row:hover{background:var(--ps-focus-fill);border-color:var(--ps-focus-line)}
.home-row-head{display:flex;justify-content:space-between;align-items:flex-start;gap:16px}
.home-row-label{color:var(--ps-ink-2);font-size:15px;font-weight:300}
.home-row strong{color:#f3f5f8;font-size:15px;font-weight:300;text-align:right;min-width:0;overflow-wrap:anywhere}
.home-row p{margin:6px 0 0;color:var(--ps-ink-4);font-size:12px;line-height:1.5;font-weight:300;overflow-wrap:anywhere}
.home-row code{display:block;color:#eef3fa;background:rgba(0,0,0,.35);border:1px solid var(--ps-hair);border-radius:8px;padding:7px 10px;margin-top:8px;font:11px/1.4 ui-monospace,SFMono-Regular,Menlo,monospace;overflow-wrap:anywhere}
.home-actions{display:flex;flex-direction:column;gap:11px;padding:24px 24px 0}
.home-actions button{width:100%;min-height:50px;border-radius:999px;padding:13px 16px;font-size:14px;font-weight:500;letter-spacing:.01em;transition:filter .14s,background .14s,border-color .14s,transform .12s}
.home-actions button:active{transform:translateY(1px)}
.home-panel button:focus-visible,.home-panel a:focus-visible{outline:2px solid rgba(255,255,255,.75);outline-offset:3px}
.home-primary{background:linear-gradient(180deg,#f4f5f7,#d4d7dd);border:0;color:#16181d;box-shadow:0 6px 18px rgba(0,0,0,.4),inset 0 1px 0 rgba(255,255,255,.7)}
.home-primary:hover{filter:brightness(1.04)}
.home-secondary{background:transparent;border:1px solid var(--ps-hair);color:var(--ps-ink)}
.home-secondary:hover{border-color:var(--ps-focus-line)}
.home-danger{background:transparent;border:1px solid var(--ps-hair);color:var(--ps-ink-2)}
.home-danger:hover{border-color:rgba(255,154,168,.6);color:#ff9aa8}
.home-panel button:disabled{opacity:.4;cursor:default}
.home-confirm{margin:18px 24px 0;padding:17px;border:1px solid var(--ps-hair);border-radius:16px;background:rgba(255,255,255,.03)}
.home-confirm.hidden{display:none}
.home-confirm b{display:block;font-size:14px;font-weight:400;margin-bottom:6px}
.home-confirm p{margin:0 0 14px;color:var(--ps-ink-3);font-size:12px;line-height:1.5;font-weight:300}
.home-confirm-actions{display:flex;gap:11px}
.home-confirm-actions button{flex:1;min-height:46px;border-radius:999px;font-size:13px;font-weight:500}

/* ---- Reconnect banner --------------------------------------------------- */
#reconnect{position:fixed;top:16px;left:50%;transform:translateX(-50%);z-index:45;width:min(720px,calc(100vw - 24px));padding:16px 18px;border:1px solid rgba(255,159,107,.45);border-radius:16px;background:rgba(26,19,15,.9);box-shadow:0 24px 80px rgba(0,0,0,.6);color:#f3f5f8;backdrop-filter:blur(18px)}
#reconnect.hidden{display:none}
.reconnect-head{display:flex;align-items:center;gap:9px;font-size:14px}
.reconnect-head i{width:7px;height:7px;border-radius:50%;background:#ff9f6b;box-shadow:0 0 12px #ff9f6b}
.reconnect-head span{margin-left:auto;color:var(--ps-ink-3);font:10px ui-monospace,SFMono-Regular,Menlo,monospace}
#reconnect p{margin:7px 0 0;color:#d6c6bc;font-size:12px;line-height:1.5;font-weight:300}
.reconnect-form{display:flex;gap:9px;margin-top:13px;flex-wrap:wrap}
.reconnect-form input{flex:1 1 260px;min-width:0;border:1px solid rgba(255,159,107,.3);border-radius:10px;background:rgba(0,0,0,.3);color:#fff;padding:11px 12px;font:12px ui-monospace,SFMono-Regular,Menlo,monospace}
.reconnect-form button{border:1px solid rgba(255,159,107,.5);border-radius:999px;padding:11px 15px;background:transparent;color:#ffe8dc;font-size:12px;font-weight:500}
.reconnect-form button:hover{border-color:#ffd0b8}
.reconnect-form button:disabled{opacity:.5;cursor:default}
.reconnect-help{margin-top:9px!important;color:#9a897e!important;font-size:11px!important;font-weight:300!important}

/* ============================================================================
   Responsive
   ========================================================================== */
@media (max-width:1240px){
  #app{grid-template-columns:272px minmax(0,1fr)}
  .inspector{display:none;position:fixed;right:0;top:0;bottom:0;width:min(380px,100vw);z-index:14;background:rgba(10,11,14,.9);backdrop-filter:blur(26px);box-shadow:-24px 0 70px rgba(0,0,0,.6);padding-bottom:70px;overflow-y:auto}
  .inspector.mobile-open{display:flex}
  .mobile-nav{display:flex;position:fixed;left:50%;bottom:max(14px,env(safe-area-inset-bottom));transform:translateX(-50%);z-index:15}
  .messages{padding-bottom:104px}.composer-wrap{padding-bottom:92px}.status{max-width:52%}
}
.mobile-nav{background:rgba(22,25,30,.88);border:1px solid var(--ps-hair);border-radius:999px;padding:5px;box-shadow:0 18px 50px rgba(0,0,0,.6);backdrop-filter:blur(22px)}
.mobile-nav button{font-size:12px;padding:10px 17px;border-radius:999px;color:var(--ps-ink-3)}
.mobile-nav button.active{background:var(--ps-focus-fill);box-shadow:inset 0 0 0 1px var(--ps-focus-line);color:#fff}
@media (min-width:1241px){.mobile-nav{display:none}}
@media (max-width:900px){
  #app{grid-template-columns:1fr}.sidebar{display:none}
  .topbar{min-height:76px;padding:16px 20px}.status{max-width:none;justify-content:flex-end}
  .messages{padding:26px 18px 104px}.composer-wrap{padding:0 16px 92px}
  .topbar-head{flex:1;min-width:0}.topbar-mark{display:flex}
  .status{display:flex;flex:1;justify-content:flex-end;min-width:0}.status .pill{display:inline-flex}
  .hero{margin-top:3vh}.hero h2{font-size:40px}.hero p{font-size:15px}
  .inspector{top:0;left:auto;right:0;width:min(380px,100vw)}.status .pill#model,.status .ctx{display:none}
  .composer-tools{gap:7px}.mic-status{max-width:130px}
  .osk{left:12px;right:12px;bottom:calc(92px + env(safe-area-inset-bottom));transform:none;width:auto;max-height:calc(100dvh - 190px);overflow:auto;padding:11px}
  .osk-grid{grid-template-columns:repeat(7,minmax(0,1fr));gap:5px}.osk-key{min-height:38px;font-size:12px}
  .osk-actions{grid-template-columns:repeat(3,minmax(0,1fr))}
  .help-card{padding:22px}.help-card dl{grid-template-columns:1fr;gap:2px}.help-card dd{margin-bottom:9px}
}
@media (max-width:540px){
  .topbar{padding:13px 15px;min-height:70px;gap:9px}.crumb{font-size:9px}h1{font-size:22px}
  .status{gap:6px}.status .pill{font-size:8.5px;padding:6px 9px}.status .pill#model,.status .ctx,.status #input-pill{display:none}
  .topbar-mark{width:34px;height:34px;flex-basis:34px;font-size:11px}.hero-status{font-size:9px;gap:9px}.hero-system{display:none}
  .hero h2{font-size:33px}.hero p{font-size:14px}.chips{margin-top:22px}.chip{padding:16px 12px}
  .chip-title{font-size:17px}
  .messages{padding:18px 14px 104px}.msg{max-width:88%;gap:8px}.msg.user{max-width:84%}
  .msg .avatar{width:28px;height:28px;flex-basis:28px;font-size:8px}
  .msg .text{font-size:14px;line-height:1.56}
  .composer textarea{font-size:16px}
  .composer-tools{gap:5px}.icon-btn{width:36px;height:36px;min-width:36px;font-size:14px}.composer .send{width:40px;height:40px;min-width:40px}
  .mic-status{max-width:118px;font-size:10px}
  .home-shell{padding:18px 15px 0}.home-title{font-size:26px}.home-icon{width:68px;height:68px;flex-basis:68px}
  .home-card{gap:13px;padding:0 0 16px}.home-actions{padding:18px 15px 0;gap:10px}
  .home-actions button{min-height:46px}.home-confirm{margin:14px 15px 0}
  .mobile-nav button{font-size:11px;padding:9px 14px}
}
@media (max-height:640px) and (min-width:901px){
  .messages{padding-top:22px}.hero{margin-top:2vh}.hero-mark{width:56px;height:56px}.hero h2{font-size:38px}.hero p{display:none}
}
</style>
</head>
<body>
<canvas id="ps5-bg" aria-hidden="true"></canvas>
<div id="reconnect" class="hidden" role="status" aria-live="polite">
  <div class="reconnect-head"><i></i><strong>Console connection lost</strong><span id="reconnect-last"></span></div>
  <p id="reconnect-detail">Retrying this address automatically…</p>
  <div class="reconnect-form">
    <input id="reconnect-url" type="text" spellcheck="false" autocomplete="off" aria-label="Console address">
    <button id="reconnect-now" type="button">Retry now</button>
    <button id="reconnect-use" type="button">Use address</button>
  </div>
  <p class="reconnect-help">If the console went to rest mode, wake it and retry; a surviving harness rebinds automatically. If it was restarted or the jailbreak was lost, re-run the jailbreak and launch the harness payload again, then retry. Use the address shown by discovery if the IP or port changed.</p>
</div>
<div id="app">
  <aside class="sidebar">
    <div class="brand">
      <div class="brand-mark" aria-hidden="true"><svg viewBox="0 0 48 48" fill="none" stroke="currentColor" stroke-width="2.1" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M24 6 L29 15 L19 15 Z"/><circle cx="38" cy="24" r="5"/><rect x="5" y="19" width="10" height="10" rx="1.5"/><path d="M20.5 34 L27.5 41 M27.5 34 L20.5 41"/></svg></div>
      <div><div class="brand-name">PS5 Harness</div><div class="brand-sub">System software</div></div>
    </div>
    <button class="new-session" id="new-session">+  New session</button>
    <nav class="nav">
      <button class="active"><span class="ico">◈</span>Workspace</button>
      <button id="show-activity"><span class="ico">⌁</span>Activity</button>
      <button id="show-tools"><span class="ico">⌘</span>Tools</button>
      <button id="show-model"><span class="ico">◉</span>Model</button>
      <button id="show-home"><span class="ico">▣</span>Home screen</button>
    </nav>
    <div class="section-label">Current session</div>
    <div class="session-item">
      <div class="session-title" id="session-title">Console workspace</div>
      <div class="session-meta" id="session-meta">new conversation</div>
    </div>
    <div class="sidebar-foot"><span class="dot" id="dot"></span><span id="conn">Connecting</span></div>
  </aside>

  <main class="main">
    <header class="topbar" id="topbar">
      <div class="topbar-head">
        <div class="topbar-mark" aria-hidden="true"><svg viewBox="0 0 48 48" fill="none" stroke="currentColor" stroke-width="2.1" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M24 6 L29 15 L19 15 Z"/><circle cx="38" cy="24" r="5"/><rect x="5" y="19" width="10" height="10" rx="1.5"/><path d="M20.5 34 L27.5 41 M27.5 34 L20.5 41"/></svg></div>
        <div><div class="crumb">PLAYSTATION 5 / SYSTEM</div><h1>Console workspace</h1></div>
      </div>
      <div class="status">
        <span class="pill full" id="mode">Full access</span>
        <button class="pill input-pill" id="input-pill" type="button" title="Keyboard, controller and voice input help">Keyboard + mouse</button>
        <button class="pill model-pill" id="model" type="button" title="Choose model and provider">model</button>
        <span class="ctx"><i id="ctxbar"></i></span>
        <span class="clock" id="clock"></span>
      </div>
    </header>
    <section class="messages" id="messages">
      <div class="hero" id="hero">
        <div class="hero-status"><i class="ready-dot"></i><span>CONSOLE READY</span><span class="hero-system">PLAYSTATION 5 / WORKSPACE</span></div>
        <div class="hero-mark" aria-hidden="true">PS<span>5</span></div>
        <h2>Console workspace</h2>
        <p>Your console is online. Start a task, inspect the system, or continue the saved trace. Everything runs here, with the tool boundaries you configured.</p>
        <div class="chips">
          <button class="chip" data-prompt="Map the PS5 architecture and filesystem layout. Start with env_info, then inspect the most important mounts and binaries."><span class="chip-kicker">SYSTEM</span><span class="chip-title">Map the system</span><span class="chip-desc">Architecture, mounts and binaries</span></button>
          <button class="chip" data-prompt="Find the most interesting binaries under /data and identify their formats, imports and strings."><span class="chip-kicker">INSPECT</span><span class="chip-title">Inspect binaries</span><span class="chip-desc">Formats, imports and strings</span></button>
          <button class="chip" data-prompt="Write a new local tool or plugin for this harness, then explain how to load it."><span class="chip-kicker">EXTEND</span><span class="chip-title">Build a tool</span><span class="chip-desc">Add a local capability</span></button>
          <button class="chip" data-prompt="Plan a long research mission over the console, with explicit evidence and stop conditions."><span class="chip-kicker">MISSION</span><span class="chip-title">Plan a mission</span><span class="chip-desc">Evidence and stop conditions</span></button>
        </div>
      </div>
    </section>
    <div class="composer-wrap">
      <nav class="mobile-nav" aria-label="Workspace panels">
        <button data-mobile="chat" class="active">Console</button>
        <button data-mobile="activity">Activity</button>
        <button data-mobile="tools">Tools</button>
        <button data-mobile="model">Model</button>
        <button data-mobile="home">Home</button>
      </nav>
      <form class="composer" id="composer">
        <textarea id="prompt" rows="1" placeholder="Send a message…"></textarea>
        <div class="composer-row">
          <span class="composer-hint">ENTER sends · SHIFT+ENTER adds a line · CTRL/⌘K focuses</span>
          <div class="composer-tools">
            <span class="mic-status" id="mic-status" aria-live="polite"></span>
            <button class="icon-btn" id="osk-button" type="button" title="Open the on-screen keyboard" aria-label="Open the on-screen keyboard"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><rect x="2.5" y="6" width="19" height="12" rx="2.5"/><path d="M6 10h0M10 10h0M14 10h0M18 10h0M8 14h8"/></svg></button>
            <button class="icon-btn" id="mic-button" type="button" title="Dictate with the browser microphone" aria-label="Dictate with the browser microphone" aria-pressed="false"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><rect x="9" y="2.5" width="6" height="11" rx="3"/><path d="M5.5 11.5a6.5 6.5 0 0 0 13 0"/><path d="M12 18v3"/><path d="M8.5 21h7"/></svg></button>
            <button class="icon-btn" id="input-help-button" type="button" title="Input help" aria-label="Input help">?</button>
            <button class="send" id="send" type="submit" aria-label="Send message" title="Send message">Send</button>
          </div>
        </div>
      </form>
    </div>
  </main>

  <aside class="inspector" id="inspector">
    <div class="tabs">
      <button class="active" data-tab="activity">Activity</button>
      <button data-tab="tools">Tools</button>
      <button data-tab="model">Model</button>
      <button data-tab="home">Home screen</button>
    </div>
    <div class="panel active" id="panel-activity">
      <h3>Activity</h3>
      <div class="activity" id="activity"><div class="event">Waiting for a turn…</div></div>
    </div>
    <div class="panel" id="panel-tools">
      <h3>Tools</h3>
      <div id="tools"><div class="event">Loading…</div></div>
    </div>
    <div class="panel" id="panel-model">
      <h3>Model &amp; provider</h3>
      <div class="model-current" id="model-current">Loading the current provider…</div>
      <form class="model-form" id="model-form">
        <label class="model-field">Endpoint preset
          <select id="model-preset" aria-label="Endpoint preset"></select>
        </label>
        <label class="model-field">Base URL
          <input id="model-base-url" type="text" spellcheck="false" autocomplete="off" placeholder="https://api.example.com/v1">
        </label>
        <label class="model-field model-toggle"><input id="model-local" type="checkbox"> Run on this console (built in, no key)</label>
        <label class="model-field">Local GGUF path
          <input id="model-local-path" type="text" spellcheck="false" autocomplete="off" placeholder="/app0/models/model.gguf">
        </label>
        <label class="model-field">Model ID
          <input id="model-name" type="text" spellcheck="false" autocomplete="off" list="model-suggestions" placeholder="model-id">
        </label>
        <datalist id="model-suggestions"></datalist>
        <label class="model-field">Key environment
          <input id="model-key-env" type="text" spellcheck="false" autocomplete="off" placeholder="PROVIDER_API_KEY">
        </label>
        <label class="model-field">Key file on console
          <input id="model-key-file" type="text" spellcheck="false" autocomplete="off" placeholder="/data/ps5h-secrets/api-key">
        </label>
        <div class="model-grid">
          <label class="model-field">Temperature
            <input id="model-temperature" type="number" min="0" max="2" step="0.1" inputmode="decimal">
          </label>
          <label class="model-field">Max tokens
            <input id="model-max-tokens" type="number" min="0" max="32768" step="1" inputmode="numeric">
          </label>
        </div>
        <div class="model-actions">
          <button class="model-apply" id="model-apply" type="submit">Apply to next turn</button>
        </div>
        <div class="model-error" id="model-error" aria-live="polite"></div>
        <p class="model-note">Choose a preset or enter any OpenAI-compatible endpoint. The key is read from the named environment variable or console key file; its contents are never returned to this page. The on-console option loads a GGUF inside PS Harness on loopback and needs no key or second app. Changes apply immediately to the next turn.</p>
      </form>
    </div>
    <div class="panel home-panel" id="panel-home">
      <div class="home-shell">
        <div class="home-kicker">PLAYSTATION 5 / HOME</div>
        <h3 class="home-title">Home screen app</h3>
        <div class="home-card">
          <img class="home-icon" id="home-icon" src="/tile-icon.png" alt="PS5 Harness tile" decoding="async">
          <div class="home-identity">
            <b>PS5 Harness</b>
            <span>Console workspace</span>
            <span class="home-badge" id="home-badge">Pending</span>
          </div>
        </div>
        <p class="home-detail" id="home-detail">Checking the tile registration.</p>
        <div class="home-rows">
          <div class="home-row">
            <div class="home-row-head"><span class="home-row-label">Tile status</span><strong id="home-state">Pending</strong></div>
          </div>
          <div class="home-row">
            <div class="home-row-head"><span class="home-row-label">Registration</span><strong id="home-registration">Checking…</strong></div>
          </div>
          <div class="home-row">
            <div class="home-row-head"><span class="home-row-label">Home screen address</span><strong id="home-address">—</strong></div>
          </div>
          <div class="home-row">
            <div class="home-row-head"><span class="home-row-label">Web-app shell</span><strong id="home-shell-state">Checking…</strong></div>
            <p id="home-shell">—</p>
          </div>
          <div class="home-row">
            <div class="home-row-head"><span class="home-row-label">Library</span><strong>Excluded</strong></div>
          <p>This tile's folder is reserved and excluded from game-library scans.</p>
          </div>
        </div>
        <div class="home-actions">
          <button class="home-primary" id="home-add" type="button">Add to home screen</button>
          <button class="home-secondary" id="home-check" type="button">Check status</button>
          <button class="home-danger" id="home-remove" type="button">Remove from home screen</button>
        </div>
        <div class="home-confirm hidden" id="home-confirm">
          <b>Remove home screen app?</b>
          <p>This deletes the tile folder, then asks ShadowMountPlus to uninstall it and rescans. It stays off after restart until you add it again.</p>
          <div class="home-confirm-actions">
            <button class="home-danger" id="home-confirm-yes" type="button">Yes, remove</button>
            <button class="home-secondary" id="home-confirm-no" type="button">No</button>
          </div>
        </div>
      </div>
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
    <div class="help-head"><h2 id="input-help-title">Keyboard, controller and voice</h2><button id="help-close" type="button">Close</button></div>
    <p>Use whichever input is in front of you. The composer stays a normal text field.</p>
    <dl>
      <dt>Keyboard + mouse</dt><dd>Type in the composer. Enter sends, Shift+Enter adds a line, Ctrl/⌘K returns to the composer. Every button, panel and tool chip is clickable.</dd>
      <dt>Controller buttons</dt><dd>D-pad or left stick moves focus; ✕ activates; ○ goes back; □ opens the keyboard; △ focuses the composer; Options toggles the keyboard; Create opens this help; L1/R1 switch Activity and Tools.</dd>
      <dt>Touchpad + pointer</dt><dd>Touch moves the pointer when the browser exposes gamepad touch surfaces; touchpad click, R3 or the touchpad button clicks. If coordinates are not exposed, use the right stick as a pointer and L3 to show it.</dd>
      <dt>Dictation</dt><dd>The mic button uses browser speech recognition and inserts final phrases at the cursor. The device running this page must expose the microphone.</dd>
    </dl>
    <p class="help-note">Speech recognition needs HTTPS or localhost. The default LAN HTTP address will show that requirement instead of silently failing.</p>
    <p class="help-note">A headset paired to the console is not visible to a remote browser. Select the controller or headset as the audio input on the device running this page. On a browser without Web Speech, use that device's own dictation key.</p>
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
const state = { token: '', busy: false, polling: false, history: [], homeTile: null,
  connected: null, lostAt: 0, reconnectDetail: '' };
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
    return esc(part)
      .replace(/`([^`]+)`/g, '<code>$1</code>')
      .replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>')
      .replace(/(^|\n)[-*] +/g, '$1• ')
      .replace(/\n/g, '<br>');
  }).join('');
}
function toast(message) {
  const el = $('toast'); el.textContent = message; el.classList.add('show');
  clearTimeout(toast.timer); toast.timer = setTimeout(() => el.classList.remove('show'), 3000);
}
function hideReconnect() {
  $('reconnect').classList.add('hidden');
}
function showReconnect(message) {
  if (!state.token) return;
  if (!state.lostAt) state.lostAt = Date.now();
  state.reconnectDetail = message || '';
  $('reconnect-last').textContent = window.location.origin;
  $('reconnect-detail').textContent = message
    ? 'Retrying this address automatically. ' + message
    : 'Retrying this address automatically…';
  if (!$('reconnect-url').value) $('reconnect-url').value = window.location.origin;
  $('reconnect').classList.remove('hidden');
}
function setConnected(ok, message) {
  const was = state.connected;
  state.connected = !!ok;
  $('dot').style.background = ok ? 'var(--green)' : 'var(--red)';
  $('dot').style.boxShadow = '0 0 10px ' + (ok ? '#42d392' : '#ff6b7d');
  $('conn').textContent = ok ? 'Connected to console' : 'Disconnected · retrying';
  if (ok) {
    state.lostAt = 0;
    hideReconnect();
    hideLogin();
    $('reconnect-url').value = window.location.origin;
    if (was === false && !state.busy) loadHistory().catch(() => {});
  } else {
    showReconnect(message || '');
  }
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
  const { timeoutMs = 6000, ...init } = options;
  const controller = timeoutMs > 0 && typeof AbortController !== 'undefined'
    ? new AbortController() : null;
  const timer = controller ? setTimeout(() => controller.abort(), timeoutMs) : null;
  try {
    const response = await fetch(path, {
      ...init,
      headers: { ...(init.headers || {}), ...authHeaders() },
      signal: controller ? controller.signal : undefined
    });
    if (response.status === 401) throw new AuthError('unauthorized');
    const text = await response.text();
    let data = {};
    try { data = text ? JSON.parse(text) : {}; } catch (_) { data = { error: text }; }
    if (!response.ok) throw new Error(data.error || ('HTTP ' + response.status));
    return data;
  } catch (error) {
    if (error && error.name === 'AbortError') throw new Error('connection timed out');
    throw error;
  } finally {
    if (timer) clearTimeout(timer);
  }
}
function showLogin(message) {
  $('login').classList.remove('hidden');
  $('login-error').textContent = message || '';
  setTimeout(() => $('token-input').focus(), 30);
}
function showTokenRejected(message) {
  state.connected = false;
  $('dot').style.background = 'var(--red)';
  $('dot').style.boxShadow = '0 0 10px #ff6b7d';
  $('conn').textContent = 'Token rejected';
  hideReconnect();
  showLogin(message || 'Token rejected. Enter the current token.');
}
function hideLogin() { $('login').classList.add('hidden'); }

function addMessage(role, content, pending) {
  const hero = $('hero'); if (hero) hero.remove();
  const wrap = document.createElement('div');
  wrap.className = 'msg ' + (role === 'user' ? 'user' : 'assistant');
  if (pending) wrap.id = 'pending-message';
  wrap.innerHTML =     '<div class="avatar">' + (role === 'user' ? 'YOU' : 'PS') + '</div>' +
    '<div class="body"><div class="who">' + (role === 'user' ? 'You' : 'Console') + '</div>' +
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
    $('messages').innerHTML = '<div class="hero" id="hero"><div class="hero-status"><i class="ready-dot"></i><span>CONSOLE READY</span><span class="hero-system">PLAYSTATION 5 / WORKSPACE</span></div><div class="hero-mark" aria-hidden="true">PS<span>5</span></div><h2>Console workspace</h2><p>Your console is online. Start a task, inspect the system, or continue the saved trace. Everything runs here, with the tool boundaries you configured.</p><div class="chips"><button class="chip" data-prompt="Map the PS5 architecture and filesystem layout. Start with env_info, then inspect the most important mounts and binaries."><span class="chip-kicker">SYSTEM</span><span class="chip-title">Map the system</span><span class="chip-desc">Architecture, mounts and binaries</span></button><button class="chip" data-prompt="Find the most interesting binaries under /data and identify their formats, imports and strings."><span class="chip-kicker">INSPECT</span><span class="chip-title">Inspect binaries</span><span class="chip-desc">Formats, imports and strings</span></button><button class="chip" data-prompt="Write a new local tool or plugin for this harness, then explain how to load it."><span class="chip-kicker">EXTEND</span><span class="chip-title">Build a tool</span><span class="chip-desc">Add a local capability</span></button><button class="chip" data-prompt="Plan a long research mission over the console, with explicit evidence and stop conditions."><span class="chip-kicker">MISSION</span><span class="chip-title">Plan a mission</span><span class="chip-desc">Evidence and stop conditions</span></button></div></div>';
    bindChips();
    $('messages').scrollTop = 0;
    return;
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
  $('mode').textContent = data.mode === 'plan' ? 'Plan · read only' : 'Full access';
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
  if (data.home_tile) renderHomeTile(data.home_tile);
  setConnected(true);
}
let modelBusy = false;
function modelSource(current) {
  if (current.local) return 'on-console · no API key';
  if (current.api_key_file) return 'key file ' + current.api_key_file;
  if (current.api_key_env) return 'env ' + current.api_key_env;
  return 'no key';
}
function renderModelCurrent(current) {
  const name = current.model || 'model';
  const base = current.base_url || '';
  $('model-current').innerHTML =
    '<b>' + esc(name) + '</b><br><span>' + esc(base) + '</span><br>' +
    '<span>' + esc(modelSource(current)) + '</span>';
}
function fillModelSuggestions(presets, current) {
  const seen = new Set();
  const names = [];
  const add = (value) => {
    if (!value || seen.has(value)) return;
    seen.add(value); names.push(value);
  };
  if (current && current.model) add(current.model);
  (presets || []).forEach((preset) => (preset.models || []).forEach(add));
  $('model-suggestions').innerHTML = names.map((name) =>
    '<option value="' + esc(name) + '"></option>').join('');
}
function fillModelForm(current, presets) {
  current = current || {};
  const select = $('model-preset');
  const match = (presets || []).find((preset) => preset.base_url === current.base_url);
  select.innerHTML = '<option value="">Custom endpoint</option>' +
    (presets || []).map((preset) => '<option value="' + esc(preset.id) + '">' +
      esc(preset.label) + '</option>').join('');
  select.value = match ? match.id : '';
  $('model-base-url').value = current.base_url || '';
  $('model-local').checked = !!current.local;
  $('model-local-path').value = current.local_model_path || '';
  $('model-name').value = current.model || '';
  $('model-key-env').value = current.api_key_env || '';
  $('model-key-file').value = current.api_key_file || '';
  $('model-temperature').value = current.temperature == null ? '' : current.temperature;
  $('model-max-tokens').value = current.max_tokens == null ? '' : current.max_tokens;
  fillModelSuggestions(presets, current);
  renderModelCurrent(current);
  $('model-error').textContent = '';
}
async function loadModelSettings() {
  if (!state.token) return;
  $('model-error').textContent = '';
  try {
    const data = await api('/api/model', { timeoutMs: 10000 });
    state.modelData = data;
    fillModelForm(data.current, data.presets);
  } catch (error) {
    if (error instanceof AuthError) showTokenRejected('Token rejected. Enter the current token.');
    else $('model-error').textContent = String(error.message || error);
  }
}
function applyModelPreset() {
  const data = state.modelData || {};
  const preset = (data.presets || []).find((item) => item.id === $('model-preset').value);
  if (!preset) return;
  $('model-base-url').value = preset.base_url || '';
  $('model-local').checked = !!preset.local;
  $('model-name').value = preset.model || '';
  if (preset.local && !$('model-local-path').value) {
    $('model-local-path').value = (state.modelData && state.modelData.current && state.modelData.current.local_model_path) || '';
  }
  $('model-key-env').value = preset.api_key_env || '';
  // A key-file override is authoritative; clear it so a preset change does not
  // silently send the previous provider's key to the new endpoint.
  $('model-key-file').value = '';
  fillModelSuggestions(data.presets, { model: preset.model });
  $('model-error').textContent = '';
}
async function applyModelSettings(event) {
  event.preventDefault();
  if (modelBusy) return;
  const baseUrl = $('model-base-url').value.trim();
  const model = $('model-name').value.trim();
  const local = $('model-local').checked;
  if (!baseUrl || !model) {
    $('model-error').textContent = 'Base URL and model ID are required.';
    return;
  }
  modelBusy = true;
  const button = $('model-apply');
  button.disabled = true;
  $('model-error').textContent = '';
  try {
    const temperature = $('model-temperature').value === ''
      ? 0.2 : Number($('model-temperature').value);
    const maxTokens = $('model-max-tokens').value === ''
      ? 0 : Number($('model-max-tokens').value);
    const data = await api('/api/model', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        base_url: baseUrl,
        model,
        local,
        local_model_path: local ? $('model-local-path').value.trim() : '',
        local_model_port: (state.modelData && state.modelData.current && state.modelData.current.local_model_port) || 18081,
        local_model_threads: (state.modelData && state.modelData.current && state.modelData.current.local_model_threads) || 4,
        local_model_context: (state.modelData && state.modelData.current && state.modelData.current.local_model_context) || 4096,
        api_key_env: local ? '' : $('model-key-env').value.trim(),
        api_key_file: local ? '' : $('model-key-file').value.trim(),
        temperature,
        max_tokens: maxTokens
      }),
      timeoutMs: 20000
    });
    state.modelData = data;
    fillModelForm(data.current, data.presets);
    toast('Model switched to ' + (data.current && data.current.model || model));
    await refreshState();
  } catch (error) {
    if (error instanceof AuthError) showTokenRejected('Token rejected. Enter the current token.');
    else $('model-error').textContent = String(error.message || error);
  } finally {
    modelBusy = false;
    button.disabled = false;
  }
}
let homeBusy = false;
function setHomeBusy(busy) {
  homeBusy = busy;
  ['home-add', 'home-check', 'home-remove', 'home-confirm-yes', 'home-confirm-no'].forEach((id) => {
    const button = $(id); if (button) button.disabled = busy;
  });
  if (!busy && state.homeTile) renderHomeTile(state.homeTile);
}
function renderHomeTile(tile) {
  if (!tile) return;
  state.homeTile = tile;
  const installed = tile.state === 'installed';
  const badge = $('home-badge');
  badge.textContent = installed ? 'Installed' : 'Pending';
  badge.className = 'home-badge' + (installed ? ' installed' : '');
  $('home-state').textContent = installed ? 'Installed' : 'Pending';
  $('home-detail').textContent = tile.detail || (installed ? 'Registered with ShadowMountPlus.' : 'Checking the tile registration.');
  $('home-address').textContent = tile.deeplink || '—';
  $('home-shell-state').textContent = tile.shell_available ? 'Available' : 'Missing';
  $('home-shell').textContent = tile.web_shell || '—';
  let registration = 'Not registered';
  if (installed) registration = 'Registered';
  else if (!tile.service_reachable) registration = 'No service on 10101';
  else if (!tile.scan_path_ok) registration = 'Scan path missing';
  $('home-registration').textContent = registration;
  const add = $('home-add');
  if (add) { add.disabled = homeBusy; add.textContent = installed ? 'Reinstall tile' : 'Add to home screen'; }
  if ($('home-check')) $('home-check').disabled = homeBusy;
  if ($('home-remove')) $('home-remove').disabled = homeBusy || !tile.folder_present;
  if (!tile.folder_present) $('home-confirm').classList.add('hidden');
}
async function refreshHomeTile() {
  if (!state.token || homeBusy) return;
  setHomeBusy(true);
  try {
    const data = await api('/api/home-tile', { timeoutMs: 15000 });
    renderHomeTile(data.home_tile);
  } catch (error) {
    if (error instanceof AuthError) showTokenRejected('Token rejected. Enter the current token.');
    else toast(String(error.message || error));
  } finally { setHomeBusy(false); }
}
async function homeAction(method, label) {
  if (!state.token || homeBusy) return;
  setHomeBusy(true);
  try {
    const data = await api('/api/home-tile', { method, timeoutMs: 30000 });
    renderHomeTile(data.home_tile);
    const tile = data.home_tile || {};
    if (tile.state === 'installed') toast('Home screen app installed');
    else if (!data.ok && data.error) toast(String(data.error));
    else toast(label + ': ' + (tile.detail || 'Pending'));
    await refreshState();
  } catch (error) {
    if (error instanceof AuthError) showTokenRejected('Token rejected. Enter the current token.');
    else toast(String(error.message || error));
  } finally { setHomeBusy(false); }
}
async function refreshState() {
  if (state.polling || !state.token) return;
  state.polling = true;
  try { renderState(await api('/api/state', { timeoutMs: 8000 })); }
  catch (error) {
    if (error instanceof AuthError) {
      showTokenRejected('Token rejected. Enter the current token. If the payload was restarted with a generated token, use the new one.');
    } else {
      setConnected(false, String(error.message || error));
    }
  } finally { state.polling = false; }
}
async function loadHistory() {
  try {
    const data = await api('/api/history');
    state.history = data.messages || [];
    renderHistory(state.history);
  } catch (error) {
    if (error instanceof AuthError) showTokenRejected('Token rejected. Enter the current token.');
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
    if (error instanceof AuthError) showTokenRejected('Token rejected. Enter the current token.');
    else showLogin(String(error.message || error));
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
$('show-model').addEventListener('click', () => selectTab('model'));
$('show-home').addEventListener('click', () => selectTab('home'));
$('model').addEventListener('click', () => selectTab('model'));
$('model-preset').addEventListener('change', applyModelPreset);
$('model-form').addEventListener('submit', applyModelSettings);
function selectTab(name) {
  document.body.classList.toggle('home-tab', name === 'home');
  document.querySelectorAll('.tabs button').forEach((b) => b.classList.toggle('active', b.dataset.tab === name));
  ['activity', 'tools', 'model', 'home'].forEach((panel) => {
    const element = $('panel-' + panel);
    if (element) element.classList.toggle('active', name === panel);
  });
  if (window.innerWidth <= 1150) {
    document.querySelector('.inspector').classList.add('mobile-open');
    document.body.classList.add('mobile-panel');
    document.querySelectorAll('[data-mobile]').forEach((b) => b.classList.toggle('active', b.dataset.mobile === name));
  }
  if (name === 'model') loadModelSettings();
  if (name === 'home') refreshHomeTile();
  echoPanel(name);
}
// The agent's screen and this page are the same surface. A panel a person opens
// is what the agent's next frame shows.
function echoPanel(name) {
  if (!state.token) return;
  fetch('/api/ui/panel', {
    method: 'POST',
    headers: Object.assign({ 'Content-Type': 'application/json' }, authHeaders()),
    body: JSON.stringify({ panel: name })
  }).catch(() => {});
}
// Focus is shared the same way. A control a person reaches with the controller,
// the keyboard or a click reports its id, so the agent's next frame shows where
// they are. Only the ids differ between the page and the console surface.
const CONSOLE_CONTROL_IDS = {
  prompt: 'composer:field', send: 'composer:send',
  'model-local': 'model:local', 'model-base-url': 'model:base_url',
  'model-name': 'model:id', 'model-key-env': 'model:key-env',
  'model-key-file': 'model:key-file', 'model-temperature': 'model:temperature',
  'model-max-tokens': 'model:max_tokens', 'model-apply': 'model:apply',
  'home-add': 'home:add', 'home-check': 'home:check', 'home-remove': 'home:remove'
};
function consoleControlId(el) {
  if (!el || el === document.body || !el.closest) return '';
  const tab = el.closest('.tabs button[data-tab]');
  if (tab) return 'rail:' + tab.dataset.tab;
  const tool = el.closest('[data-tool]');
  if (tool) return 'tool:' + tool.dataset.tool;
  return CONSOLE_CONTROL_IDS[el.id] || '';
}
let consoleFocusSent = '';
function reportConsoleFocus(el) {
  if (!state.token) return;
  const id = consoleControlId(el);
  if (!id || id === consoleFocusSent) return;
  consoleFocusSent = id;
  fetch('/api/ui/focus', {
    method: 'POST',
    headers: Object.assign({ 'Content-Type': 'application/json' }, authHeaders()),
    body: JSON.stringify({ focus: id })
  }).catch(() => { consoleFocusSent = ''; });
}
document.addEventListener('focusin', (event) => reportConsoleFocus(event.target), true);
// And a panel the agent opens is followed here while it is driving, so a person
// watching sees the console move by itself.
let consoleDriverPanel = '';
async function pollConsoleDriver() {
  if (!state.token) return;
  try {
    const data = await api('/api/ui', { timeoutMs: 4000 });
    if (!data || !data.driving || !data.panel) return;
    if (data.panel === consoleDriverPanel) return;
    consoleDriverPanel = data.panel;
    const active = document.querySelector('.tabs button.active');
    if (!active || active.dataset.tab !== data.panel) {
      selectTab(data.panel);
      toast('Console driver: ' + data.panel);
    }
  } catch (_) {}
}
setInterval(pollConsoleDriver, 1500);
function closeMobilePanel() {
  document.querySelector('.inspector').classList.remove('mobile-open');
  document.body.classList.remove('mobile-panel');
  document.body.classList.remove('home-tab');
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
      body: JSON.stringify({ message }), timeoutMs: 600000
    });
    const pending = $('pending-message'); if (pending) pending.remove();
    if (data.ok) addMessage('assistant', data.answer || '(no content)', false);
    else addMessage('assistant', 'Error: ' + (data.error || 'turn failed'), false);
    await refreshState();
  } catch (error) {
    const pending = $('pending-message'); if (pending) pending.remove();
    if (error instanceof AuthError) showTokenRejected('Token rejected. Enter the current token.');
    else addMessage('assistant', 'Error: ' + String(error.message || error), false);
  } finally {
    state.busy = false; $('send').disabled = false; $('prompt').focus();
  }
});
$('prompt').addEventListener('keydown', (event) => {
  if (event.key === 'Enter' && !event.shiftKey) { event.preventDefault(); $('composer').requestSubmit(); }
});
$('home-add').addEventListener('click', () => homeAction('POST', 'Add'));
$('home-check').addEventListener('click', refreshHomeTile);
$('home-remove').addEventListener('click', () => {
  if (homeBusy) return;
  $('home-confirm').classList.remove('hidden');
  $('home-confirm-no').focus();
});
$('home-confirm-no').addEventListener('click', () => {
  $('home-confirm').classList.add('hidden');
  $('home-remove').focus();
});
$('home-confirm-yes').addEventListener('click', async () => {
  $('home-confirm').classList.add('hidden');
  await homeAction('DELETE', 'Remove');
});

function consoleUrlFromInput() {
  let value = String($('reconnect-url').value || '').trim();
  if (!value) throw new Error('Enter the console address or URL.');
  if (!/^[a-z][a-z0-9+.-]*:\/\//i.test(value)) {
    value = window.location.protocol + '//' + value;
  }
  const url = new URL(value);
  if (url.protocol !== 'http:' && url.protocol !== 'https:') {
    throw new Error('Use an http:// or https:// address.');
  }
  url.pathname = '/';
  url.search = '';
  url.hash = '';
  return url;
}
function useConsoleAddress() {
  try {
    const url = consoleUrlFromInput();
    if (url.origin === window.location.origin) {
      $('reconnect-detail').textContent = 'That is the current address. Retrying now…';
      refreshState();
      return;
    }
    if (state.token) url.searchParams.set('token', state.token);
    window.location.assign(url.toString());
  } catch (error) {
    const message = String(error.message || error);
    $('reconnect-detail').textContent = message;
    toast(message);
  }
}
$('reconnect-now').addEventListener('click', () => {
  $('reconnect-detail').textContent = 'Retrying this address now…';
  refreshState();
});
$('reconnect-use').addEventListener('click', useConsoleAddress);
$('reconnect-url').addEventListener('keydown', (event) => {
  if (event.key === 'Enter') { event.preventDefault(); useConsoleAddress(); }
});
window.addEventListener('online', () => { if (state.token) refreshState(); });
document.addEventListener('visibilitychange', () => {
  if (!document.hidden && state.token && !state.busy) refreshState();
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
    'button:not([disabled]),textarea:not([disabled]),input:not([disabled]),select:not([disabled]),a[href],[tabindex]:not([tabindex="-1"])'
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
  $('input-pill').textContent = pad ? ('Input · ' + padLabel(pad)) : 'Keyboard + mouse';
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
/* ---- PS5 ambient particle field ---------------------------------------
   A slow drift of soft dust motes over the spotlit backdrop, echoing the
   PS5 system-software background. Static single frame when the viewer
   prefers reduced motion. */
(function ps5Background(){
  var canvas = document.getElementById('ps5-bg');
  if (!canvas || !canvas.getContext) return;
  var ctx = canvas.getContext('2d');
  var reduce = window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  var dpr = Math.min(window.devicePixelRatio || 1, 2);
  var w = 0, h = 0, motes = [];
  var TINTS = ['255,255,255','196,214,255','255,224,206','206,255,240'];
  function seed(){
    var count = Math.round(Math.min(120, Math.max(44, (w * h) / 26000)));
    motes = [];
    for (var i = 0; i < count; i++){
      motes.push({
        x: Math.random() * w,
        y: Math.random() * h,
        r: 0.6 + Math.random() * 2.4,
        a: 0.04 + Math.random() * 0.26,
        vx: (Math.random() - 0.5) * 0.12,
        vy: -(0.05 + Math.random() * 0.22),
        tint: TINTS[(Math.random() * TINTS.length) | 0],
        tw: Math.random() * Math.PI * 2
      });
    }
  }
  function resize(){
    w = canvas.clientWidth = window.innerWidth;
    h = canvas.clientHeight = window.innerHeight;
    canvas.width = w * dpr; canvas.height = h * dpr;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    seed();
  }
  function draw(t){
    ctx.clearRect(0, 0, w, h);
    for (var i = 0; i < motes.length; i++){
      var m = motes[i];
      if (!reduce){
        m.x += m.vx; m.y += m.vy; m.tw += 0.01;
        if (m.y < -8) { m.y = h + 8; m.x = Math.random() * w; }
        if (m.x < -8) m.x = w + 8; else if (m.x > w + 8) m.x = -8;
      }
      var tw = reduce ? 1 : (0.65 + 0.35 * Math.sin(m.tw));
      ctx.beginPath();
      ctx.arc(m.x, m.y, m.r, 0, Math.PI * 2);
      ctx.fillStyle = 'rgba(' + m.tint + ',' + (m.a * tw).toFixed(3) + ')';
      ctx.shadowBlur = m.r * 3; ctx.shadowColor = 'rgba(' + m.tint + ',0.5)';
      ctx.fill();
    }
    ctx.shadowBlur = 0;
    if (!reduce) requestAnimationFrame(draw);
  }
  var rt;
  window.addEventListener('resize', function(){ clearTimeout(rt); rt = setTimeout(resize, 160); });
  resize();
  requestAnimationFrame(draw);
})();
/* ---- Top-bar clock (PS5 system clock) --------------------------------- */
(function ps5Clock(){
  var el = document.getElementById('clock');
  if (!el) return;
  function tick(){
    var d = new Date();
    var h = d.getHours(), m = d.getMinutes();
    el.textContent = (h < 10 ? '0' + h : h) + ':' + (m < 10 ? '0' + m : m);
  }
  tick();
  setInterval(tick, 20000);
})();

/* ---- Home-tile image: fall back to a neutral tile if the icon is absent */
(function ps5TileFallback(){
  var img = document.getElementById('home-icon');
  if (!img) return;
  var fallback = "data:image/svg+xml;utf8," + encodeURIComponent(
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 80 80'>" +
    "<rect width='80' height='80' rx='18' fill='#17191e'/>" +
    "<g fill='none' stroke='#aeb4bf' stroke-width='3' stroke-linecap='round' stroke-linejoin='round'>" +
    "<path d='M40 20 L47 34 L33 34 Z'/><circle cx='59' cy='44' r='6.5'/>" +
    "<rect x='15' y='37.5' width='13' height='13' rx='2'/>" +
    "<path d='M34 55 L46 67 M46 55 L34 67'/></g></svg>");
  function swap(){ if (img.src !== fallback) { img.onerror = null; img.src = fallback; } }
  img.addEventListener('error', swap);
  if (img.complete && img.naturalWidth === 0) swap();
})();
</script>
</body>
</html>)PS5HUI";
}

}  // namespace ps5h
