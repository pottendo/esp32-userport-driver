#include <Arduino.h>
#include "parport-drv.h"
#include "misc.h"
#include "logger.h"

static pp_drv *drv;

typedef enum
{
    IDLE,
    ECHO,
    DUMP,
    READ
} coroutine_t;
int cr_args = 0;
const char *cr_argsstr;

void setup()
{
    // put your setup code here, to run once:
    Serial.begin(115200);
    setup_log();
    delay(20);
    drv = new pp_drv;
    drv->open();

    //drv.open();
    //drv.setup_sender();
}

coroutine_t process_cmd(char *cmd)
{
    char *cmd_buf = cmd;
    static char aux_buf[MAX_AUX];
    while (*cmd_buf)
    {
        if (strncmp("ECHO", cmd_buf, 4) == 0)
        {
            cr_argsstr = aux_buf;
            char c;
            int idx = 0;
            while (drv->read(&c, 1))
            {
                aux_buf[idx++] = c;
                if (c == '\0')
                    break;
            }
            return ECHO;
        }
        else if (strncmp("DUMP", cmd_buf, 4) == 0)
        {
            drv->read(aux_buf, 2);
            int b = aux_buf[0] + aux_buf[1] * 256;
            cr_args = b;
            return DUMP;
        }
        else if (strncmp("READ", cmd_buf, 4) == 0)
        {
            int b = cmd_buf[4] + cmd_buf[5] * 256;
            log_msg(String("Read: ") + b + " bytes");
            cr_args = b;
            return READ;
        }
        strncpy(cmd_buf, cmd_buf + 1, 4);
        int ret = drv->read(cmd_buf + 4, 1);
        if (ret != 1)
        {
            log_msg("process_cmd, read error: %d\n", ret);
            break;
        }
        cmd_buf[5] = '\0';
    }
    log_msg("Unknown command: %s\n", cmd);

    return IDLE;
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

void do_dump(void)
{
    log_msg("Coroutine dump for %d bytes requested\n", cr_args);
    static int ch = 0;
    long s1, s2;
    uint8_t *buf = new uint8_t[cr_args];
    for (int i = 0; i < cr_args; i++)
    {
        buf[i] = charset_p_topetcii('a' + ((i + ch++) % 27));
    }
    s1 = millis();
    drv->write((const char *)buf, cr_args);
    s2 = millis();
    log_msg("sent %d chars in ", cr_args);
    log_msg("%dms(", s2 - s1);
    float baud = ((float)cr_args) / (s2 - s1) * 8000;
    log_msg("%.0f)\n", baud);
    delete[] buf;
}

void do_echo(const char *what)
{
    log_msg("Coroutine echo requested '%s'\n", what);
    if (!cr_argsstr)
    {
        log_msg("nothing to echo\n");
        return;
    }
    drv->write(what, strlen(what));
}

void do_read(void)
{
    log_msg("Coroutine read requested\n");
    long sent = 0;
    while (sent < cr_args)
    {
        if (Serial.available())
        {
            char c = Serial.read();
            char cpet = charset_p_topetcii(c);
            drv->write(&cpet, 1);
            sent++;
        }
        delay(100);
    }
}

void loop()
{
    coroutine_t cr = IDLE;

    log_msg("Waiting for command from c64...\n");
#if 0
    drv->readstr(&line);
    log_msg(String("C64 sent: '") + (*line) + "'\n");
    cr = process_cmd(line->c_str());
#endif
    static char buf[MAX_AUX];
    int ret = drv->read(buf, 4);
    if (ret == 4)
    {
        buf[ret] = '\0';
        cr = process_cmd(buf);
        switch (cr)
        {
        case IDLE:
            break;
        case ECHO:
            do_echo(cr_argsstr);
            cr_argsstr = nullptr;
            cr = IDLE;
            break;
        case DUMP:
            cr = IDLE;
            do_dump();
            break;
        case READ:
            cr = IDLE;
            do_read();
            break;
        default:
            log_msg("Unknown CoRoutine... %d", cr);
            break;
        }
    }
    else
        log_msg("read error: %d\n", ret);
    //delete line;
    loop_log();
    delay(100);
}