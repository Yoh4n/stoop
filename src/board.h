#pragma once

// Waveshare ESP32-C6-LCD-1.47 pin map.
// https://docs.waveshare.com/ESP32-C6-LCD-1.47
namespace board
{

    // ST7789 LCD, 172x320, SPI (MOSI/SCLK shared with the TF card)
    constexpr int kLcdMosi = 6;
    constexpr int kLcdSclk = 7;
    constexpr int kLcdCs = 14;
    constexpr int kLcdDc = 15;
    constexpr int kLcdRst = 21;
    constexpr int kLcdBacklight = 22;
    constexpr int kLcdWidth = 172; // native (portrait) size
    constexpr int kLcdHeight = 320;
    constexpr int kLcdColOffset = 34; // panel is centered in the ST7789's 240-column RAM

    // TF card (unused for now)
    constexpr int kSdMiso = 5;
    constexpr int kSdCs = 4;

    constexpr int kRgbLed = 8;
    constexpr int kBootButton = 9; // active low

} // namespace board
