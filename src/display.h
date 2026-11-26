#pragma once

#include <Arduino_GFX_Library.h>

// Landscape 320x172 framebuffer for the onboard ST7789. Draw into gfx(), then
// call flush() to push the whole frame to the panel (no flicker).
namespace display
{

  constexpr int16_t kWidth = 320;
  constexpr int16_t kHeight = 172;

  void begin();
  Arduino_GFX *gfx();
  void flush();

  // 0xRRGGBB -> RGB565
  constexpr uint16_t rgb(uint32_t hex)
  {
    return ((hex >> 8) & 0xF800) | ((hex >> 5) & 0x07E0) | ((hex >> 3) & 0x001F);
  }

  // Height of a capital letter in a U8g2 font, in pixels (from the font header).
  inline int16_t capHeight(const uint8_t *u8g2Font) { return (int8_t)pgm_read_byte(u8g2Font + 13); }

  // Width in pixels of `text` in the current font.
  int16_t textWidth(const String &text);

  // Draw `text` with its baseline at y. align: -1 left (x = left edge),
  // 0 centered (x = center), 1 right (x = right edge).
  void drawText(const String &text, int16_t x, int16_t y, uint16_t color, int8_t align = -1);

  // Full-screen status message: a title plus up to two smaller lines.
  void showMessage(const String &title, const String &line1 = "", const String &line2 = "",
                   uint16_t accent = rgb(0x565a5c));

} // namespace display
