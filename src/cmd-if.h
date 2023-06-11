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

#ifndef __CMD_IF_H__
#define __CMD_IF_H__

typedef enum { uCCoRoutine = 1, uCZiModem, oCZiModem } uCmode_t;
void setup_cmd(void);
void loop_cmd(void);
void change_mode(uCmode_t mode);
uCmode_t get_mode(void);
String get_mode_str(void);
void mqtt_cmd(String &cmd);

uint8_t charset_p_topetcii(uint8_t c);
uint8_t charset_p_toascii(uint8_t c, int cs);
typedef enum { ASCII2PETSCII, PETSCII2ASCII } char_conv_t;
void string2Xscii(char *buf, const char *str, char_conv_t dir);

#endif