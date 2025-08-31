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
#include "ssd1306-display.h"

void setup_cr(void);
void loop_cr(void);

int cmp(uint8_t *buf, int len);
void hexdump(const char *buf, int len);

class cr_base
{
protected:
    String name;
    pp_drv *drv;
    void reg(void)
    {
        if (setup())
        {
            cr_base::coroutines.push_back(this);
            log_msg("Registered command '%s' - %p'\n", name.c_str(), this);
        }
        else
            log_msg("Setup of coroutine " + name + " failed.\n");
    }

public:
    cr_base(String n, pp_drv *drv = nullptr) : name(n), drv(drv)
    {
    }
    ~cr_base() = default;

    static std::list<cr_base *> coroutines;
    static char aux_buf[MAX_AUX];
    static char aux_buf2[MAX_AUX];

    int match(char *cmd, pp_drv *drv)
    {
        // log_msg("cmd %s vs. %s\n", cmd, name);
        if (strncmp(cmd, name.c_str(), 4) == 0)
        {
            web_send_cmd("CoRoutine#" + name);
            if (name != "ARIT")
                lcd->printf("CoRoutine %s\n", name.c_str());
            return timed_run(drv);
        }
        return 0;
    };

    int timed_run(pp_drv *drv)
    {
        unsigned long t1;
        int ret;
        t1 = millis();
        ret = run(drv);
        //log_msg("CoRoutine %s ran %dms\n", name.c_str(), millis() - t1);
        return ret;
    }
    virtual bool setup(void) = 0;
    virtual int run(pp_drv *drv) = 0;
    // virtual void loop(void) = 0;
};

typedef uint8_t canvas_t;
typedef int coord_t;
typedef uint8_t color_t;
typedef struct
{
    coord_t x;
    coord_t y;
} point_t;

class cr_mandel_t : public cr_base
{
    void *m;
    canvas_t *canvas;
    void dump_bits(uint8_t c);
    void canvas_dump(canvas_t *c);
    SemaphoreHandle_t pixmutex;
public:
    cr_mandel_t(const char *n, pp_drv *_drv) : cr_base(String{n}, _drv) { reg(); pixmutex = xSemaphoreCreateMutex(); V(pixmutex); }
    ~cr_mandel_t() { vSemaphoreDelete(pixmutex); }

    bool setup(void) override;
    int run(pp_drv *drv) override;
    // void loop(void) override;
    void canvas_setpx(canvas_t *canvas, coord_t x, coord_t y, color_t c);
};

class cr_echo_t : public cr_base
{
    uint8_t *canvas;

public:
    cr_echo_t(const char *n) : cr_base(String{n}) { reg(); }
    ~cr_echo_t() = default;

    bool setup(void) override { return true; };
    int run(pp_drv *drv) override
    {
        char c;
        int idx = 0, j;
        int ret;
        //log_msg("reading echo arg...\n");
        while (((ret = drv->read(&c, 1)) == 1) && idx < (MAX_AUX - 1))
        {
            aux_buf[idx++] = c;
            if (c == '\0')
                break;
        }
        if (ret != 1)
        {
            log_msg("read error: %d\n", ret);
            return ret;
        }
        idx--;
        static char flip_buf[128];
        j = 0;
        for (int i = (idx - 1); i >= 0; i--)
        {
            flip_buf[j++] = aux_buf[i];
        }
        flip_buf[j] = '\0';
        log_msg("ECHO '%s' -> '%s'\n", aux_buf, flip_buf);
        if ((ret = drv->write(flip_buf, strlen(flip_buf))) != idx)
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
    int run(pp_drv *drv) override
    {
        int ret;
        if ((ret = drv->read(aux_buf, 2)) != 2)
        {
            log_msg("read error: %d\n", ret);
            return ret;
        }
        int b = aux_buf[0] + aux_buf[1] * 256;

        log_msg("Coroutine dump for %d bytes\n", b);
        if (b >= MAX_AUX)
        {
            log_msg("dump too large: %d.\n", b);
            return -E2BIG;
        }
        static int ch = 0;
        for (int i = 0; i < b; i++)
        {
            aux_buf[i] = charset_p_topetcii('a' + ((i + ch++) % 27));
        }
        //delay(500);
        unsigned long t1, t2;
        t1 = millis();
        if ((ret = drv->write(aux_buf, b)) != b)
        {
            log_msg("write error: %d\n", ret);
            return ret;
        }
        t2 = millis();
        float baud;
        log_msg("sent %d chars in ", ret);
        log_msg("%dms(", t2 - t1);
        baud = ((float)ret) / (t2 - t1) * 8000;
        log_msg("%.0f BAUD)\n", baud);

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
    int run(pp_drv *drv) override
    {
        char *buf = new char[MAX_AUX];
        int ret;
        if ((ret = drv->read(aux_buf, 2)) != 2)
        {
            log_msg("read error: %d\n", ret);
            return ret;
        }
        int b = aux_buf[0] + aux_buf[1] * 256;

        if (b >= MAX_AUX)
        {
            log_msg("dump too large: %d.\n", b);
            return -E2BIG;
        }
        unsigned long t1 = millis(), t2;
        if ((ret = drv->read(buf, b)) != b)
        {
            log_msg("read error: %d\n", ret);
            return ret;
        }
        t2 = millis();
        float baud = ((float)ret) / (t2 - t1) * 8000;
        int equal = cmp((uint8_t *)buf, ret);
        log_msg("successfully read %d bytes in %ldms(%.0f BAUD) - %sidentical.\n", ret, millis() - t1, baud, ((equal == 0) ? "" : "not "));
        delete[] buf;
        return true;
    }
};

class cr_dump3_t : public cr_base
{
    size_t chunk_size;
public:
    cr_dump3_t(const char *n, size_t cs) : cr_base(String{n}), chunk_size(cs) { reg(); }
    ~cr_dump3_t() = default;

    bool setup(void) override { return true; };
    int run(pp_drv *drv) override
    {
        int lb = 0;
        int ret;
        int end = 16;

        static char strout[128];
        strout[0] = '\0';
        unsigned long t1, t2;
        t1 = t2 = millis();
        do
        {
            ret = drv->read(aux_buf, chunk_size, true);
            if (ret < 0)
            {
                log_msg("%s: read error %d\n", __FUNCTION__, ret);
                return ret;
            }
#if 1            
            for (int i = 0; i < 8; i++)
            {
                char t[64];
                snprintf(t, 8, "%02x ", aux_buf[i]);
                strcat(strout, t);
            }
            printf("%s - %d\n", strout, lb);
            strout[0] = '\0';
#endif
            if (lb == 64000 / chunk_size)
            {
                t2 = millis();
                float baud;
                log_msg("rcvd %d chars in ", lb * chunk_size);
                log_msg("%dms(", t2 - t1);
                baud = ((float)lb * chunk_size) / (t2 - t1) * 8000;
                log_msg("%.0f BAUD)\n", baud);
                lb = 0;
                t1 = millis();
            }
            lb++;
        } while (--end);
        return true;
    }
};

class cr_dump4_t : public cr_base
{
    size_t chunk_size;
public:
    cr_dump4_t(const char *n, size_t cs) : cr_base(String{n}), chunk_size(cs) { reg(); }
    ~cr_dump4_t() = default;

    bool setup(void) override { return true; };
    int run(pp_drv *drv) override
    {
        int ret, lb = 0;
        int end = 16;
        unsigned long t1, t2;
        t1 = t2 = millis();
        for (int i = 0; i < chunk_size; i++)
            aux_buf[i] = (i & 0xff);
        do
        {
            ret = drv->write(aux_buf, chunk_size);
            if (ret != chunk_size)
            {
                log_msg("%s: write error: %d\n", __FUNCTION__, ret);
                return ret;
            }
            if (lb == 64000 / chunk_size)
            {
                t2 = millis();
                float baud;
                log_msg("sent %d chars in ", lb * chunk_size);
                log_msg("%dms(", t2 - t1);
                baud = ((float)lb * chunk_size) / (t2 - t1) * 8000;
                log_msg("%.0f BAUD)\n", baud);
                lb = 0;
                t1 = millis();
            }
            lb++;
        } while (--end);  
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
    int run(pp_drv *drv) override
    {
        int ret;
        if ((ret = drv->read(aux_buf, 2)) != 2)
        {
            log_msg("read error: %d\n", ret);
            return ret;
        }
        int b = aux_buf[0] + aux_buf[1] * 256;

        log_msg("Coroutine read for %d bytes\n", b);
        if (b >= MAX_AUX)
        {
            log_msg("read too large: %d.\n", b);
            return -E2BIG;
        }
        long sent = 0;
        log_msg("READ: Requesed to write %d bytes...\n", b);
        // generate data
        for (int i = 0; i < b; i++)
            aux_buf[i] = charset_p_topetcii('a' + (i % 27));
        if ((ret = drv->write(aux_buf, b)) != b)
        {
            log_msg("READ: write error: %d\n", ret);
            return ret;
        }
        log_msg("READ: sent\n");
        hexdump(aux_buf, 64);
        // now read back
        if ((ret = drv->read(aux_buf2, b)) != b)
        {
            log_msg("READ: readback error: %d\n", ret);
            return ret;
        }
        log_msg("READ: received\n");
        hexdump(aux_buf2, 64);
        if (memcmp(aux_buf, aux_buf2, b) != 0)
        {
            log_msg("READ: readback data mismatch.\n");
            return -EBADMSG;
        }
        else
            log_msg("READ: readback data OK.\n");
        return true;
    }
};

#ifdef IRC_CRED
class cr_irc_t : public cr_base
{
public:
    cr_irc_t(const char *n) : cr_base(String{n}) { reg(); }
    ~cr_irc_t() = default;

    bool setup(void) override { return true; }
    int run(pp_drv *drv) override
    {
        irc_t irc;
        while (irc.loop(*drv))
            delay(1);
        return true;
    }
};
#endif /* IRC_CRED */

class cr_arith_t : public cr_base
{
    // const std::vector<double> pow2s = {pow(2, -8), pow(2, -16), pow(2, -24), pow(2, -32)};
    //  to be consistent with definitions on 8bit side!
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

    void dump(float f)
    {
        char *p = (char *)&f;
        log_msg("%lf = %02x-%02x-%02x-%02x\n", f, p[0], p[1], p[2], p[3]);
    }
    void dump5(const char *f)
    {
        char *p = (char *)f;
        log_msg("%02x-%02x-%02x-%02x-%02x\n", p[0], p[1], p[2], p[3], p[4]);
    }
    void dump6(const char *f)
    {
        char *p = (char *)f;
        log_msg("%02x-%02x-%02x-%02x-%02x-%02x\n", p[0], p[1], p[2], p[3], p[4], p[5]);
    }

public:
    cr_arith_t(const char *n) : cr_base(String{n}) { reg(); }
    ~cr_arith_t() = default;
    bool setup(void) override { return true; }
    int run(pp_drv *drv) override;
};

class cr_plot_t : public cr_base
{
public:
    cr_plot_t(const char *n) : cr_base(String{n}) { reg(); }
    ~cr_plot_t() = default;
    bool setup(void) override { return true; }
    int run(pp_drv *drv) override;
};
#endif