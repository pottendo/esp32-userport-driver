# esp32-userport-driver
- [esp32-userport-driver](#esp32-userport-driver)
  - [Introduction](#introduction)
  - [Credits](#credits)
  - [Build](#build)
  - [Hardware](#hardware)
    - [Connections](#connections)
    - [Parts used](#parts-used)
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

[ circuit diagram missing ]

### Connections 

**ATTENTION - use with care, information may be wrong**
| C64 Userport | Level Shifter | Level Shifter| ESP I/O | Comment |
|--------------|------|-----|--|-|
| GND (N)| LvSh A GND|||
| 5V (2)| LvSh A VB|LvSh B VB||
| FLAG (B)| LvSh A B8| LvSh A A8| GPIO 15|
| PB0 (C)| LvSh A B7| LvSh A A7|GPIO 32|
| PB1 (D)| LvSh A B6| LvSh A A6| GPIO 19|
| PB2 (E)| LvSh A B5| LvSh A A5| GPIO 18|
| PB3 (F)| LvSh A B4| LvSh A A4| GPIO 5|
| PB4 (H)| LvSh A B3| LvSh A A3| GPIO 17|
| PB5 (J)| LvSh A B2| LvSh A A2| GPIO 16|
| PB6 (K)| LvSh A B1| LvSh A A1| GPIO 4|
| GND (1)| | LvSh B GND | GND|
| PB7 (L)| LvSh B B8| LvSh B A8| GPIO 13|
| PA2 (M)| LvSh B B7| LvSh B A7| GPIO 23|
| PC2 (8)| LvSh B B6| LvSh B A5| GPIO 25|
| SP2 (7)| LvSh B B5| LvSh B A5| GPIO 27|
| - | LvSh A OE |LvSh B OE| GPIO 26 | Output enable control|
|-| LvSh A VA | LvSh B VA| 3V3
| CNT2 (6)| LvSh B B4| LvSh B A4| -| not used|
| SP1 (5)| LvSh B B3| LvSh B A3| -|not used|
| CNT1 (4)| LvSh B B2| LvSh B A2| -|not used|
| ATN (9)| LvSh B B1| LvSh B A1| -|not used|
| RESET(3)| ||| connected to GND via button

These can be adjusted in *parport-drv.h* to your uController preference.

Power supply for the ESP is **not** provided from the C64 but needs to be provided by USB (ESP may use more than 100mA peak current). A later stage the project may provide some other PS concept.<br>

Refer to https://photos.app.goo.gl/k1wo87s1YanoMtGB7 for some pictures.

### Parts used
|Part|Type|Comment|
|---|---|---|
|2 x AZDelivery TXS0108E| 5V <-> 3.3V Bidirectional 8 Channel Level Shifter |
| 1 x AZDelivery ESP32 D1 Mini NodeMCU  | ESP32 uController |
| 1 x Youmile 6x6x6 mm Miniatur-Mikro-Taster-Tastschalter  | Reset Button|
| 16x 100Ohm Resistor | Protection|
| Sockets||
| 1 x Prototyping PCB 6x8cm
| 1 x Userport Connector

## Functions
The ESP firmware features
- 8-Parallel *high-speed* communication to the C64
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
