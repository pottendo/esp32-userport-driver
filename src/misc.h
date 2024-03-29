#ifndef __MISC_H__
#define __MISC_H__
#include <Arduino.h>
#include <stdarg.h>
#include <WiFi.h>
#include "logger.h"

#define MUT_EXCL
#ifdef MUT_EXCL
#define P(sem) xSemaphoreTake((sem), portMAX_DELAY)
#define V(sem) xSemaphoreGive(sem)
#else
#define P(sem)
#define V(seM)
#endif

void setup_slip(WiFiClass &);
void loop_slip(void);

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
        delayMicroseconds(ms * 1000);
        digitalWrite(2, LOW);
        delayMicroseconds(ms * 1000);
    }
}

template <class T>
class ring_buf_t
{
    const int size;
    T *buffer;
    int w, r;
    SemaphoreHandle_t wmutex, rmutex, mutex;
    bool unblock;

public:
    ring_buf_t(int sz) : size(sz), w(0), r(0), unblock(false)
    {
        buffer = new T[sz];
        wmutex = xSemaphoreCreateCounting(sz, sz - 1);
        rmutex = xSemaphoreCreateCounting(sz, 0);
        mutex = xSemaphoreCreateMutex();    // mutex for access to pointers
    }
    ~ring_buf_t() { delete[] buffer; }

    inline void put(T &item)
    {
        P(wmutex);
        P(mutex);
        buffer[w] = item;
        w++;
        w %= size;
        V(mutex);
        V(rmutex);
    }
    inline bool get(T &item, bool block = true)
    {
        if (!block)
        {
            BaseType_t res;
            res = xSemaphoreTake(rmutex, 100 * portTICK_PERIOD_MS);
            if (res == pdFALSE)
                return false;
        }
        else
            P(rmutex);
        if (r == w)
        {
            log_msg("ringbuffer corrupt r:%d, w:%d\n", r, w);
            return false;
        }
        P(mutex);
        item = buffer[r];
        r++;
        r %= size;
        V(mutex);
        V(wmutex);
        return true;
    }

    inline T peek(void)
    {
        _FMUTEX(mutex);
        return buffer[r]; // maybe invalid (if empty)
    }

    inline int len(void)
    {
        _FMUTEX(mutex);
        int ret = (w - r + size) % size;
        return ret;
    }
};


#endif