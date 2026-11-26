#include "wifi_provisioning.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>

#include "web_ui.h"

namespace wifiprov {
namespace {

using webui::htmlEscape;
using webui::page;

constexpr uint32_t kConnectTimeoutMs = 15000;
constexpr uint16_t kDnsPort = 53;
constexpr const char* kPrefsNamespace = "wifi";

WebServer* server = nullptr;
DNSServer dns;
Preferences prefs;
String host;
bool setupMode = false;
String apSsid;
String networkOptions;  // cached <option> list from the last scan

void scanNetworks() {
  Serial.println("[wifi] scanning...");
  int n = WiFi.scanNetworks();
  networkOptions = "";
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.isEmpty() || networkOptions.indexOf(">" + htmlEscape(ssid) + " (") >= 0) continue;
    String esc = htmlEscape(ssid);
    networkOptions += "<option value=\"" + esc + "\">" + esc + " (" + WiFi.RSSI(i) + " dBm" +
                      (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? ", open" : "") + ")</option>";
  }
  WiFi.scanDelete();
  Serial.printf("[wifi] found %d networks\n", n);
}

String wifiForm() {
  return "<form method=POST action=/save>"
         "<label>Network</label>"
         "<select id=pick onchange=\"document.getElementById('ssid').value=this.value\">"
         "<option value=''>-- pick a scanned network --</option>" + networkOptions + "</select>"
         "<label>SSID</label><input id=ssid name=ssid required maxlength=32>"
         "<label>Password</label><input name=pass type=password maxlength=64>"
         "<button type=submit>Save &amp; connect</button></form>"
         "<p><a href=/rescan>Rescan networks</a></p>";
}

void handleSave() {
  String ssid = server->arg("ssid");
  String pass = server->arg("pass");
  if (ssid.isEmpty()) {
    server->send(400, "text/plain", "SSID required");
    return;
  }
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  Serial.printf("[wifi] saved credentials for '%s', restarting\n", ssid.c_str());
  server->send(200, "text/html",
               page("Saved", "<p>Restarting and joining <b>" + htmlEscape(ssid) +
                                 "</b>.</p><p>If it works, find the device at <a href='http://" + host +
                                 ".local/'>http://" + host + ".local/</a>. If not, the setup network "
                                 "will reappear.</p>",
                    false));
  delay(1000);
  ESP.restart();
}

void handleRescan() {
  scanNetworks();
  server->sendHeader("Location", setupMode ? "/" : "/wifi", true);
  server->send(302, "text/plain", "");
}

void registerPortalRoutes() {
  server->on("/", HTTP_GET, [] { server->send(200, "text/html", page(host + " setup", wifiForm(), false)); });
  server->on("/save", HTTP_POST, handleSave);
  server->on("/rescan", HTTP_GET, handleRescan);
  // Anything else (OS connectivity checks like /generate_204, /hotspot-detect.html)
  // redirects to the form, which makes phones pop the captive portal sheet.
  server->onNotFound([] {
    server->sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
    server->send(302, "text/plain", "");
  });
}

void registerStationRoutes() {
  server->on("/wifi", HTTP_GET, [] {
    String body = "<p>Connected to <b>" + htmlEscape(WiFi.SSID()) + "</b> (" + WiFi.RSSI() +
                  " dBm), IP " + WiFi.localIP().toString() + "</p>" + wifiForm() +
                  "<form method=POST action=/forget onsubmit=\"return confirm('Forget network and reboot into setup mode?')\">"
                  "<button type=submit>Forget network</button></form>";
    server->send(200, "text/html", page("Wi-Fi", body));
  });
  server->on("/save", HTTP_POST, handleSave);
  server->on("/rescan", HTTP_GET, handleRescan);
  server->on("/forget", HTTP_POST, [] {
    forgetCredentials();
    server->send(200, "text/html", page("Forgotten", "<p>Restarting into setup mode.</p>"));
    delay(1000);
    ESP.restart();
  });
}

bool connect(const String& ssid, const String& pass) {
  Serial.printf("[wifi] connecting to '%s'", ssid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(host.c_str());
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid.c_str(), pass.c_str());
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < kConnectTimeoutMs) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

void startPortal() {
  setupMode = true;
  // AP+STA so we can still scan for networks while the AP is up.
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP_STA);
  scanNetworks();

  uint8_t mac[6];
  WiFi.macAddress(mac);
  char name[40];
  snprintf(name, sizeof(name), "%s-%02X%02X", host.c_str(), mac[4], mac[5]);
  apSsid = name;
  WiFi.softAP(apSsid.c_str());
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(kDnsPort, "*", WiFi.softAPIP());

  registerPortalRoutes();
  server->begin();
  Serial.printf("[wifi] setup mode: join '%s' and open http://%s/\n", apSsid.c_str(),
                WiFi.softAPIP().toString().c_str());
}

}  // namespace

void begin(WebServer& srv, const char* hostname) {
  server = &srv;
  host = hostname;
  prefs.begin(kPrefsNamespace, false);

  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");

  if (ssid.isEmpty() || !connect(ssid, pass)) {
    Serial.println(ssid.isEmpty() ? "[wifi] no saved credentials" : "[wifi] connection failed");
    startPortal();
    return;
  }

  Serial.printf("[wifi] connected, IP %s\n", WiFi.localIP().toString().c_str());
  if (MDNS.begin(host.c_str())) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("[wifi] http://%s.local/\n", host.c_str());
  }
  registerStationRoutes();
  server->begin();
}

void loop() {
  if (setupMode) dns.processNextRequest();
  server->handleClient();
}

bool inSetupMode() { return setupMode; }

String apName() { return apSsid; }

void forgetCredentials() {
  prefs.remove("ssid");
  prefs.remove("pass");
}

}  // namespace wifiprov
