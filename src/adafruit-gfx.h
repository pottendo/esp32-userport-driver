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
public:
    c64_adafruit(int16_t w, int16_t h) : Adafruit_GFX(w, h) {};
    virtual ~c64_adafruit() = default;

    virtual void drawPixel(int16_t x, int16_t y, uint16_t color) override;
};

bool plot_af(pp_drv *drv);

#endif