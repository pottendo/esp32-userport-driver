#include <Arduino.h>
#include "parport-drv.h"
#include "misc.h"
#include "logger.h"
#include "co-routines.h"
#include "irc.h"
#include "pet2asc.h"

#define TEST_FW
#define EXT80COLS
#define ZIMODEM

pp_drv drv;
static char buf[MAX_AUX];
static int ret;

int process_cmd(char *cmd)
{
    char *cmd_buf = cmd;
    while (*cmd_buf)
    {
        for (auto cr : cr_base::coroutines)
        {
            if (cr->match(cmd_buf, &drv))
                return 1;
        }
        strncpy(cmd_buf, cmd_buf + 1, 3);
        int ret = drv.read(cmd_buf + 3, 1);
        if (ret != 1)
        {
            log_msg("process_cmd, read error: %d\n", ret);
            break;
        }
        cmd_buf[4] = '\0'; // probably not needed.
    }
    log_msg("Unknown command: '%s'\n", cmd);

    return 0;
}

static uint8_t petcii_fix_dupes(uint8_t c)
{
    if ((c >= 0x60) && (c <= 0x7f))
    {
        return ((c - 0x60) + 0xc0);
    }
    else if (c >= 0xe0)
    {
        return ((c - 0xe0) + 0xa0);
    }
    return c;
}

uint8_t charset_p_topetcii(uint8_t c)
{
    //log_msg("before conv: '%c'/0x%02x\n", (isPrintable(c)?c:'~'), c);
    /* map ascii to petscii */
    if (c == '\n')
        return 0x0d; /* petscii "return" */
    else if (c == '\r')
        return 0x0a;
    else if ((c == '`') || (c == '\''))
        return 0x27; /* petscii "'" */
    else if (c == '@')
        return 0x40;
    else if (c == '_')
        return 0xa4;        // was 0x64
    else if ((c == '{') || (c == '['))
        return 0x5b;        // was 0x1b
    else if ((c == '}') || (c == ']'))
        return 0x5d;        // was 0x1d
    else if (c == '|')
        return 0xdd;        // was 0xdd
    else if (c == '\\')
        return 0x5c;    // 0x5d
    else if (c == '/')
        return 0x2f;
    else if (c == '~')
        return 0x5e;    // was 0x1f
    else if (c == '^')
        return 0x5e;        // was 1e
    else if ((c >= 'a') && (c <= 'z'))
    {
        /* lowercase (petscii 0x41 -) */
#ifdef EXT80COLS
    char c1 = (c - 'a') + 0x41;
    //log_msg("lowercase ret: '%c'/0x%02x\n", (isPrintable(c1)?c1:'~'), c1);
    return c1;
#else
        return (uint8_t)((c - 'a') + 1);
#endif
    }
    else if ((c >= 'A') && (c <= 'Z'))
    {
        /* uppercase (petscii 0xc1 -)
           (don't use duplicate codes 0x61 - ) */
#ifdef EXT80COLS
    char c1 = (c - 'A') + 0xc1;
    //log_msg("Uppercas ret: '%c'/0x%02x\n", (isPrintable(c1)?c1:'~'), c1);
    return c1;
#else
        return c; //(uint8_t)((c + 0x20));
#endif
    }
    else if (c <= 0x1f)
        /* unhandled ctrl codes */
        return '.';
    else if (c >= 0x7b)
    {
        /* last not least, ascii codes >= 0x7b can not be
           represented properly in petscii */
        return '.';
    }

    return petcii_fix_dupes(c);
}

void string2petscii(char *buf, const char *str)
{
    int i = 0;
    while (*str)
    {
        buf[i++] = charset_p_topetcii(*str); // ascToPetcii(*str);
        str++;
    }
    buf[i] = '\0';
}

#define ASCII_UNMAPPED '.'

uint8_t charset_p_toascii(uint8_t c, int cs)
{
    if (cs)
    {
        //log_msg("before conv: 0x%02x\n", c);
        /* convert ctrl chars to "screencodes" (used by monitor) */
        if (c == 0x64)
            return c = 95; // underline '_'
        if (c == 0x1c)
            return c = 35; // pound sign
        if (c == 0x1e)
            return c = 94; // arrow up
        if (c == 0x1f)
            return c = 60; // arrow left mapped to '<'
        if ((c >= 0x41) && (c <= 0x5a))
            return c + 0x20;
        //if (c <= 0x1f)
        //    c += 0x40;
    }
    //log_msg("before dupes: 0x%02x\n", c);
    c = petcii_fix_dupes(c);
    //log_msg("after dupes: 0x%02x\n", c);
    /* map petscii to ascii */
    if (c == 0x0d)
    { /* petscii "return" */
        return '\n';
    }
    else if (c == 0x0a)
    {
        return '\r';
    }
    else if (c <= 0x1f)
    {
        /* unhandled ctrl codes */
        return ASCII_UNMAPPED;
    }
    else if (c == 0xa0)
    { /* petscii Shifted Space */
        return ' ';
    }
    else if ((c >= 0xc1) && (c <= 0xda))
    {
        /* uppercase (petscii 0xc1 -) */

        return (uint8_t)((c - 0xc1) + 'A');
    }
    else if ((c >= 0x41) && (c <= 0x5a))
    {
        /* lowercase (petscii 0x41 -) */
        return (uint8_t)((c - 0x41) + 'a');
    }

    return ((isprint(c) ? c : ASCII_UNMAPPED));
}

void zisetup_parallel(void);
void ziloop_parallel(void);

void setup()
{
    // put your setup code here, to run once:
    Serial.begin(115200);
    setup_log();
    setup_cr();
    delay(20);
    drv.open();
#ifdef ZIMODEM
    zisetup_parallel();
#endif
    return;
}

void loop()
{
#ifdef ZIMODEM
    ziloop_parallel();
    delay(10);
    return;
#endif
    log_msg("Waiting for command from c64...\n");
    ret = drv.read(buf, 4);
    if (ret == 4)
    {
        buf[ret] = '\0';
        if (!process_cmd(buf))
        {
            log_msg("Unknown CoRoutine... %s\n", buf);
        }
    }
    else
    {
        static int retry = 0;
        log_msg("read error(%d'th time): %d\n", ++retry, ret);
        if (retry > 10)
        {
            log_msg("...giving up & rebooting\n");
            ESP.restart();
        }
        delay(500);
    }
    loop_log();
    delay(100);
}

#ifdef TEST_FW
void terminal(void)
{
    if (Serial.available())
    {
        static char buf[80];
        static int idx = 1;
        char c = Serial.read();
        if (c == '\n')
        {
            buf[0] = idx - 2;
            buf[idx--] = '\0';
            // log_msg("sending: '%s', len = %d\n", buf+1, buf[0]);
            string2petscii(buf + 1, buf + 1);

            drv.sync4write();
            //log_msg("synced for write... writing %d byte...\n", idx);
            if ((ret = drv.write(buf, 1)) != 1)
            {
                log_msg("len write error: %d\n", ret);
            }
            idx--;
            //delay(1000);
            if ((ret = drv.write(buf + 1, idx)) != idx)
            {
                log_msg("data write error: %d\n", ret);
            }
            idx = 1;
        }
        else
        {
            buf[idx++] = c;
            log_msg("%c", c);
        }
    }
    delay(5);
    return;
}

void dump_chars(void)
{
    for (int i = 0; i < 256; i++)
    {
        char c;
        c = i;
        log_msg("a:0x%02x/%c, ", c, (isPrintable(c)) ? c : '~');
        c = charset_p_topetcii(i);
        log_msg("v:0x%02x/%c, ", c, (isPrintable(c)) ? c : '~');
        c = ascToPetcii(i);
        log_msg("z:0x%02x/%c, ", c, (isPrintable(c)) ? c : '~');
        log_msg("\n");
    }
}
#endif
