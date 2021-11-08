#include <Arduino.h>
#include "parport-drv.h"
#include "misc.h"
#include "logger.h"
#include "co-routines.h"

static pp_drv *drv;

void setup()
{
    // put your setup code here, to run once:
    Serial.begin(115200);
    setup_log();
    setup_cr();
    delay(20);
    drv = new pp_drv;
    drv->open();

    //drv.open();
    //drv.setup_sender();
}

int process_cmd(char *cmd)
{
    char *cmd_buf = cmd;
    while (*cmd_buf)
    {
        for (auto cr : cr_base::coroutines)
        {
            if (cr->match(cmd_buf, drv))
                return 1;
        }
        strncpy(cmd_buf, cmd_buf + 1, 3);
        int ret = drv->read(cmd_buf + 4, 1);
        if (ret != 1)
        {
            log_msg("process_cmd, read error: %d\n", ret);
            break;
        }
        cmd_buf[5] = '\0';  // probably not needed.
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

void loop()
{
    log_msg("Waiting for command from c64...\n");

    static char buf[MAX_AUX];
    int ret = drv->read(buf, 4);
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