#pragma once

// Shows the next few trains (1-6, set at /trains) for the picked stop, one at a time,
// rotating every few seconds. Polls the CTA Train Tracker API in the background
// of loop().
namespace train_tracker {

void begin();
void loop();

// Fetch again right away (e.g. after the stop or API key changed).
void refreshNow();

}  // namespace train_tracker
