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

#ifndef __ADAFRUIT_GFX_H__
#define __ADAFRUIT_GFX_H__

#include <Adafruit_GFX.h>
#include "parport-drv.h"

class c64_adafruit : public Adafruit_GFX
{
    static char buf[256];
    int col;

    void push(void);

public:
    c64_adafruit(int16_t w, int16_t h) : Adafruit_GFX(w, h){};
    virtual ~c64_adafruit() = default;

    // cmd: high-nibble = cmdnum, low-nibble = #bytes
    // typical format of bytestream: <col byte><x 16bit><y 8bit> total 1+3
    enum
    {
        plPLOT = 0x14,
        plFLH = 0x26,    // col(1byte), p1(3byte), len(2byte)
        plFLV = 0x35,    // col, p1, len(1byte)
        plLINE = 0x47,   // col, p1, p2
        plFILLSC = 0x51, // col
        plFILLR = 0x67,  // col, p1, p2
        plPLOTR = 0x77,  // col, p1, p2
        plPLEND = 0xf0   // end marker
    };

    virtual void drawPixel(int16_t x, int16_t y, uint16_t color) override;

    virtual void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override;
    virtual void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override;
    virtual void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override;
    virtual void fillScreen(uint16_t color) override;
    virtual void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) override;
    virtual void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override;

    void close(void);
};

bool plot_af(pp_drv *drv);

#endif