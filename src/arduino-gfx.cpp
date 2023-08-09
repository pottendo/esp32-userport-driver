/* -*-c++-*-
 * This file is part of esp32-userport-driver.
 *
 * FE playground is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FE playground is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FE playground.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#include "adafruit-gfx.h"

const uint8_t PIXELW = 2;
static c64_adafruit c64_gfx(320 / PIXELW, 200);
const uint8_t C64COLRAM = 0xb2;

unsigned long testRoundRects()
{
    int w, i, i2,
        cx = c64_gfx.width() / 2 - 1,
        cy = c64_gfx.height() / 2 - 1;
    int c = 0;
    w = min(c64_gfx.width(), c64_gfx.height());
    for (i = 0; i < w; i += 18)
    {
      c++;
        i2 = i / 2;
        c64_gfx.drawRoundRect(cx - i2, cy - i2, i, i, i / 8, (c%3)+1);
    }
    return 0;
}

unsigned long testTriangles()
{
    int n, i, cx = c64_gfx.width() / 2 - 1,
              cy = c64_gfx.height() / 2 - 1;
    int c;
    // c64_gfx.fillScreen(ILI9341_BLACK);
    n = min(cx, cy);
    for (i = 0; i < n; i += 15)
    {
      c++;
        c64_gfx.drawTriangle(
            cx, cy - i,     // peak
            cx - i, cy + i, // bottom left
            cx + i, cy + i, // bottom right
            (c%3) + 1);
    }
    return 0;
}

static unsigned long testRects(uint16_t color) {
  int           n, i, i2,
                cx = c64_gfx.width()  / 2,
                cy = c64_gfx.height() / 2;
  int c = color;
  c64_gfx.fillScreen(C64COLRAM);
  n     = min(c64_gfx.width(), c64_gfx.height());
  for(i=2; i<n; i+=6) {
    i2 = i / 2;
    c = (++c % 3) + 1;
    c64_gfx.drawRect(cx-i2, cy-i2, i, i, c);
  }

  return 0;
}

unsigned long testText() {
  c64_gfx.fillScreen(C64COLRAM);
  c64_gfx.setCursor(0, 0);
  c64_gfx.setTextColor(1);  c64_gfx.setTextSize(1);
  c64_gfx.println("Hello World!");
  c64_gfx.setTextColor(2); c64_gfx.setTextSize(2);
  c64_gfx.println(1234.56);
  c64_gfx.setTextColor(3);    c64_gfx.setTextSize(3);
  c64_gfx.println(0xDEADBEEF, HEX);
  c64_gfx.println();
  c64_gfx.setTextColor(1);
  c64_gfx.setTextSize(5);
  c64_gfx.println("Groop");
  c64_gfx.setTextSize(2);
  c64_gfx.println("I implore thee,");
  c64_gfx.setTextSize(1);
  c64_gfx.println("my foonting turlingdromes.");
  c64_gfx.println("And hooptiously drangle me");
  c64_gfx.println("with crinkly bindlewurdles,");
  c64_gfx.println("Or I will rend thee");
  c64_gfx.println("in the gobberwarts");
  c64_gfx.println("with my blurglecruncheon,");
  c64_gfx.println("see if I don't!");
  return 0;
}

unsigned long testFilledRects(uint16_t color1, uint16_t color2) {
  int           n, i, i2,
                cx = c64_gfx.width()  / 2 - 1,
                cy = c64_gfx.height() / 2 - 1;
  int c = color1;
  int c2 = color2;
  c64_gfx.fillScreen(C64COLRAM);
  n = min(c64_gfx.width(), c64_gfx.height());
  for(i=n; i>0; i-=18) {
    i2    = i / 2;
    c = (++c %3) + 1;
    c64_gfx.fillRect(cx-i2, cy-i2, i, i, c);
    // Outlines are not included in timing results
    c2 = (--c2 %3) + 1;
    c64_gfx.drawRect(cx-i2, cy-i2, i, i, c2);
    yield();
  }

  return 0;
}

unsigned long testLines(uint16_t color) {
  unsigned long start, t;
  int           x1, y1, x2, y2,
                w = c64_gfx.width(),
                h = c64_gfx.height();

  c64_gfx.fillScreen(C64COLRAM);
  yield();
  
  x1 = y1 = 0;
  y2    = h - 1;
  start = micros();
  for(x2=0; x2<w; x2+=6) c64_gfx.drawLine(x1, y1, x2, y2, (color++ % 3) + 1);
  x2    = w - 1;
  for(y2=0; y2<h; y2+=6) c64_gfx.drawLine(x1, y1, x2, y2, (color++ %3) + 1);
  t     = micros() - start; // fillScreen doesn't count against timing

return 0;
  yield();
  c64_gfx.fillScreen(C64COLRAM);
  yield();

  x1    = w - 1;
  y1    = 0;
  y2    = h - 1;
  start = micros();
  for(x2=0; x2<w; x2+=6) c64_gfx.drawLine(x1, y1, x2, y2, color);
  x2    = 0;
  for(y2=0; y2<h; y2+=6) c64_gfx.drawLine(x1, y1, x2, y2, color);
  t    += micros() - start;

  yield();
  c64_gfx.fillScreen(C64COLRAM);
  yield();

  x1    = 0;
  y1    = h - 1;
  y2    = 0;
  start = micros();
  for(x2=0; x2<w; x2+=6) c64_gfx.drawLine(x1, y1, x2, y2, color);
  x2    = w - 1;
  for(y2=0; y2<h; y2+=6) c64_gfx.drawLine(x1, y1, x2, y2, color);
  t    += micros() - start;

  yield();
  c64_gfx.fillScreen(C64COLRAM);
  yield();

  x1    = w - 1;
  y1    = h - 1;
  y2    = 0;
  start = micros();
  for(x2=0; x2<w; x2+=6) c64_gfx.drawLine(x1, y1, x2, y2, color);
  x2    = 0;
  for(y2=0; y2<h; y2+=6) c64_gfx.drawLine(x1, y1, x2, y2, color);

  yield();
  return micros() - start;
}

unsigned long testFastLines(uint16_t color1, uint16_t color2) {
  unsigned long start;
  int           x, y, w = c64_gfx.width(), h = c64_gfx.height() - 1;

  c64_gfx.fillScreen(C64COLRAM);
  start = micros();
  for(y=0; y<h; y+=5) c64_gfx.drawFastHLine(0, y, w, color1);
  for(x=0; x<w; x+=5) c64_gfx.drawFastVLine(x, 0, h, color2);

  return micros() - start;
}


bool plot_af(pp_drv *_drv)
{
    c64_gfx.fillScreen(C64COLRAM);
    c64_gfx.drawCircle(80, 100, 79, 1);
    testFilledRects(1, 2);
    testFastLines(1,2);
    testLines(3);
    testRoundRects();
    testTriangles();
    testRects(1);
    testText();
    c64_gfx.close();
    return true;
}

//static members
char c64_adafruit::buf[256];

void c64_adafruit::push(void)
{
    int ret, len;
    len = (buf[0] & 0x0f) + 1;  // len coded in cmd, low nibble
    ret = drv.write(buf, len);
    if (ret != len)
        log_msg("coroutine plot, write error: %d\n", ret);
    delay(1);
}

void c64_adafruit::close(void)
{
    buf[0] = plPLEND;
    push();
}

void c64_adafruit::drawPixel(int16_t x, int16_t y, uint16_t color)
{
    const uint h = x % (8 / PIXELW);
    const uint shift = (8 / PIXELW - 1) - h;

    //log_msg("c64-AF: %s\n", __FUNCTION__); 
    col = color % 4;
    buf[0] = plPLOT;
    buf[1] = (color << (shift * PIXELW));
    buf[2] = x % 256;
    buf[3] = x / 256;
    buf[4] = y;
    push();
}

void c64_adafruit::drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color)
{
    //log_msg("c64-AF: %s, col = %d\n", __FUNCTION__, color); 
    col = color % 4;
    buf[0] = plFLV;
    buf[1] = col;
    buf[2] = x % 256;
    buf[3] = x / 256;
    buf[4] = y;
    buf[5] = (y + h) % 200;   
    push();
}

void c64_adafruit::drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color)
{
    //log_msg("c64-AF: %s, col = %d\n", __FUNCTION__, color);
    col = color % 4;
    buf[0] = plFLH;
    buf[1] = col;
    buf[2] = x % 256;
    buf[3] = x / 256;
    buf[4] = y;
    buf[5] = (x + w) % 256;   
    buf[6] = (x + w) / 256;   
    push();
}

void c64_adafruit::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    //log_msg("c64-AF: %s\n", __FUNCTION__);
    col = color % 4;
    buf[0] = plFILLR;   // plFILLR XXX to be implemented
    buf[1] = col;
    buf[2] = x % 256;
    buf[3] = x / 256;
    buf[4] = y;
    buf[5] = (x + w) % 256;   
    buf[6] = (x + w) / 256;   
    buf[7] = y + h;
    push();
}

void c64_adafruit::drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    //log_msg("c64-AF: %s\n", __FUNCTION__);
    col = color % 4;
    buf[0] = plPLOTR;
    buf[1] = col;
    buf[2] = x % 256;
    buf[3] = x / 256;
    buf[4] = y;
    buf[5] = (x + w) % 256;   
    buf[6] = (x + w) / 256;   
    buf[7] = y + h;
    push();
}

void c64_adafruit::drawLine(int16_t x, int16_t y, int16_t x2, int16_t y2, uint16_t color)
{
    //log_msg("c64-AF: %s\n", __FUNCTION__);
    col = color % 4;
    buf[0] = plLINE;
    buf[1] = col;
    buf[2] = x % 256;
    buf[3] = x / 256;
    buf[4] = y;
    buf[5] = x2 % 256;   
    buf[6] = x2 / 256;   
    buf[7] = y2;
    push();
}

void c64_adafruit::fillScreen(uint16_t color)
{
    //log_msg("c64-AF: %s\n", __FUNCTION__);
    col = color % 256;
    buf[0] = plFILLSC;
    buf[1] = col;
    push();
}
