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

#include <Arduino.h>

/* disable brownout */
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

#include "misc.h"
#include "cmd-if.h"
#include "wifi.h"

void setup()
{
    Serial.begin(115200);
    //printf("Disabling brownout...\n");
    //WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // disable brownout detector, needed for my AzDelivery ESP32 Mini D1 module
    setup_log();
    setup_wifi();
    setup_cmd();
}

void loop()
{
    loop_cmd();
}
