#include "display.h"

#include <U8g2lib.h>

#include "board.h"

namespace display {
namespace {

Arduino_DataBus* bus = nullptr;
Arduino_GFX* panel = nullptr;
Arduino_Canvas* canvas = nullptr;

}  // namespace

void begin() {
  bus = new Arduino_ESP32SPI(board::kLcdDc, board::kLcdCs, board::kLcdSclk, board::kLcdMosi,
                             GFX_NOT_DEFINED /* MISO */);
  panel = new Arduino_ST7789(bus, board::kLcdRst, 0 /* rotation */, true /* IPS */, board::kLcdWidth,
                             board::kLcdHeight, board::kLcdColOffset, 0, board::kLcdColOffset, 0);
  // Canvas is native-size; rotation 1 makes it landscape. Use 3 if the image is upside down.
  canvas = new Arduino_Canvas(board::kLcdWidth, board::kLcdHeight, panel, 0, 0, 1);
  if (!canvas->begin()) {
    Serial.println("[display] init failed (out of memory for framebuffer?)");
  }
  canvas->fillScreen(RGB565_BLACK);
  canvas->flush();

  pinMode(board::kLcdBacklight, OUTPUT);
  digitalWrite(board::kLcdBacklight, HIGH);
}

Arduino_GFX* gfx() { return canvas; }

void flush() { canvas->flush(); }

int16_t textWidth(const String& text) {
  int16_t x1, y1;
  uint16_t w, h;
  canvas->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  return w;
}

void drawText(const String& text, int16_t x, int16_t y, uint16_t color, int8_t align) {
  int16_t w = align < 0 ? 0 : textWidth(text);
  canvas->setTextColor(color);
  canvas->setCursor(align == 0 ? x - w / 2 : align > 0 ? x - w : x, y);
  canvas->print(text);
}

void showMessage(const String& title, const String& line1, const String& line2, uint16_t accent) {
  canvas->fillScreen(RGB565_BLACK);
  canvas->fillRect(0, 0, kWidth, 6, accent);
  canvas->setFont(u8g2_font_helvB18_tr);
  drawText(title, kWidth / 2, 70, RGB565_WHITE, 0);
  canvas->setFont(u8g2_font_helvR12_tr);
  if (line1.length()) drawText(line1, kWidth / 2, 108, rgb(0xc8c8c8), 0);
  if (line2.length()) drawText(line2, kWidth / 2, 132, rgb(0xc8c8c8), 0);
  canvas->flush();
}

}  // namespace display
