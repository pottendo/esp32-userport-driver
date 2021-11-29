#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <IRCClient.h>
#include <list>
#include "irc.h"
#include "logger.h"
#include "cred.h"
#include "misc.h"
#include "pet2asc.h"

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
    //log_msg(ircMessage.original + '\n');
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
static void dummy_server(void *p)
{
    log_msg("IRC dummy server started...\n");
    int cnt = 0;
    srand(millis());
    while (true)
    {
        P(mutex);
        msgs.push_back(String{"Dummy IRC message: @_"} + String{cnt++});
        V(mutex);
        delay(500 + rand() % 3000);
    }
}
#endif

irc_t::irc_t(void)
{
#ifndef TEST_IRC
    if (!WiFi.isConnected())
    {
        WiFi.begin(ssid, password);
        while (WiFi.status() != WL_CONNECTED)
        {
            log_msg(".");
            delay(500);
        }
        WiFi.printDiag(Serial);
    }
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

void irc_t::annotate4irc(char *s, int l)
{

    for (int i = 0; i < l; i++)
    {   
        char c = s[i];
        if ((c == ' ') || (c == '>'))
            break;
        if ((c >= 'A') && (c <= 'Z'))
            s[i] |= 0x80;
        if ((c >= 'a') && (c <= 'z'))
            s[i] |= 0x80;
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
        int it, i = 0, e = s.length();
        while (i < e)
        {
            it = ((i + 77) < e) ? (i + 77) : e;
            String t = s.substring(i, it);
            log_msg("\t'%s'\n", t.c_str());
            if ((e - i) <= 0)
                break;
            i += 77;
            ibuf[0] = t.length();
            string2petscii(ibuf + 1, t.c_str());
            annotate4irc(ibuf+1, ibuf[0]);
            drv.sync4write();
            //log_msg("synced for write... writing %d byte...\n", ibuf[0] + 1);
            if ((ret = drv.write(ibuf, 1)) != 1)
            {
                log_msg("len write error: %d\n", ret);
            }
            //log_msg("...wrote length...\n");
            //delay(1000);
            if ((ret = drv.write(ibuf + 1, ibuf[0])) != ibuf[0])
            {
                log_msg("data write error: %d\n", ret);
            }
            //log_msg("...and data\n");
            delay(100);
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
                iclient->sendMessage("pottendo" /*IRC_CHANNEL*/, String{buf});
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