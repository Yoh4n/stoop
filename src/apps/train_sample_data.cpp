#include "train_sample_data.h"

namespace train_sample_data {
namespace {

using train_api::Arrival;
using train_api::Result;

enum Flags : uint8_t { kNone = 0, kApp = 1, kSch = 2, kDly = 4, kFlt = 8 };

Arrival arr(const char* route, const char* dest, const char* run, int32_t etaSec, uint8_t flags = kNone) {
  Arrival a;
  a.route = route;
  a.destination = dest;
  a.run = run;
  a.etaSec = etaSec;
  a.approaching = flags & kApp;
  a.scheduled = flags & kSch;
  a.delayed = flags & kDly;
  a.fault = flags & kFlt;
  return a;
}

Result ok(std::vector<Arrival> arrivals) {
  Result r;
  r.ok = true;
  r.arrivals = std::move(arrivals);
  return r;
}

Result fail(const char* error) {
  Result r;
  r.error = error;
  return r;
}

struct Scenario {
  const char* name;
  Result (*make)();
};

const Scenario kScenarios[] = {
    {"normal: three Red Line trains",
     [] {
       return ok({arr("Red", "Howard", "812", 4 * 60 + 20), arr("Red", "Howard", "815", 11 * 60 + 5),
                  arr("Red", "Howard", "820", 19 * 60 + 40)});
     }},
    {"due: approaching flag, under a minute, just arrived",
     [] {
       return ok({arr("Red", "Howard", "809", 3 * 60, kApp),  // approaching overrides the 3 min
                  arr("Red", "Howard", "812", 50),             // <= 1 min shows Due
                  arr("Red", "Howard", "815", -10)});          // past arrival but inside the drop window
     }},
    {"status flags: delayed, scheduled, estimate",
     [] {
       return ok({arr("Red", "Howard", "812", 6 * 60, kDly), arr("Red", "Howard", "", 14 * 60, kSch),
                  arr("Red", "Howard", "820", 22 * 60, kFlt)});
     }},
    {"mixed lines at a shared Loop stop (header colors, dot colors)",
     [] {
       return ok({arr("Brn", "Kimball", "407", 2 * 60 + 30), arr("P", "Linden", "512", 5 * 60),
                  arr("Y", "Dempster-Skokie", "591", 8 * 60), arr("Org", "Midway", "706", 9 * 60 + 30),
                  arr("Pink", "54th/Cermak", "308", 12 * 60), arr("G", "Ashland/63rd", "014", 15 * 60)});
     }},
    {"long destinations and big numbers (font fallback, truncation)",
     [] {
       return ok({arr("G", "Harlem/Lake", "002", 99 * 60),
                  arr("Blue", "Forest Park via Congress Branch", "122", 45 * 60),
                  arr("Brn", "Harold Washington Library-State/Van Buren", "433", 8 * 60, kDly)});
     }},
    {"stale: one long gone, one just dropped, one upcoming",
     [] {
       return ok({arr("Red", "95th/Dan Ryan", "901", -120),  // dropped: well past arrival
                  arr("Red", "95th/Dan Ryan", "904", -25),   // drops a few seconds after fetch
                  arr("Red", "95th/Dan Ryan", "907", 7 * 60)});
     }},
    {"single train (one page dot)", [] { return ok({arr("Blue", "O'Hare", "131", 17 * 60)}); }},
    {"empty: no arrivals predicted", [] { return ok({}); }},
    {"API error", [] { return fail("Invalid API key"); }},
    {"network error", [] { return fail("Network error (connection refused)"); }},
};
constexpr size_t kScenarioCount = sizeof(kScenarios) / sizeof(kScenarios[0]);

size_t nextIndex = 0;

}  // namespace

Result next(uint8_t max) {
  const Scenario& s = kScenarios[nextIndex];
  Serial.printf("[trains] sample %u/%u: %s\n", (unsigned)(nextIndex + 1), (unsigned)kScenarioCount, s.name);
  nextIndex = (nextIndex + 1) % kScenarioCount;

  delay(400);  // roughly a real request's latency, so the loading path gets exercised
  Result r = s.make();
  if (r.arrivals.size() > max) r.arrivals.resize(max);
  r.fetchedAtMs = millis();
  return r;
}

}  // namespace train_sample_data
