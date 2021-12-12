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

#ifndef __html_h__
#define __html_h__
#include <AutoConnect.h>
#include "cmd-if.h"

void setup_html(AutoConnect *p, WebServer *w);
void setup_websocket(void);
void loop_websocket(void);
void web_send_cmd(String cmd);

uCmode_t web_get_uCmode(void);

#endif