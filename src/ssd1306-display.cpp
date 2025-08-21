
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "logger.h"
#include "ssd1306-display.h"

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32

void setup_display(void)
{
    log_msg("setting up display...\n");
    Wire.begin(21, 22); // SDA, SCL pins for I2C
    Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
    if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
        log_msg("SSD1306 allocation failed\n");
        Wire.end();
        return;
    }
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.print(F("ESP32 Userport Driver"));
    display.setCursor(0, 10);
    display.drawLine(0, 10, SCREEN_WIDTH - 1, SCREEN_HEIGHT -1, SSD1306_WHITE);
    display.drawLine(0, SCREEN_HEIGHT -1, SCREEN_WIDTH - 1, 10, SSD1306_WHITE);
    display.display();
}
