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
#include <MQTT.h>
#include <list>
#include <ESPmDNS.h>
#include <WiFiClientSecure.h>
#include "logger.h"

#include "mqtt.h"
#include "cmd-if.h"

static myMqtt *mqtt_connection;
static SemaphoreHandle_t mqtt_mutex; /* ensure exclusive access to mqtt client lib */
static SemaphoreHandle_t mqtt_mutex_mt;
typedef struct
{
    String t;
    String m;
    myMqtt *mqtt;
    int qos;
} mqpub_t;
static std::list<mqpub_t> mqtt_queue;
static std::list<myMqtt *> mqtt_connections;

static const char *client_id = "c64esp32modem"; /* identify c64 uniquely on mqtt */
static void mqtt_upstream(String &, String &);

/* embedded device name local broker on network*/
#define MQTT_LOCAL MQTT_LOG

#include "cred.h"

#ifndef MQTT_CRED
#define MQTT_LOG "invalid mqtt-broker set in cred.h"
#define MQTT_LOG_USER ""
#define MQTT_LOG_PW ""
#endif

void setup_mqtt(void)
{
#ifndef MQTT_CRED
    log_msg("mqtt credentials not set in 'cred.h' -> disabling mqtt support.\n");
    return;
#endif
    mqtt_mutex = xSemaphoreCreateMutex();
    V(mqtt_mutex);
    mqtt_mutex_mt = xSemaphoreCreateMutex();
    V(mqtt_mutex_mt);
    mqtt_connection = new myMqttLocal(client_id, MQTT_LOG, mqtt_upstream, "local");
}

void loop_mqtt()
{
#ifndef MQTT_CRED
    return;
#endif
    std::for_each(mqtt_connections.begin(), mqtt_connections.end(),
                  [](myMqtt *c)
                  {
                      if (c->connected())
                          c->loop();
                      else
                          c->reconnect();
                  });
    P(mqtt_mutex_mt);
    if (mqtt_queue.size() == 0)
    {
        V(mqtt_mutex_mt);
        return;
    }
    mqpub_t cmd = mqtt_queue.front();
    V(mqtt_mutex_mt);
    mqtt_publish(cmd.t, cmd.m, cmd.mqtt, cmd.qos);
    P(mqtt_mutex_mt);
    mqtt_queue.pop_front();
    V(mqtt_mutex_mt);
}

String mqtt_get_broker(void)
{
    return String{MQTT_LOG};
}

void mqtt_set_broker(String broker)
{
    log_msg("new broker requested: " + broker + '\n');
}

String mqtt_get_conn_stat(void)
{
    String res;
    if (mqtt_connection)
        res = mqtt_connection->get_conn_stat();
    else
        res = "unkown";
    return res;
}

ZResult2 mqtt_command(String &full_cmd)
{
    static char buf[256];
    String cmd = full_cmd.substring(6, full_cmd.length());  // full cmd: 'ATMQTTcmd&topic&msg
    int i1 = cmd.indexOf('&');
    int i2 = cmd.indexOf('&', i1 + 1);
    string2Xscii(buf, cmd.substring(0, i1).c_str(), PETSCII2ASCII);
    String c = String{buf};
    string2Xscii(buf, cmd.substring(i1 + 1, i2).c_str(), PETSCII2ASCII);
    String topic = String{buf};
    string2Xscii(buf, cmd.substring(i2 + 1, cmd.length()).c_str(), PETSCII2ASCII);
    String m = String{buf};
    log_msg("mqtt command issued: '%s' topic: '%s', msg: '%s'\n", c.c_str(), topic.c_str(), m.c_str());
    mqtt_publish(topic, m);

    return ZOK2;
}

void mqtt_publish(String topic, String msg, myMqtt *c, int qos)
{
    P(mqtt_mutex); /* ensure mut-excl acces into MQTTClient library */
    myMqtt *client = (c ? c : mqtt_connection);
    client->publish(topic, msg, qos);
    V(mqtt_mutex);
}

void mqtt_publish_mt(String topic, String msg, myMqtt *c, int qos)
{
    P(mqtt_mutex_mt);
    mqtt_queue.push_back({topic, msg, c, qos});
    V(mqtt_mutex_mt);
}

void mqtt_P(void)
{
    P(mqtt_mutex);
}

void mqtt_V(void)
{
    V(mqtt_mutex);
}

myMqtt *mqtt_register_logger(void)
{
#ifdef MQTT_LOG_LOCAL
    return new myMqttLocal(client_id, MQTT_LOG, nullptr, "log broker", 1883, MQTT_LOG_USER, MQTT_LOG_PW);
#else
    return new myMqttSec(client_id, MQTT_LOG, nullptr, "log broker", 8883, MQTT_LOG_USER, MQTT_LOG_PW);
#endif
}

/* class myMqtt broker */
myMqtt::myMqtt(const char *id, const char *sv, upstream_fn f, const char *n, const char *user, const char *pw)
    : id(id), server(sv), user(user), pw(pw)
{
    mutex = xSemaphoreCreateMutex();
    name = (n ? n : id);
    if (!f)
        up_fn = [](String &t, String &p)
        { log_msg(String(client_id) + " received: " + t + ":" + p); };
    else
        up_fn = f;
    V(mutex);

    mqtt_connections.push_back(this);
    log_msg(String(name) + " mqtt client created.\n");
}

void myMqtt::loop(void)
{
    //    if (!connected())
    //        reconnect();
    P(mutex);
    client->loop();
    V(mutex);
}

String myMqtt::get_conn_stat(void)
{
    String res;
    switch (conn_stat)
    {
    case CONN:
        res = "connected";
        break;
    case NO_CONN:
        res = "not connected";
        break;
    case RECONN:
        res = "reconnecting";
        break;
    default:
        res = "unknown";
        break;
    }
    return res;
}

bool myMqtt::connected(void)
{
    bool r;
    P(mutex);
    r = client->connected();
    conn_stat = (r ? CONN : NO_CONN);
    V(mutex);
    return r;
}

void myMqtt::reconnect(void)
{
    reconnect_body();
}

void myMqtt::cleanup(void)
{
}

void myMqtt::reconnect_body(void)
{

    // Loop until we're reconnected
    if (client->connected())
    {
        set_conn_stat(CONN);
        log_msg("reconnect_body, client is connected - shouldn't happen here\n");
    }
    if ((millis() - last) < 2500)
    {
        delay(500);
        return;
    }
    if (connection_wd == 0)
        connection_wd = millis();
    last = millis();
    reconnects++;
    log_msg("mqtt broker not connected, attempting MQTT connection..." + String(reconnects) + '\n');

    // Attempt to connect
    if (connect())
    {
        reconnects = 0;
        connection_wd = 0;
        log_msg("mqtt connected to %s.\n", server);
        P(mutex);
        client->subscribe("c64/ctrl/#", 0);
        V(mutex);
        mqtt_publish("c64/log", "C64 resurrected - aloha...", this);
        set_conn_stat(CONN);
    }
    else
    {
        unsigned long t1 = (millis() - connection_wd) / 1000;
        log_msg(String("Connection lost for: ") + String(t1) + "s...");
        if (t1 > 300)
        {
            log_msg("mqtt reconnections failed for 5min... rebooting\n");
            delay(250);
            ESP.restart();
        }
    }
}

void myMqtt::publish(String &t, String &p, int qos)
{
    P(mutex);
    if (!client->connected() ||
        !client->publish(t, p, false, qos))
    {
        log_msg(String(name) + " mqtt failed, rc=" + String(client->lastError()) + ", discarding: " + t + ":" + p);
    }
    V(mutex);
}

void myMqtt::register_callback(upstream_fn fn)
{
    P(mutex);
    client->onMessage(fn);
    V(mutex);
}

myMqttSec::myMqttSec(const char *id, const char *sv, upstream_fn fn, const char *n, int port, const char *user, const char *pw)
    : myMqtt(id, sv, fn, n, user, pw)
{
    client = new MQTTClient{256};
    client->begin(server, port, net);
    client->onMessage(up_fn);
    delay(50);
}

bool myMqttSec::connect(void)
{
    P(mutex);
    if (!client->connected() &&
        !client->connect(id, user, pw))
    {
        log_msg(String(name) + " mqtt connect failed, rc=" + String(client->lastError()));
        V(mutex);
        return false;
    }
    V(mutex);
    return true;
}

myMqttLocal::myMqttLocal(const char *id, const char *sv, upstream_fn fn, const char *n, int port, const char *user, const char *pw)
    : myMqtt(id, sv, fn, n, user, pw)
{
    client = new MQTTClient{256}; /* default msg size, 128 */
    /*
    log_msg("trying to MDNS query %s\n", server);
    IPAddress sv = MDNS.queryHost(server);
    Serial.println(sv);
    if (sv == (IPAddress) INADDR_NONE)
    {
        log_msg(String(name) + " mqtt broker '" + server + "' not found.");
        return; // XXX throw exception here
    }
    */
    client->begin(server, port, net);
    client->onMessage(up_fn);
    reconnect();
    delay(50);
}

char *myMqttLocal::get_id(void)
{
    snprintf(id_buf, 16, "%s-%04d", id, nr++);
    return id_buf;
}
bool myMqttLocal::connect(void)
{
    P(mqtt_mutex);
    if (!client->connected() &&
        !client->connect(id, "hugo", "schrammel"))
    {
        log_msg(String(name) + " mqtt connect failed, rc=" + String(client->lastError()));
        V(mqtt_mutex);
        return false;
    }
    V(mqtt_mutex);
    return true;
}

static void mqtt_upstream(String &t, String &payload)
{
    if (t == "c64/ctrl")
        mqtt_cmd(payload);
    else
        log_msg("mqtt unsupported topic %s\n", t.c_str());
}
