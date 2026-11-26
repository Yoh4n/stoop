#include "train_api.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>

#ifdef TRAIN_SAMPLE_DATA
#include "train_sample_data.h"
#endif

namespace train_api
{
  namespace
  {

    // Days since 1970-01-01 for a civil date (Howard Hinnant's algorithm).
    int32_t daysFromCivil(int y, unsigned m, unsigned d)
    {
      y -= m <= 2;
      const int era = (y >= 0 ? y : y - 399) / 400;
      const unsigned yoe = (unsigned)(y - era * 400);
      const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
      const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
      return era * 146097 + (int32_t)doe - 719468;
    }

    // "2026-11-25T08:52:30" (Chicago local time) -> seconds since epoch, as if UTC.
    // Only differences between two API timestamps are used, so the zone doesn't matter.
    bool parseTime(const char *s, int64_t &out)
    {
      int y, mo, d, h, mi, se;
      if (!s || sscanf(s, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &se) != 6)
        return false;
      out = (int64_t)daysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60 + se;
      return true;
    }

    bool flag(JsonVariantConst v)
    {
      const char *s = v.as<const char *>();
      return s && s[0] == '1';
    }

    void parseEta(JsonObjectConst e, int64_t now, Result &r)
    {
      int64_t arr;
      if (!parseTime(e["arrT"], arr))
        return;
      Arrival a;
      a.route = e["rt"].as<const char *>();
      a.destination = e["destNm"].as<const char *>();
      a.run = e["rn"].as<const char *>();
      a.etaSec = (int32_t)(arr - now);
      a.approaching = flag(e["isApp"]);
      a.scheduled = flag(e["isSch"]);
      a.delayed = flag(e["isDly"]);
      a.fault = flag(e["isFlt"]);
      r.arrivals.push_back(a);
    }

  } // namespace

  Result fetch(const String &apiKey, uint32_t stopId, uint8_t max)
  {
#ifdef TRAIN_SAMPLE_DATA
    (void)apiKey;
    (void)stopId;
    return train_sample_data::next(max);
#endif
    Result r;
    String url = "https://lapi.transitchicago.com/api/1.0/ttarrivals.aspx?outputType=JSON&key=" + apiKey +
                 "&stpid=" + String(stopId) + "&max=" + String(max);

    NetworkClientSecure client;
    // Skips certificate verification to keep this simple. The data is public,
    // but the API key could be read by someone intercepting traffic.
    client.setInsecure();
    HTTPClient http;
    http.useHTTP10(true); // no chunked encoding, so the body can be streamed into the parser
    http.setTimeout(8000);
    if (!http.begin(client, url))
    {
      r.error = "Bad request URL";
      return r;
    }
    int code = http.GET();
    if (code != HTTP_CODE_OK)
    {
      r.error = code < 0 ? "Network error (" + http.errorToString(code) + ")" : "HTTP " + String(code);
      http.end();
      return r;
    }

    JsonDocument filter;
    filter["ctatt"]["tmst"] = true;
    filter["ctatt"]["errCd"] = true;
    filter["ctatt"]["errNm"] = true;
    for (const char *k : {"rt", "destNm", "rn", "arrT", "isApp", "isSch", "isDly", "isFlt"})
    {
      filter["ctatt"]["eta"][0][k] = true;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();
    r.fetchedAtMs = millis();
    if (err)
    {
      r.error = String("Bad response: ") + err.c_str();
      return r;
    }

    JsonObjectConst tt = doc["ctatt"];
    const char *errCd = tt["errCd"] | "0";
    if (strcmp(errCd, "0") != 0)
    {
      r.error = tt["errNm"] | "CTA API error";
      return r;
    }
    int64_t now;
    if (!parseTime(tt["tmst"], now))
    {
      r.error = "Bad response timestamp";
      return r;
    }
    // `eta` is normally an array, but tolerate a lone object too.
    JsonVariantConst eta = tt["eta"];
    if (eta.is<JsonArrayConst>())
    {
      for (JsonObjectConst e : eta.as<JsonArrayConst>())
        parseEta(e, now, r);
    }
    else if (eta.is<JsonObjectConst>())
    {
      parseEta(eta.as<JsonObjectConst>(), now, r);
    }
    r.ok = true;
    return r;
  }

} // namespace train_api
