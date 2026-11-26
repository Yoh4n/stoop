#pragma once

#include <Arduino.h>

// Shared helpers for the device's web pages.
namespace webui {

inline String htmlEscape(const String& in) {
  String out;
  out.reserve(in.length());
  for (char c : in) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out += c;
    }
  }
  return out;
}

inline String jsonEscape(const String& in) {
  String out = "\"";
  for (char c : in) {
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if (c == '<') out += "\\u003c";  // safe to inline in <script>
    else if ((uint8_t)c < 0x20) { char buf[8]; snprintf(buf, sizeof(buf), "\\u%04x", c); out += buf; }
    else out += c;
  }
  return out + "\"";
}

// Wraps body in a full HTML document. Pass showHome=false on pages that have
// no home to return to (the Wi-Fi setup portal).
inline String page(const String& title, const String& body, bool showHome = true) {
  return "<!doctype html><html><head><meta charset=utf-8>"
         "<meta name=viewport content='width=device-width,initial-scale=1'>"
         "<title>" + htmlEscape(title) + "</title><style>"
         "body{font-family:system-ui,sans-serif;max-width:460px;margin:1.5em auto;padding:0 1em;color:#222}"
         "input,select,button{width:100%;padding:.6em;margin:.3em 0 1em;box-sizing:border-box;font-size:1em}"
         "label,h2{font-weight:600}h2{font-size:1.05em;margin:1.4em 0 .4em}a{color:#06c}"
         ".muted{color:#666;font-size:.9em}.card{border:1px solid #ddd;border-radius:8px;padding:.8em 1em;margin:1em 0}"
         "</style></head><body>" +
         (showHome ? String("<p class=muted><a href=/>&larr; Home</a></p>") : String()) +
         "<h1>" + htmlEscape(title) + "</h1>" + body + "</body></html>";
}

}  // namespace webui
