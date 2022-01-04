# esp32-userport-driver

This project is closely connected to its counterpart, the *C64 Userport Driver* (https://github.com/pottendo/c64-userport-driver).

It enables an ESP32 based uController to act a C64 userport modem, a general purpose co-processor or a generic interface to wireless connectivity (e.g. via MQTT or IRC protocolls).

## Credits
This project is heavily based on *Bo Zimmerman's* **ZiModem** implementation (https://github.com/bozimmerman/Zimodem)

Valuable support has been provided by members of the *WiC64* project (https://www.wic64.de/) - a great project, check it out!

The *Arduino ecosystem* and its various great libraries enabled a rapid prototyping approach. The project uses several libraries, such as *AutoConnect*, *MQTT*, *ArduinoIRC* to facilitate the respective functions in high quality.

Without the *Corona Virus*, this project never would have received the time to progress to the current stage. Regardless of this fact, we *hate* it... 

## Build
The project is built using VSCode with the great *PlaformIO IDE* extension installed.
Bo's modem code is directly copied (shame on me) to the repo, so it should be selfcontained and easily buildable using platformIO, embedded into VSCode.

MQTT requires some credentials one needs to provide in a file mqtt-cred.h

WiFi configuration is bootstrapped using the great *AutoConnect* Arduine package, refer to https://hieromon.github.io/AutoConnect/index.html

## Hardware
The needed hardware can be easily built - even I managed to do it - Knowing the sensitivity of the I/O chips (CIAs) of a C64 specific care has been taken to protect the individual I/O lines (100 Ohm resistors). 
I was told by HW engineers, that using this approach, even when both side apply logical *HIGH* (5V on C64, 3.3V on ESP side) the conflict would not result into damaged CIA chips. 
During development and experimentation the SW could easily be buggy to provoke such a scenario.

[ circuit & pics to be added here ]

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
Refer to https://github.com/bozimmerman/Zimodem.
Phonebook down-/upload, MQTT (command *ATMQTT...*) have been added.

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
  Acts as a modem, receives characters from the C64 or - if connected to a BBS bidirectional passes through traffic. ASCII/PETSCII translation is provided, where needed
- CoProcesser<br>
  Mostly testfunctions are implemented. Noticably a *mandelbrot set* zoomer has been added, to visualize GFX using the CoProcessor
- IRC<br>
  Manages the connection and ASCII/PETSCII conversion to some IRC server

