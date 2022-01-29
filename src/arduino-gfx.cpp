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

static c64_adafruit c64_gfx(320, 200);

unsigned long testRoundRects()
{
    int w, i, i2,
        cx = c64_gfx.width() / 2 - 1,
        cy = c64_gfx.height() / 2 - 1;

    w = min(c64_gfx.width(), c64_gfx.height());
    for (i = 0; i < w; i += 18)
    {
        i2 = i / 2;
        c64_gfx.drawRoundRect(cx - i2, cy - i2, i, i, i / 8, 1);
    }
    return 0;
}

unsigned long testTriangles()
{
    int n, i, cx = c64_gfx.width() / 2 - 1,
              cy = c64_gfx.height() / 2 - 1;

    // c64_gfx.fillScreen(ILI9341_BLACK);
    n = min(cx, cy);
    for (i = 0; i < n; i += 15)
    {
        c64_gfx.drawTriangle(
            cx, cy - i,     // peak
            cx - i, cy + i, // bottom left
            cx + i, cy + i, // bottom right
            1);
    }
    return 0;
}

bool plot_af(pp_drv *_drv)
{
    // c64_gfx.fillScreen(0);
    c64_gfx.drawCircle(160, 100, 80, 1);
    testRoundRects();
    testTriangles();
    return true;
}

void c64_adafruit::drawPixel(int16_t x, int16_t y, uint16_t color)
{
    int ret;
    static char buf[4];
    buf[0] = x % 256;
    buf[1] = x / 256;
    buf[2] = y;
    int c = (color > 0) ? 1 : 0;
    buf[3] = c << (7 - (x % 8));
    ret = drv.write(buf, 4);
    if (ret != 4)
        log_msg("coroutine plot, write error: %d\n", ret);
    delay(2);
}
