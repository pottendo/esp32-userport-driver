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

#ifndef __PARPORT_DRV_H__
#define __PARPORT_DRV_H__

#include <Arduino.h>
#include <list>
#include "misc.h"
#include "logger.h"

#define MAX_AUX (1024 * 8)

extern void udelay(unsigned long us);

class pp_drv
{
    uint16_t qs, bs;
    TaskHandle_t th1, th2;
    bool verbose;
    bool in_write = false;
    // enum to enable iteration over all pins
    typedef enum
    {
        _PB0 = 0,
        _PB1,
        _PB2,
        _PB3,
        _PB4,
        _PB5,
        _PB6,
        _PB7,
        _PA2,
        _PC2,
        _SP2,
        _FLAG,
        _OE
    } par_pins_t;
//    const uint8_t par_pins[13] = {13, 4, 16, 17, 5, 18, 19, 32 /*22*/, 23, 25 /*21*/, 27, 15, 26};
    const uint8_t par_pins[13] = {32, 19, 18, 5, 17, 16, 4, 13, 23, 25 /*21*/, 27, 15, 26};
    QueueHandle_t rx_queue;
    QueueHandle_t tx_queue;
    QueueHandle_t s1_queue;
    QueueHandle_t s2_queue;

    uint8_t mode;
    const int rbuf_len = 256;
    ring_buf_t<unsigned char> ring_buf{rbuf_len};
    int32_t csent;
#define PAR(x) (par_pins[x])
#define PB0 PAR(_PB0)
#define PB1 PAR(_PB1)
#define PB2 PAR(_PB2)
#define PB3 PAR(_PB3)
#define PB4 PAR(_PB4)
#define PB5 PAR(_PB5)
#define PB6 PAR(_PB6)
#define PB7 PAR(_PB7)
#define PA2 PAR(_PA2)
#define PC2 PAR(_PC2)
#define SP2 PAR(_SP2)
#define FLAG PAR(_FLAG)
#define OE PAR(_OE)

protected:
    static void th_wrapper1(void *t);
    static void th_wrapper2(void *t);
    static void isr_wrapper_sp2(void);
    static void isr_wrapper_pc2(void);
    static pp_drv *active_drv;

    void sp2_isr(void);
    void pc2_isr(void);
    void drv_ackrcv(void);
    bool outchar(const char c, bool from_isr);
    void drv_body();
    inline void flag_handshake(void)
    {
        digitalWrite(FLAG, HIGH);
        digitalWrite(FLAG, LOW);
    }
    size_t _write(const void *s, size_t len);
    
public:
    pp_drv(uint16_t qs = MAX_AUX, uint16_t bs = MAX_AUX);
    ~pp_drv();

    void setup_rcv(void);
    void setup_snd(void);
    void open(void);
    void close(void);

    int writestr(String &s) { return write(s.c_str(), s.length()); }
    size_t write(const void *s, size_t len);
    size_t write(uint8_t c) { return write((const char *)(&c), 1); }
    void sync4write(void);
    ssize_t read(void *buf, size_t len, bool block = true);
    int available(void) { return ring_buf.len(); }
    int read(void)
    {
        char c;
        int ret = read(&c, 1);
        return ((ret == 1) ? c : -1);
    }
    int peek(void) { return ring_buf.peek(); }
    int readBytes(uint8_t *buf, size_t len) { return read((char *)buf, len); }
    int availableForWrite(void) { return rbuf_len - ring_buf.len(); }
    void flush(void) {}
    void setTimeout(int) {}
    /* functions from HWSerial */
    void changeBaudRate(int) {}
    void changeConfig(uint32_t) {}
    void begin(unsigned long baud, uint32_t config) {}

    void printf(const char *s, ...)
    {
        char t[256];
        va_list args;
        va_start(args, s);
        vsnprintf(t, 256, s, args);
        write(t, strlen(t));
    }
    bool ctsActive(void)
    {
        return (digitalRead(PA2) == LOW);
    }
};

extern pp_drv drv;

#endif