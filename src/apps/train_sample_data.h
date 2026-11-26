#pragma once

#include "train_api.h"

// Canned Train Tracker responses for testing the display without an API key or
// network. Build with -DTRAIN_SAMPLE_DATA (the `esp32-c6-sample` env) and
// train_api::fetch() returns these instead of calling CTA, stepping to the next
// scenario on every call.
namespace train_sample_data {

// Returns the next scenario's result, capped at `max` arrivals like the real API.
train_api::Result next(uint8_t max);

}  // namespace train_sample_data
