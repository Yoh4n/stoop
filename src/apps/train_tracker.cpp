#include "train_tracker.h"

#include <U8g2lib.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "display.h"
#include "train_api.h"
#include "trains.h"

namespace train_tracker
{
  namespace
  {

#ifdef TRAIN_SAMPLE_DATA
    constexpr uint32_t kPollMs = 20000; // sample data: step through scenarios faster
#else
    constexpr uint32_t kPollMs = 30000; // ~2.9k calls/day, well under CTA's daily key limit
#endif
    constexpr uint32_t kRetryMs = 15000;             // after an error
    constexpr uint32_t kRotateMs = 6000;             // time each train stays on screen
    constexpr uint32_t kRedrawMs = 1000;             // keep the countdown current
    constexpr int32_t kDropAfterSec = -30;           // hide a train this long after its predicted arrival
    constexpr uint32_t kFetchStackBytes = 12 * 1024; // TLS handshake + JSON parsing

    // --- Shared between the fetch task and loop(); only touch while holding `lock`. ---
    struct Request
    {
      String apiKey;
      uint32_t stopId = 0;
      uint8_t count = trains::kDefaultTrainCount;
      uint32_t generation = 0; // bumped by refreshNow() so stale responses can be dropped
    };
    struct Shared
    {
      Request request;
      train_api::Result result;
      bool haveResult = false;
      uint32_t resultGeneration = 0; // request.generation the result belongs to
      uint32_t version = 0;          // bumped on every new result
    };
    Shared shared;
    SemaphoreHandle_t lock = nullptr;
    TaskHandle_t fetchTask = nullptr;

    // --- Owned by loop() only. ---
    train_api::Result result; // copy of the latest result for the current request
    bool haveResult = false;
    uint32_t seenVersion = 0;
    uint32_t lastRotateMs = 0;
    uint32_t lastDrawMs = 0;
    uint8_t current = 0;

    uint32_t lineColor(const String &rt)
    {
      if (rt == "Red")
        return 0xc60c30;
      if (rt == "Blue")
        return 0x00a1de;
      if (rt == "Brn")
        return 0x62361b;
      if (rt == "G")
        return 0x009b3a;
      if (rt == "Org")
        return 0xf9461c;
      if (rt == "Pink")
        return 0xe27ea6;
      if (rt == "P")
        return 0x522398;
      if (rt == "Y")
        return 0xf9e300;
      return 0x565a5c;
    }

    int32_t secondsAway(const train_api::Arrival &a)
    {
      return a.etaSec - (int32_t)((millis() - result.fetchedAtMs) / 1000);
    }

    // Trains that haven't come and gone since the last fetch.
    std::vector<const train_api::Arrival *> upcoming()
    {
      std::vector<const train_api::Arrival *> out;
      for (const auto &a : result.arrivals)
      {
        if (secondsAway(a) > kDropAfterSec && out.size() < trains::config().trainCount)
          out.push_back(&a);
      }
      return out;
    }

    void drawArrival(const trains::Config &cfg, const train_api::Arrival &a, uint8_t index, uint8_t count)
    {
      using display::drawText;
      using display::rgb;
      Arduino_GFX *g = display::gfx();
      const uint32_t color = lineColor(a.route.length() ? a.route : cfg.route);
      const uint16_t headerText = color == 0xf9e300 ? RGB565_BLACK : RGB565_WHITE;
      const uint16_t muted = rgb(0x9a9a9a);

      g->fillScreen(RGB565_BLACK);

      // Header: station in the line color. Long names drop to a smaller font, then truncate.
      constexpr int16_t kHeaderHeight = 42, kHeaderMaxWidth = display::kWidth - 20;
      g->fillRect(0, 0, display::kWidth, kHeaderHeight, rgb(color));
      const uint8_t *headerFont = u8g2_font_helvB18_tr;
      g->setFont(headerFont);
      String station = cfg.stationName;
      if (display::textWidth(station) > kHeaderMaxWidth)
      {
        headerFont = u8g2_font_helvB14_tr;
        g->setFont(headerFont);
        while (station.length() > 1 && display::textWidth(station + "...") > kHeaderMaxWidth)
        {
          station.remove(station.length() - 1);
        }
        if (station != cfg.stationName)
          station += "...";
      }
      drawText(station, 15, (kHeaderHeight + display::capHeight(headerFont)) / 2, headerText);

      // Body: the left text block (run + destination) and the right block (minutes)
      // share one vertical center between the header and the page dots.
      using display::capHeight;
      constexpr int16_t kRight = 308, kLeft = 12, kCenterY = 97; // midway between header and dots
      const int32_t mins = secondsAway(a) / 60;
      const bool due = a.approaching || mins <= 1;

      // Minutes away, big, on the right.
      int16_t rightWidth;
      if (due)
      {
        g->setFont(u8g2_font_logisoso42_tr);
        rightWidth = display::textWidth("Due");
        drawText("Due", kRight, kCenterY + capHeight(u8g2_font_logisoso42_tr) / 2, rgb(0xf9e300), 1);
      }
      else
      {
        constexpr int16_t kGap = 10;
        const int16_t numCap = capHeight(u8g2_font_logisoso50_tn), minCap = capHeight(u8g2_font_helvB14_tr);
        const int16_t numBase = kCenterY - (numCap + kGap + minCap) / 2 + numCap;
        g->setFont(u8g2_font_logisoso50_tn);
        rightWidth = display::textWidth(String(mins));
        drawText(String(mins), kRight, numBase, RGB565_WHITE, 1);
        g->setFont(u8g2_font_helvB14_tr);
        rightWidth = max(rightWidth, display::textWidth("min"));
        drawText("min", kRight, numBase + kGap + minCap, muted, 1);
      }

      // Destination: largest font that fits beside the minutes, else truncated.
      const int16_t maxDestWidth = kRight - rightWidth - 14 - kLeft;
      const uint8_t *const destFonts[] = {u8g2_font_fub30_tr, u8g2_font_fub25_tr, u8g2_font_fub20_tr,
                                          u8g2_font_fub17_tr};
      const uint8_t *destFont = destFonts[0];
      String dest = a.destination;
      for (const uint8_t *f : destFonts)
      {
        destFont = f;
        g->setFont(f);
        if (display::textWidth(dest) <= maxDestWidth)
          break;
      }
      if (display::textWidth(dest) > maxDestWidth)
      {
        while (dest.length() > 1 && display::textWidth(dest + "...") > maxDestWidth)
          dest.remove(dest.length() - 1);
        dest += "...";
      }

      constexpr int16_t kRunGap = 12;
      const int16_t runCap = capHeight(u8g2_font_helvR12_tr), destCap = capHeight(destFont);
      const int16_t runBase = kCenterY - (runCap + kRunGap + destCap) / 2 + runCap;
      const int16_t destBase = runBase + kRunGap + destCap;
      g->setFont(u8g2_font_helvR12_tr);
      drawText(a.run.length() ? "Run #" + a.run + " to" : String("To"), kLeft, runBase, muted);
      g->setFont(destFont);
      drawText(dest, kLeft, destBase, RGB565_WHITE);

      // Status flags, under the destination.
      g->setFont(u8g2_font_helvR12_tr);
      const int16_t flagBase = destBase + 12 + runCap;
      if (a.delayed)
      {
        drawText("Delayed", kLeft, flagBase, rgb(0xf9a01c));
      }
      else if (a.scheduled)
      {
        drawText("Scheduled", kLeft, flagBase, muted);
      }
      else if (a.fault)
      {
        drawText("Estimate", kLeft, flagBase, muted);
      }

      // Page dots.
      const int16_t spacing = 16, cy = 161;
      int16_t cx = display::kWidth / 2 - (count - 1) * spacing / 2;
      for (uint8_t i = 0; i < count; i++, cx += spacing)
      {
        if (i == index)
          g->fillCircle(cx, cy, 4, rgb(color == 0x62361b || color == 0x522398 ? 0xffffff : color));
        else
          g->drawCircle(cx, cy, 4, muted);
      }

      display::flush();
    }

    void draw()
    {
      const trains::Config &cfg = trains::config();
      // Only built when shown, so sample builds (no Wi-Fi) never query the network stack.
      auto url = []
      { return "http://" + WiFi.localIP().toString() + "/trains"; };

      if (!cfg.hasStop())
      {
        display::showMessage("Pick a stop", "Open stoop.local/trains", "or " + url());
        return;
      }
      if (cfg.apiKey.isEmpty())
      {
        display::showMessage("Add an API key", "Open stoop.local/trains", "or " + url());
        return;
      }
      if (!haveResult)
      {
        display::showMessage(cfg.stationName, "Loading arrivals...", "", display::rgb(lineColor(cfg.route)));
        return;
      }
      if (!result.ok)
      {
        display::showMessage("Can't get arrivals", result.error, "Retrying shortly", display::rgb(0xc60c30));
        return;
      }

      auto trainsList = upcoming();
      if (trainsList.empty())
      {
        display::showMessage(cfg.stationName, "No arrivals predicted", cfg.direction,
                             display::rgb(lineColor(cfg.route)));
        return;
      }
      if (current >= trainsList.size())
        current = 0;
      drawArrival(cfg, *trainsList[current], current, trainsList.size());
    }

    // Runs forever in its own FreeRTOS task. The HTTPS request blocks only this
    // task, so loop() keeps drawing and serving the web UI meanwhile.
    void fetchTaskFn(void *)
    {
      bool lastOk = true;
      bool loggedResources = false;
      for (;;)
      {
        // Sleep until the next poll is due, or until refreshNow() wakes us.
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(lastOk ? kPollMs : kRetryMs));

        xSemaphoreTake(lock, portMAX_DELAY);
        Request req = shared.request;
        xSemaphoreGive(lock);

#ifdef TRAIN_SAMPLE_DATA
        const bool online = true; // sample data needs no network
#else
        const bool online = WiFi.status() == WL_CONNECTED;
#endif
        if (!req.stopId || req.apiKey.isEmpty() || !online)
        {
          lastOk = false; // check again after kRetryMs
          continue;
        }

        train_api::Result r = train_api::fetch(req.apiKey, req.stopId, req.count);
        lastOk = r.ok;
        if (r.ok)
        {
          Serial.printf("[trains] %u arrivals\n", (unsigned)r.arrivals.size());
        }
        else
        {
          Serial.printf("[trains] fetch failed: %s\n", r.error.c_str());
        }
        if (!loggedResources)
        {
          loggedResources = true;
          Serial.printf("[trains] fetch task stack headroom %u bytes, free heap %u bytes\n",
                        (unsigned)uxTaskGetStackHighWaterMark(nullptr), (unsigned)ESP.getFreeHeap());
        }

        xSemaphoreTake(lock, portMAX_DELAY);
        if (req.generation == shared.request.generation)
        { // ignore if the stop changed mid-request
          shared.result = std::move(r);
          shared.haveResult = true;
          shared.resultGeneration = req.generation;
          shared.version++;
        }
        xSemaphoreGive(lock);
      }
    }

    // Copy a new result from the fetch task, if there is one. Returns true if it changed.
    bool takeNewResult()
    {
      bool changed = false;
      xSemaphoreTake(lock, portMAX_DELAY);
      if (shared.version != seenVersion)
      {
        seenVersion = shared.version;
        if (shared.resultGeneration == shared.request.generation)
        {
          result = shared.result;
          haveResult = shared.haveResult;
          changed = true;
        }
      }
      xSemaphoreGive(lock);
      return changed;
    }

  } // namespace

  void begin()
  {
    lock = xSemaphoreCreateMutex();
    // Same priority as the Arduino loop task; the fetch spends its time waiting on the network.
    xTaskCreate(fetchTaskFn, "cta_fetch", kFetchStackBytes, nullptr, 1, &fetchTask);
    refreshNow();
    draw();
  }

  void refreshNow()
  {
    const trains::Config &cfg = trains::config();
    xSemaphoreTake(lock, portMAX_DELAY);
    shared.request.apiKey = cfg.apiKey;
    shared.request.stopId = cfg.stopId;
    shared.request.count = cfg.trainCount;
    shared.request.generation++;
    xSemaphoreGive(lock);

    haveResult = false;
    current = 0;
    lastDrawMs = 0;
    xTaskNotifyGive(fetchTask); // fetch now instead of waiting out the poll interval
  }

  void loop()
  {
    if (takeNewResult())
    {
      lastRotateMs = millis();
      lastDrawMs = 0; // redraw immediately with fresh data
    }
    if (millis() - lastRotateMs >= kRotateMs)
    {
      lastRotateMs = millis();
      current++;
      lastDrawMs = 0;
    }
    if (lastDrawMs == 0 || millis() - lastDrawMs >= kRedrawMs)
    {
      draw();
      lastDrawMs = millis();
    }
  }

} // namespace train_tracker
