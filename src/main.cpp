#include <Arduino.h>
#include "parport-drv.h"
#include "misc.h"
#include "logger.h"
#include "co-routines.h"
#include "irc.h"
#include "pet2asc.h"

#define TEST_FW
//#define ZIMODEM

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
    /* map ascii to petscii */
    if (c == '\n')
    {
        return 0x0d; /* petscii "return" */
    }
    else if (c == '\r')
    {
        return 0x0a;
    }
    else if (c <= 0x1f)
    {
        /* unhandled ctrl codes */
        return '.';
    }
    else if (c == '`')
    {
        return 0x27; /* petscii "'" */
    }
    else if ((c >= 'a') && (c <= 'z'))
    {
        /* lowercase (petscii 0x41 -) */
        return (uint8_t)((c - 'a') + 0x1);
    }
    else if ((c >= 'A') && (c <= 'Z'))
    {
        /* uppercase (petscii 0xc1 -)
           (don't use duplicate codes 0x61 - ) */
        return (uint8_t)((c - 'a') + 0x1);
    }
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
        buf[i++] = ascToPetcii(*str); //charset_p_topetcii(*str);
        str++;
    }
    buf[i] = '\0';
}

#define ASCII_UNMAPPED '.'

uint8_t charset_p_toascii(uint8_t c, int cs)
{
    if (cs)
    {
        /* convert ctrl chars to "screencodes" (used by monitor) */
        if (c <= 0x1f)
        {
            c += 0x40;
        }
    }

    c = petcii_fix_dupes(c);

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
    setup_irc();
#ifdef ZIMODEM
    zisetup_parallel();
#endif
}

void loop()
{
#ifdef TEST_FW
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
    loop_irc();
    static String s;
    if (irc_get_msg(s))
    {
        static char ibuf[128];
        String t;
        log_msg("IRC msg '%s' len: %d\n", s.c_str(), s.length());
        int it, i = 0, e = s.length();
        while (i < e)
        {
            it = ((i + 77) < e) ? (i + 77) : e;
            String t = s.substring(i, it);
            log_msg("\t'%s'\n", t.c_str());
            if ((e - i) <= 0)
                break;
            i += 77;
            ibuf[0] = t.length();
            string2petscii(ibuf + 1, t.c_str());
            drv.sync4write();
            log_msg("synced for write... writing %d byte...\n", ibuf[0] + 1);
            if ((ret = drv.write(ibuf, 1)) != 1)
            {
                log_msg("len write error: %d\n", ret);
            }
            //delay(1000);
            if ((ret = drv.write(ibuf + 1, ibuf[0])) != ibuf[0])
            {
                log_msg("data write error: %d\n", ret);
            }
            delay(200);
        }
    }
    delay(100);
    return;
#endif
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