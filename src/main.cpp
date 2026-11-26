#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>

#include "apps/train_tracker.h"
#include "apps/trains.h"
#include "board.h"
#include "display.h"
#include "web_ui.h"
#include "wifi_provisioning.h"

constexpr const char *kHostname = "stoop";

WebServer server(80);

// Hold BOOT for 3 s right after reset to wipe saved Wi-Fi credentials.
void checkFactoryReset()
{
  pinMode(board::kBootButton, INPUT_PULLUP);
  uint32_t start = millis();
  while (digitalRead(board::kBootButton) == LOW)
  {
    if (millis() - start > 3000)
    {
      Serial.println("[main] BOOT held, forgetting Wi-Fi credentials");
      wifiprov::forgetCredentials();
      display::showMessage("Wi-Fi forgotten", "Release the button");
      while (digitalRead(board::kBootButton) == LOW)
        delay(10);
      break;
    }
    delay(10);
  }
}

void handleHome()
{
  using webui::htmlEscape;
  const trains::Config &t = trains::config();
  String trainSummary = t.hasStop() ? htmlEscape(t.lineName + " · " + t.stationName + " · " + t.direction)
                                    : String("Not set up yet");
  String body =
      "<a class=card style='display:block;text-decoration:none' href=/trains><b>Train arrivals</b><br>"
      "<span class=muted>" +
      trainSummary + "</span></a>"
                     "<div class=card><b>Bus arrivals</b><br><span class=muted>Coming soon</span></div>"
                     "<a class=card style='display:block;text-decoration:none' href=/wifi><b>Wi-Fi</b><br>"
                     "<span class=muted>" +
      htmlEscape(WiFi.SSID()) + " · " + WiFi.localIP().toString() + "</span></a>";
  server.send(200, "text/html", webui::page(kHostname, body, false));
}

void setup()
{
  Serial.begin(115200);
  delay(1000); // give native USB serial time to enumerate
  Serial.println("\n[main] stoop booting");

  display::begin();

#ifdef TRAIN_SAMPLE_DATA
  // Sample build: no network needed, so skip Wi-Fi entirely and act as if connected.
  Serial.println("[main] sample data build, skipping Wi-Fi");
#else
  checkFactoryReset();

  display::showMessage("Connecting to Wi-Fi...");
  wifiprov::begin(server, kHostname);

  if (wifiprov::inSetupMode())
  {
    display::showMessage("Wi-Fi setup", "Join the network " + wifiprov::apName(),
                         "on your phone to connect me");
    return;
  }
#endif

  trains::begin(server);
  trains::onConfigChanged(train_tracker::refreshNow);
  server.on("/", HTTP_GET, handleHome);
  train_tracker::begin();
}

void loop()
{
#ifdef TRAIN_SAMPLE_DATA
  train_tracker::loop();
#else
  wifiprov::loop();
  if (!wifiprov::inSetupMode())
    train_tracker::loop();
#endif
}
