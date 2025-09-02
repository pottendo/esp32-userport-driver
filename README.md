# esp32-userport-driver
- [esp32-userport-driver](#esp32-userport-driver)
  - [Introduction](#introduction)
  - [Credits](#credits)
  - [Build](#build)
  - [Hardware](#hardware)
    - [Connections](#connections)
    - [Parts used](#parts-used)
  - [Software](#software)
  - [Functions](#functions)
    - [ZiModem](#zimodem)
    - [MQTT Client as 'AT' command](#mqtt-client-as-at-command)
    - [Remote controllable](#remote-controllable)
    - [IRC](#irc)
    - [Testhooks](#testhooks)
  - [Design](#design)
    - [Bugs / Missing Features](#bugs--missing-features)
  - [License](#license)
## Introduction
This project is closely connected to its counterpart, the *C64 Userport Driver* (https://github.com/pottendo/c64-userport-driver).

It enables an ESP32 based uController to act a C64 userport modem, a general purpose co-processor or a generic interface to wireless connectivity (e.g. via MQTT or IRC protocolls).

## Credits
This project is heavily based on **Bo Zimmerman's ZiModem** implementation (https://github.com/bozimmerman/Zimodem)

Valuable support has been provided by members of the **WiC64 project** (https://www.wic64.de/) - a great project, check it out!

The **Arduino ecosystem** and its various great libraries enabled a rapid prototyping approach. The project uses several libraries, such as *AutoConnect*, *MQTT*, *ArduinoIRC* to facilitate the respective functions in high quality.

Without the **Corona Virus**, this project never would have received the time to progress to the current stage. Regardless of this fact, we *hate* it... 

## Build
The project is built using VSCode with the great *PlaformIO IDE* extension installed.
Bo's modem code is directly copied (shame on me) to the repo, so it should be selfcontained and easily buildable using platformIO, embedded into VSCode.

MQTT and IRC require some credentials one needs to provide in a file *cred.h*. A template is provided as src/cred.h

WiFi configuration is bootstrapped using the great *AutoConnect* Arduino package, refer to https://hieromon.github.io/AutoConnect/index.html

## Hardware

**DISCLAIMER! <br>REBUILDING MAY DESTROY YOUR C64/ESP/other equipment! USE THIS INFORMATION AT YOUR OWN RISK** 

The needed hardware can be easily built - even I managed to do it - Knowing the sensitivity of the I/O chips (CIAs) of a C64, specific care has been taken to protect the individual I/O lines (100 Ohm resistors).
I was told by HW engineers, that using this approach, even when both side apply logical *HIGH* (5V on C64, 3.3V on ESP side) the conflict would not result into damaged CIA chips. 
During development and experimentation the SW could easily be buggy to provoke such a scenario.

Update - as of September 2025

I've built another version of the HW, see pinout below. Resistors are gone, as I've made some SW precautions to avoid writing from both side. Also the used level-shifters have 10kOhm resistors, so I assume the HW is safe, even in the rare case of SW caused writes from both ends. Still, use at your own risk.

The new HW features a parallel connector on the other side to be connected to Amiga machines - tested on a A500+. The SW for the ESP is compatible with both sides.
I found out that my 8 channel level-shifters using a _TXS0108E_ don't work, as I saw unreliable signals, especially when talking to the Amiga. So I use simple 4 channel shifters.

[ circuit diagram missing ]

### Connections 

**ATTENTION - use with care, information may be wrong**
| C64 Userport (Pin)| Amiga Parallelport (Pin)| ESP I/O | Comment |
|-------------------|-------------------------|---------|---------|
| GND (1)| GND(25) | GND | connect also to all level shifter GND's, both on high- and low-voltage side|
| 5V (2)| +5V PULLUP (14)| - | connect to level shifter high voltage side |
| RESET (3)|-|-|connected to GND via button|
| CNT1 (4)| BUSY (11)| GPIO 26 | C64 SW doesn't use this yet |
| CNT2 (6)| -| - | not used yet|
| SP1 (5)| SELECT (13) | GPIO 27 | not used yet|
| SP2 (7)| POUT (12)| GPIO 14 | indicates write |
| PC2 (8)| STROBE (1)| GPIO 25| Interrupt trigger |
| GND (A)| -| GPIO (33) | This pulls down the GPIO indicating C64 mode - DO NOT CONNECT TO GND ON THE AMIGA/PARALELPORT SIDE!!! |
| - | RESET(16) | GPIO (33) | This pulls up the GPIO indicating Amiga mode |
| FLAG (B)| ACK (10)|GPIO 15| Acknowledge |
| PB0 (C)| D0| GPIO 32| Data line |
| PB1 (D)| D1| GPIO 19| Data line |
| PB2 (E)| D2| GPIO 18| Data line |
| PB3 (F)| D3| GPIO 5| Data line |
| PB4 (H)| D4| GPIO 17| Data line |
| PB5 (J)| D5| GPIO 16| Data line |
| PB6 (K)| D6|GPIO 4| Data line |
| PB7 (L)| D7|GPIO 13| Data line |
| PA2 (M)| - |GPIO 23| specific flow control on C64|
| - | - | 3V3 | connect to level shifter low voltage side |

Note: the RESET line from the Amiga port is used to distinguish C64 from Amiga mode, as the SW needs to act slightly different.

| LCD SSD1306 (Pin)| ESP I/O | Comment|
|------------------|---------|--------|
|GND|GND||
|VCC|3V3||
|SDA|GPIO 21||
|SCL|GPIO 22||


These can be adjusted in *parport-drv.h* to your uController preference.

Power supply for the ESP is **not** provided from the C64 but needs to be provided by USB (ESP may use more than 100mA peak current). A later stage the project may provide some other PS concept.<br>

Refer to https://photos.app.goo.gl/k1wo87s1YanoMtGB7 for some pictures of version 1 HW

Refer to https://photos.app.goo.gl/Bz6kksDdvZpG9Ar46 for some pictures of version 2 HW

### Parts used
|Part|Type|Comment|
|---|---|---|
| 4x 4-Channel Bi Directional Level Shifter | 5V <-> 3.3V|
| 1x AZDelivery ESP32 D1 Mini NodeMCU  | ESP32 uController |
| 1x Youmile 6x6x6 mm Miniatur-Mikro-Taster-Tastschalter  | Reset Button|
| 1x Prototyping PCB 6x8cm
| 1x Userport Connector
| 1x Parallelport Connector

## Software

|Target | Description|URL|
|-------|------------|---|
|ESP32| ESP driver and functions|https://github.com/pottendo/esp32-userport-driver|
|C64| C64 driver and test SW|https://github.com/pottendo/c64-userport-driver|
|Amiga| Amiga driver and test SW|https://github.com/pottendo/amiga-play|

## Functions
The ESP firmware features
- 8-Parallel *high-speed* communication to the C64/Amiga
- ZiModem (default mode)
- MQTT remote controllable
- WebServer controllable
- uController functions
  - IRC interface
  - Mandelbrot Zoomer
  - Test-hooks
- WiFi Bootstrap
- OTA Update

### ZiModem
That's the primary function to support highspeed connectivity for the C64. The firmware features a *Hayes Modem* alike commandset. By design, some of the parameters (Baudrate, etc.) are not needed and therefore have no effect.<br>
Refer to https://github.com/bozimmerman/Zimodem.<br>
Phonebook down-/upload, MQTT (command *ATMQTT...*) have been added. See below *Remote controllable* via web-page.

### MQTT Client as 'AT' command
Using a terminal SW (e.g. CCGMS) ZiModem has been enhanced to accept the command 'ATMQTTcmd%topic%msg'. So far only the cmd *publish* is implemented hardwired and *cmd* is ignored.

### Remote controllable
One can use to administer the firmware via a web-page provided by the uController, by connecting to http://esp32coC64.local.
If MQTT is enabled (default), the FW subscribes to the *c64/ctrl/#* and publishes on *c64/log*. One can use any mqtt clients to send commands or receive some log-information via MQTT.

### IRC
Basic IRC connectivity is provided - so far not very flexible. May be added later.

### Testhooks
These functions are used to test certain functionalities of the driver.

## Design
3 modes of operation
- ZiModem (default)<br>
  Acts as a modem, receives characters from the C64 or - if connected to a BBS bidirectional passes through traffic. ASCII/PETSCII translation is provided, where needed.
- CoProcesser<br>
  Mostly testfunctions are implemented. Noticably a *mandelbrot set* zoomer has been added, to visualize GFX using the CoProcessor.
  Commands are sent from the C64 using 4 byte strings and arguments, where needed.
- IRC<br>
  Manages the connection and ASCII/PETSCII conversion to some IRC server. Initiated by co-processor command "IRC_".

### Bugs / Missing Features
- IRC not as flexible as needed
- More complete MQTT cmds
- Web administration improvements, extensions

## License

Refer to: https://github.com/pottendo/esp32-userport-driver/blob/master/LICENSE

This is a fun project - don't expect any *real* support if you need any help. I'll do my best to respond, though, as my time permits.
(C) 2022, pottendo productions
