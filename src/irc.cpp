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

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <IRCClient.h>
#include <list>
#include <map>
#include "irc.h"
#include "logger.h"
#include "cred.h"
#include "misc.h"
#include "pet2asc.h"
#include "cmd-if.h"
static SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
static std::list<String> msgs;

#ifndef TEST_IRC
static IRCClient *iclient;
static WiFiClientSecure *wclient;

static void callback(IRCMessage ircMessage)
{
    // PRIVMSG ignoring CTCP messages
    if (ircMessage.command == "PRIVMSG" && ircMessage.text[0] != '\001')
    {
        String message(ircMessage.nick + ">" + ircMessage.text);
        log_msg(message + '\n');
        P(mutex);
        if (msgs.size() > 500)
            msgs.pop_front();
        msgs.push_back(message);
        V(mutex);
        return;
    }
    // log_msg(ircMessage.original + '\n');
    msgs.push_back(ircMessage.original);
}

static void debugSentCallback(String data)
{
    log_msg(data + '\n');
}

static void send_msg(String s)
{
    iclient->sendRaw(s);
}
#endif /* TEST_IRC */

bool irc_t::get_msg(String &s)
{
    _FMUTEX(mutex);
    if (msgs.size() == 0)
        return false;

    s = msgs.front();
    msgs.pop_front();
    return true;
}

#ifdef TEST_IRC
static String test_str{"0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"};

static void dummy_server(void *p)
{
    static int msglen[10] = {5, 10, 70, 78, 79, 80, 81, 82, 100, 120};
    static int idx = 0;
    log_msg("IRC dummy server started...\n");
    int cnt = 0;
    srand(millis());
    while (true)
    {
        P(mutex);
        static char buf[140];
        snprintf(buf, 140, "%cbla>%c-%03d %s", 254, 254, msglen[idx], test_str.substring(11, msglen[idx]).c_str());
        msgs.push_back(String{buf});
        if (++idx > 9)
            idx = 0;
        V(mutex);
        delay(500 + rand() % 3000);
    }
}
#endif

irc_t::irc_t(void)
{
#ifndef TEST_IRC
    wclient = new WiFiClientSecure;
    wclient->setInsecure();
    iclient = new IRCClient{IRC_SERVER, IRC_PORT, *wclient};
    iclient->setCallback(callback);
    iclient->setSentCallback(debugSentCallback);
    delay(500);
    // Attempt to connect
    log_msg("attempting IRC connection to %s:%d\n", IRC_SERVER, IRC_PORT);
    while (!iclient->connected())
    {
        if (iclient->connect(IRC_NICKNAME, IRC_USER))
        {
            log_msg("connected as %s, nick %s\n", IRC_USER, IRC_NICKNAME);
        }
        delay(1000);
    }
    iclient->sendRaw("JOIN " + String{IRC_CHANNEL});
#else
    xTaskCreate(dummy_server, "IRC Fake", 4000, nullptr, uxTaskPriorityGet(nullptr), &th);
#endif
}

irc_t::~irc_t()
{
#ifdef TEST_IRC
    vTaskDelete(th);
#else
    delete wclient;
    delete iclient;
#endif
}

static void _loop_irc(void)
{
#ifndef TEST_IRC
    if (!iclient->connected())
    {
        log_msg("reconnecting IRC...");
        if (iclient->connect(IRC_NICKNAME, IRC_USER))
        {
            log_msg("connected as %s, nick %s\n", IRC_USER, IRC_NICKNAME);
        }
        else
        {
            delay(200);
            return;
        }
        delay(500);
    }

    iclient->loop();
#endif
}

static std::map<String, int> nick2col;

void irc_t::annotate4irc(String &s)
{
    static int cols[8] = {253, 252, 251, 250};
    static int next_colidx = 0;
    int col;

    int idx = s.indexOf('>');
    if (idx > 0)
    {
        String nick = s.substring(0, idx);
        auto n = nick2col.find(nick);
        if (n != nick2col.end())
            col = (*n).second;
        else
            col = nick2col[nick] = cols[(++next_colidx) % 4];
        s = String{static_cast<char>(col)} + String{static_cast<char>(254)} + nick + String{static_cast<char>(254)} +
            s.substring(idx, s.length()); // wrap with ctrl char 254 -> revers on
    }
}

bool irc_t::loop(pp_drv &drv)
{
    size_t ret;
    _loop_irc();
    static String s;
    if (get_msg(s))
    {
        static char ibuf[128];
        String t;
        log_msg("IRC msg '%s' len: %d\n", s.c_str(), s.length());
        annotate4irc(s);
        int it, i = 0, e = s.length();
        while (i < e)
        {
            it = ((i + 80) < e) ? (i + 80) : e;
            String t = s.substring(i, it);
            log_msg("\t'%s'\n", t.c_str());
            if ((e - i) <= 0)
                break;
            i += 80;
            ibuf[0] = t.length();
            string2Xscii(ibuf + 1, t.c_str(), ASCII2PETSCII);
            drv.sync4write();
            log_msg("synced for write... msglen = %d...\n", ibuf[0]);
            if ((ret = drv.write(ibuf, 1)) != 1)
            {
                log_msg("len write error: %d\n", ret);
            }
            // log_msg("...wrote length...\n");
            // delay(1000);
            if ((ret = drv.write(ibuf + 1, ibuf[0])) != ibuf[0])
            {
                log_msg("data write error: %d\n", ret);
            }
            // log_msg("...and data\n");
            delay(200);
        }
    }
    if (drv.available() > 0)
    {
        // C64 user posted something
        static char buf[256];
        static int idx = 0;
        int c;
        while (drv.available() && (idx < 256))
        {
            c = drv.read();
            if (c < 0)
            {
                log_msg("read error: %d\n", c);
                continue;
            }
            if (c == 0xa0)
            {
                buf[idx] = 0;
                log_msg("idx = %d, IRC post: '%s'\n", idx, buf);
                if (strcmp(buf, "*qui*") == 0)
                    return false;
#ifndef TEST_IRC
                iclient->sendMessage(/*"pottendo"*/ IRC_CHANNEL, String{buf});
#endif
                idx = 0;
                break;
            }
            buf[idx++] = charset_p_toascii(c, true);
        }
    }
    delay(100);
    return true;
}