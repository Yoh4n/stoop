#pragma once

#include <WebServer.h>

#include <functional>

// CTA 'L' train arrivals app: config storage and its web UI at /trains.
namespace trains {

constexpr uint8_t kMaxTrainCount = 6;
constexpr uint8_t kDefaultTrainCount = 3;

struct Config {
  String route;        // Train Tracker route code: Red, Blue, Brn, G, Org, Pink, P, Y
  String lineName;     // "Red Line"
  uint32_t mapId = 0;  // parent station ID (4xxxx), Train Tracker `mapid`
  String stationName;  // "Jarvis"
  uint32_t stopId = 0; // platform/direction stop ID (3xxxx), Train Tracker `stpid`
  String direction;    // "Howard-bound"
  String apiKey;       // CTA Train Tracker API key
  uint8_t trainCount = kDefaultTrainCount;  // arrivals to rotate through

  bool hasStop() const { return stopId != 0; }
};

// Loads saved config and registers the /trains routes. Call after the web
// server is set up (i.e. not in Wi-Fi setup mode).
void begin(WebServer& server);

const Config& config();

// Called after the user saves a new config from the web UI.
void onConfigChanged(std::function<void()> cb);

}  // namespace trains
