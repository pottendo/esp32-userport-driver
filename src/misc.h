#ifndef __MISC_H__
#define __MISC_H__
#include <Arduino.h>
#include <stdarg.h>
#include "logger.h"

#define MUT_EXCL
#ifdef MUT_EXCL
#define P(sem) xSemaphoreTake((sem), portMAX_DELAY)
#define V(sem) xSemaphoreGive(sem)
#else
#define P(sem)
#define V(seM)
#endif

class func_mutex
{
    SemaphoreHandle_t &mutex;

public:
    func_mutex(SemaphoreHandle_t &m) : mutex(m) { P(mutex); }
    ~func_mutex() { V(mutex); }
};
#define _FMUTEX(x) \
    func_mutex __m { (x) }

inline void blink(uint32_t ms = 100, int count = 1)
{
    pinMode(2, OUTPUT);
    if (count == 0)
    {
        digitalWrite(2, !digitalRead(2));
        return;
    }
    for (int i = 0; i < count; i++)
    {
        digitalWrite(2, HIGH);
        delay(ms);
        digitalWrite(2, LOW);
        delay(ms);
    }
}

template <class T>
class ring_buf_t
{
    const int size;
    T *buffer;
    int w, r;
    SemaphoreHandle_t wmutex, rmutex;
    bool unblock;

public:
    ring_buf_t(int sz) : size(sz), w(0), r(0), unblock(false)
    {
        buffer = new T[sz];
        wmutex = xSemaphoreCreateCounting(sz, sz);
        rmutex = xSemaphoreCreateCounting(sz, 0);
    }
    ~ring_buf_t() { delete[] buffer; }

    inline void put(T &item)
    {
        P(wmutex);
        buffer[w] = item;
        w++; w %= size;
        V(rmutex);
    }
    inline bool get(T &item, bool block = true)
    {
        P(rmutex);
        if (r == w)
        {
            log_msg("ringbuffer corrupt r:%d, w:%d\n", r, w);
            return false;
        }

        item = buffer[r];
        r++; r %= size;
        V(wmutex);
        return true;
    }
};

uint8_t charset_p_topetcii(uint8_t c);
#endif