#pragma once

#include <WebServer.h>

// Wi-Fi credential provisioning over the web.
//
// On boot, tries the credentials saved in NVS. If there are none, or they fail,
// starts an open access point ("<hostname>-XXXX") with a captive portal where
// you pick a network and enter its password. Once connected, the same
// WebServer serves a status page at http://<hostname>.local/ with a /wifi page
// for changing networks, so register your own routes on it after begin().
namespace wifiprov {

// Call once from setup(). Blocks for up to ~15 s while connecting.
void begin(WebServer& server, const char* hostname);

// Call every loop(). Services DNS (portal mode) and the web server.
void loop();

// True while the setup access point is running instead of a normal connection.
bool inSetupMode();

// Name of the setup access point (empty unless in setup mode).
String apName();

// Erase saved credentials. Takes effect on next boot.
void forgetCredentials();

}  // namespace wifiprov
