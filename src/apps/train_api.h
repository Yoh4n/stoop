#pragma once

#include <Arduino.h>

#include <vector>

// Client for the CTA Train Tracker Arrivals API.
// https://www.transitchicago.com/developers/ttdocs/
namespace train_api
{

  struct Arrival
  {
    String route;       // "Red", "Brn", ...
    String destination; // "Howard"
    String run;         // run number, e.g. "812"
    int32_t etaSec = 0; // seconds from the API's timestamp to predicted arrival
    bool approaching = false;
    bool scheduled = false; // schedule-based, not live tracking
    bool delayed = false;
    bool fault = false; // CTA flags the prediction as unreliable
  };

  struct Result
  {
    bool ok = false;
    String error; // set when !ok
    std::vector<Arrival> arrivals;
    uint32_t fetchedAtMs = 0; // millis() when the response arrived
  };

  // Blocking HTTPS request (~0.5-2 s). Returns up to `max` arrivals for one platform stop.
  Result fetch(const String &apiKey, uint32_t stopId, uint8_t max = 3);

} // namespace train_api
