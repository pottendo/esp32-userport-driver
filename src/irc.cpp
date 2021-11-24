#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <IRCClient.h>
#include <list>
#include "irc.h"
#include "logger.h"
#include "cred.h"

static IRCClient *iclient;
static WiFiClientSecure *wclient;

static std::list<String> msgs;
void callback(IRCMessage ircMessage)
{
    // PRIVMSG ignoring CTCP messages
    if (ircMessage.command == "PRIVMSG" && ircMessage.text[0] != '\001')
    {
        String message(ircMessage.nick + ">" + ircMessage.text);
        log_msg(message + '\n');
        msgs.push_back(message);
        return;
    }
    //log_msg(ircMessage.original + '\n');
    msgs.push_back(ircMessage.original);
}

void debugSentCallback(String data)
{
    log_msg(data + '\n');
}

void send_msg(String s)
{
    iclient->sendRaw(s);
}

bool irc_get_msg(String &s)
{
    if (msgs.size() == 0)
        return false;

    s = msgs.front();
    msgs.pop_front();
    return true;
}

void setup_irc(void)
{
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
    iclient->setCallback(callback);
    iclient->setSentCallback(debugSentCallback);
    iclient->sendRaw("JOIN " + String{IRC_CHANNEL});
}

void loop_irc(void)
{
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
}