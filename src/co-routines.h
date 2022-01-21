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

#ifndef __CO_ROUTINES_H__
#define __CO_ROUTINES_H__

#include <list>
#include "logger.h"
#include "irc.h"
#include "html.h"

void setup_cr(void);
void loop_cr(void);

int cmp(uint8_t *buf, int len);

class cr_base
{
protected:
    String name;
    void reg(void)
    {
        if (setup())
        {
            cr_base::coroutines.push_back(this);
            log_msg("Registered command '" + name + "'\n");
        }
        else
            log_msg("Setup of coroutine " + name + " failed.\n");
    }

public:
    cr_base(String n) : name(n)
    {
    }
    ~cr_base() = default;

    static std::list<cr_base *> coroutines;
    static char aux_buf[MAX_AUX];

    bool match(char *cmd, pp_drv *drv)
    {
        // log_msg("cmd %s vs. %s\n", cmd, name);
        if (strncmp(cmd, name.c_str(), 4) == 0)
        {
            web_send_cmd("CoRoutine#" + name);
            return run(drv);
        }
        return false;
    };

    virtual bool setup(void) = 0;
    virtual bool run(pp_drv *drv) = 0;
    // virtual void loop(void) = 0;
};

class cr_mandel_t : public cr_base
{
    // uint8_t *canvas;
    void *m;

public:
    cr_mandel_t(const char *n) : cr_base(String{n}) { reg(); }
    ~cr_mandel_t() = default;

    bool setup(void) override;
    bool run(pp_drv *drv) override;
    // void loop(void) override;
};

class cr_echo_t : public cr_base
{
    uint8_t *canvas;

public:
    cr_echo_t(const char *n) : cr_base(String{n}) { reg(); }
    ~cr_echo_t() = default;

    bool setup(void) override { return true; };
    bool run(pp_drv *drv) override
    {
        char c;
        int idx = 0, j;
        log_msg("reading echo arg...\n");
        while (drv->read(&c, 1))
        {
            aux_buf[idx++] = c;
            if (c == '\0')
                break;
        }
        int ret;
        idx--;
        static char flip_buf[128];
        j = 0;
        for (int i = (idx - 1); i >= 0; i--)
        {
            flip_buf[j++] = aux_buf[i];
        }
        flip_buf[j] = '\0';
        log_msg("sending back '%s'.\n", flip_buf);
        if ((ret = drv->write(flip_buf, idx)) != idx)
            log_msg("write error: %d\n", ret);
        return true;
    }
};

class cr_dump_t : public cr_base
{
    uint8_t *canvas;

public:
    cr_dump_t(const char *n) : cr_base(String{n}) { reg(); }
    ~cr_dump_t() = default;

    bool setup(void) override { return true; };
    bool run(pp_drv *drv) override
    {
        int ret;
        if ((ret = drv->read(aux_buf, 2)) != 2)
        {
            log_msg("read error: %d\n", ret);
            return false;
        }
        int b = aux_buf[0] + aux_buf[1] * 256;

        log_msg("Coroutine dump for %d bytes\n", b);
        if (b >= MAX_AUX)
        {
            log_msg("dump too large: %d.\n", b);
            return false;
        }
        static int ch = 0;
        for (int i = 0; i < b; i++)
        {
            aux_buf[i] = charset_p_topetcii('a' + ((i + ch++) % 27));
        }
        // delay(500);
        if ((ret = drv->write(aux_buf, b)) != b)
        {
            log_msg("write error: %d\n", ret);
            return false;
        }
        return true;
    }
};

class cr_dump2_t : public cr_base
{
    uint8_t *canvas;

public:
    cr_dump2_t(const char *n) : cr_base(String{n}) { reg(); }
    ~cr_dump2_t() = default;

    bool setup(void) override { return true; };
    bool run(pp_drv *drv) override
    {
        char *buf = new char[MAX_AUX];
        int ret;
        if ((ret = drv->read(aux_buf, 2)) != 2)
        {
            log_msg("read error: %d\n", ret);
            return false;
        }
        int b = aux_buf[0] + aux_buf[1] * 256;

        if (b >= MAX_AUX)
        {
            log_msg("dump too large: %d.\n", b);
            return false;
        }
        unsigned long t1 = millis(), t2;
        if ((ret = drv->read(buf, b)) != b)
        {
            log_msg("read error: %d\n", ret);
            return false;
        }
        t2 = millis();
        float baud = ((float)ret) / (t2 - t1) * 8000;
        int equal = cmp((uint8_t *)buf, ret);
        log_msg("successfully read %d bytes in %ldms(%.0f BAUD) - ident = %d.\n", ret, millis() - t1, baud, equal);
        delete[] buf;
        return true;
    }
};

class cr_read_t : public cr_base
{
    uint8_t *canvas;

public:
    cr_read_t(const char *n) : cr_base(String{n}) { reg(); }
    ~cr_read_t() = default;

    bool setup(void) override { return true; };
    bool run(pp_drv *drv) override
    {
        int ret;
        if ((ret = drv->read(aux_buf, 2)) != 2)
        {
            log_msg("read error: %d\n", ret);
            return false;
        }
        int b = aux_buf[0] + aux_buf[1] * 256;

        log_msg("Coroutine read for %d bytes\n", b);
        if (b >= MAX_AUX)
        {
            log_msg("read too large: %d.\n", b);
            return false;
        }
        long sent = 0;
        while (sent < b)
        {
            if (Serial.available())
            {
                char c = Serial.read();
                char cpet = charset_p_topetcii(c);
                if ((ret = drv->write(&cpet, 1)) != 1)
                {
                    log_msg("read error: %d\n", ret);
                    return false;
                }
                sent++;
            }
        }
        return true;
    }
};

#ifdef IRC_CRED
class cr_irc_t : public cr_base
{
public:
    cr_irc_t(const char *n) : cr_base(String{n}) { reg(); }
    ~cr_irc_t() = default;

    bool setup(void) override { return true; };
    bool run(pp_drv *drv) override
    {
        irc_t irc;
        while (irc.loop(*drv))
            delay(1);
    }
};
#endif /* IRC_CRED */

class cr_arith_t : public cr_base
{
    const std::vector<double> pow2s = {pow(2, -8), pow(2, -16), pow(2, -24), pow(2, -32)};
    // to be consistent with definitions on 8bit side! 
    enum
    {
        uCFADD = 0b00010010,
        uCFSUB = 0b00100010,
        uCFMUL = 0b00110010,
        uCFDIV = 0b01000010,
        uCFSIN = 0b01010001
    };
    double arg1, arg2, arg3, arg4;
    void parse_arg(const char *);

    /*
        Routines for converting Commodore's floating point format to IEEE 754 and back
        Code for little endian machine
        Code: Wil, 2022-Jan
        License: The Unlicense (Free use)
    */
    void float2cbm6(float f, char *c);
    void float2cbm5(float f, char *c);
    float cbm52float(char *c);
    float cbm62float(char *c);

    void dump(float f) { char *p = (char *) &f; log_msg("%lf = %02x-%02x-%02x-%02x\n", f, p[0], p[1], p[2], p[3]); }
    void dump5(const char *f) { char *p = (char *) f; log_msg("%02x-%02x-%02x-%02x-%02x\n", p[0], p[1], p[2], p[3], p[4]); }
    void dump6(const char *f) { char *p = (char *) f; log_msg("%02x-%02x-%02x-%02x-%02x-%02x\n", p[0], p[1], p[2], p[3], p[4], p[5]); }

public:
    cr_arith_t(const char *n) : cr_base(String{n})
    {
        reg();
    }
    ~cr_arith_t() = default;
    bool setup(void) override { return true; };
    bool run(pp_drv *drv) override;
};

#endif