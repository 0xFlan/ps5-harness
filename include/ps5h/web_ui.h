#pragma once

namespace ps5h {

// Self-contained HTML/CSS/JS. No CDN, fonts or external assets: the console
// serves the same page on a LAN with no internet dependency.
const char* web_ui_html();

}  // namespace ps5h
