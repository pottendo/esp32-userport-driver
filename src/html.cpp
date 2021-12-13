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
#include <WebServer.h>
#include <WiFi.h>
#include <WebSocketsServer.h>
#include <list>
#include "html.h"
#include "logger.h"
#include "misc.h"
#include "mqtt.h"
#include "cmd-if.h"
#include "phonebook.h"

static AutoConnect *portal;
static WebServer *server;
static WebSocketsServer webSocket(81);
static SemaphoreHandle_t web_mutex_clients = xSemaphoreCreateMutex();
static SemaphoreHandle_t web_mutex_cmds = xSemaphoreCreateMutex();
static std::list<uint8_t> clients;
static std::list<String, Mallocator<String>> web_cmds;
static bool mqtton = false;

const char esp32coC64_main[] PROGMEM = R"raw(
{
  "title" : "<b>ESP32 Coprocessor for C64</b> - Control Page",
  "uri" : "/",
  "menu" : false,
  "element" : [
    {
        "name": "tablestyle",
        "type": "ACStyle",
        "value": "table.style{font-family:arial,sans-serif;border-collapse:collapse;width:100%;color:black;}table.style td,table.style th{border:1px solid #dddddd;text-align:left;padding:8px;}table.style tr:nth-child(even){background-color:#dddddd;}"
    },
    {
        "name": "Table",
        "type": "ACElement",
        "value": ""
    },
    {
        "name" : "newline",
        "type" : "ACElement",
        "value" : "<hr>"    
    },
    {
      "name" : "LastUpdate",
      "type" : "ACText",
      "value" : "(c) 2021 pottendo productions"
    },
    {
      "name" : "Bottom",
      "type" : "ACText",
      "value" : " (c) 2021 pottendo productions"
    },
    {
        "name" : "BRefresh",
        "type" : "ACSubmit",
        "value": "Refresh",
        "uri": "/"
    }
  ]
}
)raw";

// document.getElementById("Text2").value = document.getElementById("Text1").value;
const char *scButtonCB = R"(
<script>
  var gateway = `ws://${window.location.hostname}:81`;
  var websocket;
  window.addEventListener('load', onLoad);
  function initWebSocket() {
    console.log('Trying to open a WebSocket connection...');
    websocket = new WebSocket(gateway);
    websocket.onopen    = onOpen;
    websocket.onclose   = onClose;
    websocket.onmessage = onMessage; // <-- add this line
  }
  function onOpen(event) {
    console.log('Connection opened');
  }
  function onClose(event) {
    console.log('Connection closed');
    setTimeout(initWebSocket, 2000);
  }
  function onMessage(e) {
    //console.log('onMessage received: ' + e.data);
    if (e.data == 'Connected') return;
    var str = e.data.split("#");
	var mode = str[0];
	var detail = str[1]
	if ((mode == 'CoRoutine') || (mode == 'ZiModem'))
	{
		//console.log(mode + '/' + detail);
		document.getElementById('uCmode').innerHTML = mode;
		document.getElementById('mdetail').innerHTML = detail;
		return;
	}
	if (mode == 'MQTT')
	{
		//console.log('mqtt status update...' + detail + '-' + document.getElementById('MqttStatus').innerHTML + '-');
		if (uniqueId == "connected")
			document.getElementById('MqttStatus').innerHTML = "connected";
		else
		{
			document.getElementById('MqttStatus').innerHTML = uniqueId;
			document.getElementById("mqttcb").checked = false;
		}
		return;
	}
	console.log('unknwon mode: ' + mode);
  }
function onLoad(event) {
	if (websocket == null) {
	    initWebSocket();
	}
}
function _btnCB(cbox) {
  var xhr = new XMLHttpRequest();
  var str = cbox.id; //cbox.id.split("|");
  var cmd = "/update?oifid=" + str + "&state=";
  //console.log("cmd = " + cmd + cbox.value);
  xhr.open("GET", cmd + cbox.value, true);
  xhr.send();
}
function _MqttCB(cbox) {
  var xhr = new XMLHttpRequest();
  var cmd = "/mqtt?mqtt=";
  if(cbox.checked){ xhr.open("GET", cmd + "on", true); }
  else { xhr.open("GET", cmd + "off", true); }
  xhr.send();
}
function _MqttCMT () {
	const br = document.querySelector("#MqttSv");
  	//console.log("mqtt broker val = " + br.value);
	var xhr = new XMLHttpRequest();
  	var cmd = "/mqtt?mqtt=sv&server=" + br.value;
	xhr.open("GET", cmd, true);
	xhr.send();
	document.getElementById("mqttcb").checked = false;
}
function _CBuCmode(mode) {
	//console.log("uC mode clicked: " + mode);
	var xhr = new XMLHttpRequest();
  	var cmd = "/uCmode?mode=" + mode;
	xhr.open("GET", cmd, true);
	xhr.send();
}
</script>
)";

// The handler called before HTML generating
String initialize(AutoConnectAux &aux, PageArgument &args)
{
	AutoConnectText &update = aux.getElement<AutoConnectText>("LastUpdate");
	struct tm timeinfo;
	char buf[64];
	if (!getLocalTime(&timeinfo))
	{
		log_msg("Failed to obtain time.\n");
		snprintf(buf, 64, "<unknown>");
	}
	else
		strftime(buf, 64, "%a, %b %d %Y %H:%M:%S", &timeinfo);
	update.value = String(buf) + " FreeMem: " + String(ESP.getFreeHeap());

	static const String header{"<table id=\"Table\" class='style'><tr><th>CoProc Mode</th><th>Status</th></tr>"};
	String uCstatus{"<tr><td><label id=\"uCmode\">" + get_mode_str() + "</label></td><td><label id=\"mdetail\">unknown detail</label></tr>"};
	static const String tail{"</table>"};
	static const String uCm{"<br><b>Coproc Mode</b><br>"
							"<input type=\"radio\" name=\"uCmode\" id=\"uCmode_1\" value=\"CoRoutines\" checked onclick=\"_CBuCmode(1)\"><label for=\"uCmode_1\">CoRoutines</label><br>"
							"<input type=\"radio\" name=\"uCmode\" id=\"uCmode_2\" value=\"ZiModem\" onclick=\"_CBuCmode(2)\"><label for=\"uCmode_2\">ZiModem</label><br>"};

	static const String mqttcb_pre{"<br><b>MQTT Control</b><br><input type=\"checkbox\" id=\"mqttcb\" name=\"mqttcbox\" onchange=\"_MqttCB(this)\""};
	String mqtt_checked = mqtton ? " checked" : "";
	static const String mqttcb_post{"><label for=\"mqttbox\">MQTT Output</label>"};
	static const String mqttcommit{"<input type=\"button\" name=\"MqttCmt\" value=\"Set Mqtt Broker\" onclick=\"_MqttCMT('')\">"};
	static const String mqttbrinput{"<input type=\"text\" id=\"MqttSv\" name=\"MqttSv\" placeholder=\"my.favorite-broker.org\""};
	String mqttval = String(" value=\"") + mqtt_get_broker() + "\">";
	String mqttstatus{"<label id=\"MqttStatus\">" + mqtt_get_conn_stat() + "</label>"};
	String pb = PhoneBookEntry::dumpHTMLPhonebook();

	return String(scButtonCB) + header +
		   uCstatus + tail +
		   uCm +
		   pb +
		   mqttcb_pre + mqtt_checked + mqttcb_post + mqttcommit + mqttbrinput + mqttval + mqttstatus + "<br>";
}

void onMqttCB(void)
{
	// log_msg("MQTT toggle callback: " + server->arg("mqtt"));
	String new_broker{""};
	if (server->arg("mqtt") == "sv")
	{
		new_broker = server->arg("server");
		mqtt_set_broker(new_broker);
		mqtton = false; // explicit check to activate mqtt connection
	}
	else
	{
		mqtton = (server->arg("mqtt") == "on") ? true : false;
		// mqtt_set(mqtton);
	}
	server->send(200, "text/plain", "");
}

void onuCmode(void)
{
	// log_msg("uCmode callback: %s\n", server->arg("mode").c_str());
	String arg = server->arg("mode");
	uCmode_t t = static_cast<uCmode_t>(arg.toInt());
	if (t != get_mode())
	{
		log_msg("mode changed from %d to %d\n", get_mode(), t);
		change_mode(t);
	}
	server->send(200, "text/plain", "");
}

void onDownload(void)
{
	if (SPIFFS.exists("/zphonebook.txt"))
	{
		File f = SPIFFS.open("/zphonebook.txt", "r");
		server->streamFile<File>(f, "text/plain");
	}
	else
		server->send(200, "text/html", "<H1>Failed to open file for reading</h1>");
}

void web_send_cmd(String cmd)
{
	if (!server)
		return;
	P(web_mutex_cmds);
	if (web_cmds.size() > 10) // protect against mem overun and discard oldest 5 cmds
	{
		for (auto i = 0; i < 5; i++)
			web_cmds.pop_front();
	}
	web_cmds.push_back(cmd);
	V(web_mutex_cmds);
	// mqtt_publish_mt("/web-command", cmd);
}

void setup_html(AutoConnect *p, WebServer *s)
{
	portal = p;
	server = s;
	portal->load(FPSTR(esp32coC64_main));
	portal->aux("/")->on(initialize, AC_EXIT_AHEAD);
	s->on("/mqtt", onMqttCB);
	s->on("/uCmode", onuCmode);
	s->on("/zphonebook.txt", onDownload);
}

void setup_websocket(void)
{
	webSocket.begin();
	webSocket.onEvent([](uint8_t num, WStype_t type, uint8_t *payload, size_t length)
					  {
						  switch (type)
						  {
						  case WStype_DISCONNECTED:
							  //log_msg("[%u] Disconnected!\n", num);
							  P(web_mutex_clients);
							  clients.remove(num);
							  V(web_mutex_clients);
							  break;
						  case WStype_CONNECTED:
						  {
							  IPAddress ip = webSocket.remoteIP(num);
							  //log_msg("[%u] Connected from %s\n", num, ip.toString().c_str());
							  P(web_mutex_clients);
							  clients.push_back(num);
							  V(web_mutex_clients);
						  }
						  break;
						  case WStype_TEXT:
							  log_msg("[%u] get Text: %s\n", num, payload);
							  // send some response to the client
							  //webSocket.sendTXT(num, "message here");
							  break;
						  default:
							  break;
						  } });
}

void loop_websocket(void)
{
	webSocket.loop();
	P(web_mutex_cmds);
	if (web_cmds.size() == 0)
	{
		V(web_mutex_cmds);
		return;
	}
	String &c = web_cmds.front();
	V(web_mutex_cmds);
	P(web_mutex_clients);
	for (auto clt : clients)
		webSocket.sendTXT(clt, c);
	V(web_mutex_clients);
	P(web_mutex_cmds);
	web_cmds.pop_front();
	V(web_mutex_cmds);
}