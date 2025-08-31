#ifndef __SSD1306_DISPLAY_H__
#define __SSD1306_DISPLAY_H__
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <vector>
#include <string>
#include <cstdarg>
void setup_display(void);

class RollingText {
public:
    RollingText(Adafruit_SSD1306 &d, uint16_t fg = SSD1306_WHITE, uint16_t bg = SSD1306_BLACK)
        : display(d), fgColor(fg), bgColor(bg) {
        lineHeight = 8; // default 8 px for built-in font
        maxLines   = display.height() / lineHeight;
    }

    void println(const char *text) {
        lines.push_back(std::string(text));
        if (lines.size() > maxLines) {
            lines.erase(lines.begin()); // remove oldest line
        }
        redraw();
        startScroll();
    }

    void print(const char *text) {
        buffer += text;
    }

    void flushLine() {
        if (!buffer.empty()) {
            println(buffer.c_str());
            buffer.clear();
        }
    }

    void printf(const char *fmt, ...) {
        char buf[128];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        println(buf);
    }

    void clear() {
        lines.clear();
        buffer.clear();
        display.clearDisplay();
        display.display();
    }
    void orientation(int o) {
        display.setRotation(o);
        redraw();
    }
private:
    Adafruit_SSD1306 &display;
    uint16_t fgColor, bgColor;
    int lineHeight;
    int maxLines;
    std::vector<std::string> lines;
    std::string buffer;

    void redraw() {
        display.clearDisplay();
        int y = 0;
        for (auto &line : lines) {
            display.setCursor(0, y);
            display.setTextColor(fgColor, bgColor);
            display.print(line.c_str());
            y += lineHeight;
        }
        display.display();
    }

    void startScroll() {
        // Start vertical scrolling (SSD1306 hardware feature)
        //display.startscrolldiagright(0x00, 0x0F); // scroll entire screen
    }

    void stopScroll() {
        display.stopscroll();
    }
};

extern RollingText *lcd;
#endif // __SSD1306_DISPLAY_H__