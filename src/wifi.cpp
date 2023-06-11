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
#include <WebServer.h>
//#include <WebSocketsServer.h>
#include <AutoConnectOTA.h>
#include <AutoConnect.h>
#include <ESPmDNS.h>
#include <PageBuilder.h>
#include "logger.h"
#include "wifi.h"
#include "html.h"
#include "misc.h"

#ifdef MQTT
#include "mqtt.h"
#endif

// Wifi gets his own task
static TaskHandle_t wifi_th;

const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 1 * 3600;
const int daylightOffset_sec = 0;

static WebServer ip_server;
static AutoConnect portal(ip_server);
static AutoConnectConfig config;
static PageBuilder page; // entry page
// extern WebSocketsServer webSocket;

static const String hostname = "esp32coC64";

static void printLocalTime()
{
    struct tm timeinfo;
    int err = 10;
    while (!getLocalTime(&timeinfo) && err--)
    {
        log_msg("Failed to obtain time - " + String(10 - err));
        delay(500);
    }
    if (err <= 0)
    {
        // tried for 10s to get time, better reboot and try again
        log_msg("failed to obtain time... consider rebooting.\n");
        delay(250);
        return;
    }
    char buf[64];
    strftime(buf, 64, "local time: %a, %b %d %Y %H:%M:%S\n", &timeinfo);
    log_msg(buf);
}

static void wifi_task(void *p)
{
    log_msg("Wifi task created with priority %d\n", uxTaskPriorityGet(nullptr));

    while (true)
    {
        loop_wifi();
        delay(50);
    }
}

void setup_wifi(void)
{
    log_msg("Setting up Wifi...\n");
    config.ota = AC_OTA_BUILTIN;
    config.hostName = hostname;
    config.beginTimeout = 25000; /* try 25s to connect */
    // config.preserveAPMode = false;
    // config.autoRise = true;
    // config.immediateStart = false;
    portal.config(config);
    setup_html(&portal, &ip_server);
    if (portal.begin())
    {
        log_msg("WiFi connected: " + WiFi.localIP().toString() + '\n');
    }
    // Prepare dynamic web page
    setup_websocket();
    log_msg(String(hostname) + ", " +
            WiFi.localIP().toString() + ", GW = " +
            WiFi.gatewayIP().toString() + ", DNS = " +
            WiFi.dnsIP().toString() + '\n');
    // init and get the time
    log_msg("Setting up local time...\n");
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    printLocalTime();

    log_msg("Setting hostname to '" + hostname + "'\n");
    if (!MDNS.begin(hostname.c_str()))
    {
        log_msg("Setup of DNS for fcc failed.\n");
    }
#ifdef MQTT
    setup_mqtt();
#endif
    setup_slip(WiFi);

    xTaskCreate(wifi_task, "Wifi-task", 4096, nullptr, uxTaskPriorityGet(nullptr), &wifi_th);
}

void loop_wifi(void)
{
    static int recon = 0;
    if (!WiFi.isConnected())
    {
        log_msg("Wifi not connected ... strange\n");
        delay(500);
        WiFi.reconnect();
        if ((++recon) > 20)
        {
            log_msg("Reconnect failed too often... rebooting.\n");
            delay(500);
            ESP.restart();
        }
        return;
    }
    loop_websocket();
    portal.handleClient();
    loop_log();
#ifdef MQTT
    loop_mqtt();
#endif

    delay(50);
}