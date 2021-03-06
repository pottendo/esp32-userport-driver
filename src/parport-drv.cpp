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

#include <Arduino.h>

#include "parport-drv.h"
#include "misc.h"
#include "logger.h"

//#define DUMP_TRAFFIC

void udelay(unsigned long us)
{
    unsigned long st = micros();
    while ((micros() - st) < us)
        ;
}

/*
 * static members
 */
pp_drv *pp_drv::active_drv = nullptr;
void IRAM_ATTR pp_drv::isr_wrapper_sp2(void)
{
    active_drv->sp2_isr();
}

void IRAM_ATTR pp_drv::isr_wrapper_pc2(void)
{
    active_drv->pc2_isr();
}

void pp_drv::th_wrapper1(void *t)
{
    pp_drv *drv = static_cast<pp_drv *>(t);
    drv->drv_body();
}

void pp_drv::th_wrapper2(void *t)
{
    pp_drv *drv = static_cast<pp_drv *>(t);
    drv->drv_ackrcv();
}

/* ISRs */
void pp_drv::sp2_isr(void)
{
    // SP2 signaled by C64:
    //   LOW: C64 -> ESP
    //   HIGH: ESP -> C64 possible
    if (digitalRead(SP2) == LOW)
    {
        digitalWrite(LED_BUILTIN, LOW);
        // log_msg_isr("SP2(LOW) isr - mode C64 -> ESP\n");
        setup_rcv(); // make sure I/Os are setup to input to avoid conflicts on lines
    }
    else
    {
        digitalWrite(LED_BUILTIN, HIGH);
        // log_msg_isr("SP2(HIGH) isr - mode ESP->C64\n");
    }
}

/* ISRs */
void pp_drv::pc2_isr(void)
{
    int32_t err = 0;
    BaseType_t higherPriorityTaskWoken = pdFALSE;
    if (mode == INPUT)
    {
        char c;
        int i;
        c = 0;

        for (i = _PB7; i >= _PB0; i--)
        {
            char b = (digitalRead(PAR(i)) == LOW) ? 0 : 1;
            c = (c << 1) | b;
        }
        // log_msg_isr("ps2isr: %c\n", c);
        if (xQueueSendToBackFromISR(rx_queue, &c, &higherPriorityTaskWoken) == errQUEUE_FULL)
            blink(150, 0); // signal that we've just discarded a char
        flag_handshake();
        unsigned long to = micros();
        while (digitalRead(PA2) != HIGH)
        {
            if ((micros() - to) > 500)
            {
                // log_msg_isr("TC2 handshake1 - C64 not responding.\n");
                err = -1;
                // blink(150, 0);
                flag_handshake();
            }
        }
        // blink(150, 0);
        if (higherPriorityTaskWoken)
            portYIELD_FROM_ISR();
    }
    if (mode == OUTPUT)
    {
        // log_msg_isr("pc2 isr - output\n");
        BaseType_t xTaskWokenByReceive = pdFALSE;
        char c;
        if (uxQueueMessagesWaitingFromISR(tx_queue) > 0)
        {
            if (xQueueReceiveFromISR(tx_queue, (void *)&c, &xTaskWokenByReceive) == pdTRUE)
            {
                // log_msg_isr("would send from ISR" + c + '\n');
                if (!outchar(c))
                    log_msg_isr("write error in ISR.\n");
                else
                    csent++;
                flag_handshake();
                unsigned long to = micros();
                while ((digitalRead(PA2) != HIGH) && ((micros() - to) < 100000))
                    ;
                if ((micros() - to) > 2000) // was 500, 1850 seen once.
                {
                    log_msg_isr("TC2 handshake1 - C64 not responding for %dus.\n", micros() - to);
                    err = -1;
                }
                // log_msg_isr("tc2 handshake 1 took %ldus\n", micros() - to);
            }
            else
                log_msg_isr("xQueueReceive failed in ISR.\n");
        }
        else
        {
            // log_msg_isr("last char sent, releasing mutex\n");
            int32_t out;
            if (err < 0)
            {
                log_msg_isr("ISR write error, sent so far: %d bytes.", csent);
                out = err;
            }
            else
                out = csent;

            if (xQueueSendToBackFromISR(s1_queue, &out, &higherPriorityTaskWoken) == errQUEUE_FULL)
                ; // log_msg_isr("TC2 can't release write.\n");
            csent = 0;
        }
        if (err >= 0)
        {
            unsigned long to = micros();
            while (digitalRead(PA2) != LOW)
            {
                if ((micros() - to) > 2250) // saw delays up to 1860us
                {
                    while (digitalRead(PA2) != LOW)
                        ;
                    log_msg_isr("TC2 handshake2 - C64 not responding for %dus.\n", micros() - to);
                    err = -2;
                    break;
                }
            }
            // log_msg_isr("tc2 handshake 2 took %dus\n", micros() - to);
        }
        if (err < 0)
        {
            // in case of error empty queue
            while (uxQueueMessagesWaitingFromISR(tx_queue))
            {
                xQueueReceiveFromISR(tx_queue,
                                     (void *)&c,
                                     &xTaskWokenByReceive);
            }
            log_msg_isr("ISR emptied queue because of error %d.\n", err);
        }
        if (xTaskWokenByReceive != pdFALSE)
            taskYIELD();
        udelay(25); // was 15, testing for soft80
    }
}

/*
 * member functions
 */
pp_drv::pp_drv(uint16_t qs, uint16_t bs)
    : qs(qs), bs(bs), verbose(false)
{
    rx_queue = xQueueCreate(qs, sizeof(char));
    tx_queue = xQueueCreate(qs, sizeof(char));
    s1_queue = xQueueCreate(1, sizeof(int32_t));
    s2_queue = xQueueCreate(1, sizeof(int32_t));
    active_drv = this;
    csent = 0;
    pinMode(OE, OUTPUT);
    digitalWrite(OE, HIGH);
}

pp_drv::~pp_drv()
{
    vQueueDelete(rx_queue);
    vQueueDelete(tx_queue);
    vQueueDelete(s1_queue);
    vQueueDelete(s2_queue);
}

void pp_drv::drv_body(void)
{
    unsigned char c;
    Serial.printf("driver(reader) launched at priority %d\n", uxTaskPriorityGet(nullptr));

    while (true)
    {
        while (xQueueReceive(rx_queue, &c, portMAX_DELAY) == pdTRUE)
        {
            // log_msg("pp - rcv: '%c'/0x%02x\n", (c? c : '~'), c);
            ring_buf.put(c);
        }
    }
}

void pp_drv::drv_ackrcv(void)
{
    Serial.printf("diver(wsync) launched with priority %d\n", uxTaskPriorityGet(nullptr));
    while (true)
    {
        int32_t err;
        if (xQueueReceive(s1_queue, &err, portMAX_DELAY) == pdTRUE)
        {
            if (xQueueSend(s2_queue, &err, 20 * portTICK_PERIOD_MS) == pdTRUE)
                continue;
        }
        log_msg("queue s1/s2 failed.\n");
    }
}

void pp_drv::setup_snd(void)
{
    // log_msg("C64 Terminal - sender");
    for (uint8_t i = _PB0; i <= _PB7; i++)
    {
        pinMode(PAR(i), OUTPUT);
        digitalWrite(PAR(i), LOW);
    }
    mode = OUTPUT;
}

// called from ISR context!
void pp_drv::setup_rcv(void)
{
    for (uint8_t i = _PB0; i <= _PB7; i++)
    {
        pinMode(PAR(i), INPUT);
    }
    mode = INPUT;
}

void pp_drv::open(void)
{
    pinMode(PA2, INPUT);
    pinMode(PC2, INPUT_PULLDOWN);
    pinMode(SP2, INPUT);
    pinMode(FLAG, OUTPUT);
    digitalWrite(FLAG, HIGH);
    mode = INPUT;
    xTaskCreate(th_wrapper1, "pp-drv-rcv", 4000, this, uxTaskPriorityGet(nullptr) + 1, &th1);
    xTaskCreate(th_wrapper2, "pp-drv-snd", 4000, this, uxTaskPriorityGet(nullptr) + 1, &th2);
    delay(50); // give logger time to setup everything before first interrupts happen
    attachInterrupt(digitalPinToInterrupt(SP2), isr_wrapper_sp2, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PC2), isr_wrapper_pc2, HIGH);

    setup_rcv();
}

void pp_drv::close(void)
{
    detachInterrupt(digitalPinToInterrupt(SP2));
    detachInterrupt(digitalPinToInterrupt(PC2));
    vTaskDelete(th1);
    vTaskDelete(th2);
}

ssize_t pp_drv::read(void *buf_, size_t len, bool block)
{
    unsigned char *buf = static_cast<unsigned char *>(buf_);
    int count = 0;
    unsigned char c;
    while (len)
    {
        if (!ring_buf.get(c, block))
            return -1;
        buf[count++] = c;
#if 0
        log_msg("rcv: 0x%02x/'%c'/", c, (isPrintable(c)?c:'~'));
        for (int i = 0; i < 8; i++)
        {
            if ((c & (1 << i)) != 0)
                log_msg("1");
            else log_msg("0");
        }
        log_msg("\n");
#endif
        len--;
    }
    return count;
}

// also called from ISR context!
bool pp_drv::outchar(const char ct)
{
    unsigned long t, t1;
    for (uint8_t s = _PB0; s <= _PB7; s++)
    {
        t = micros();
        while ((digitalRead(SP2) == LOW) && ((micros() - t) < 1000 * 100)) // allow 100ms to pass
            ;
        if ((t1 = (micros() - t)) > 1000)
            log_msg("outchar: C64 busy for %dus\n", t1);

        if (digitalRead(SP2) == HIGH)
        {
            uint8_t bit = (ct & (1 << s)) ? HIGH : LOW;
            digitalWrite(PAR(s), bit);
        }
        else
        {
            log_msg("C64 SP2 low (=writing) - cowardly refusing to write.\n"); // may fail as ISR printfs ar no-good...
            return false;
        }
        // log_msg("%d", bit);
    }
    return true;
}

ssize_t pp_drv::write(const void *buf, size_t len)
{
    const char *str = static_cast<const char *>(buf);
    int32_t ret = -1;
    unsigned long t1, t2;
    // log_msg("write of %d chars\n", len);
    if ((len == 0) || (len >= qs))
        return -1;
#ifdef DUMP_TRAFFIC
    char c = *str;
    char cd;
    if ((c == 0xa) || (c == 0xd))
        cd = '~';
    else
        cd = c;
    log_msg("0x%02x, /*'%c'*/\n", c, cd);
#endif
    setup_snd();
    bool was_busy = false;
    t1 = millis();
    while (digitalRead(PA2) == HIGH)
    {
        delay(1);
        was_busy = true;
        if ((millis() - t1) > 5000) // give up after 5s
        {
            log_msg("C64 not responding, giving up...\n");
            ret = -1;
            goto out;
        }
    }
    /* if (was_busy)
        log_msg("C64 was busy for %ldms.\n", millis() - t1);
    */
    t1 = millis();
    if (!outchar(*str))
    {
        log_msg("write error %db, retrying...\n", len);
        flag_handshake();
        if (!outchar(*str))
        {

            log_msg("presistent write error: %d bytes not written.\n", len);
            ret = -1;
            flag_handshake();
            goto out;
        }
        log_msg("...oisdaun, ged eh!\n");
    }
    csent = 1;
    str++;
    len--;
    while (len--)
    {
        if (xQueueSend(tx_queue, str, 200 * portTICK_PERIOD_MS) != pdTRUE)
            log_msg("xQueueSend failed for %c, remaining: %d\n", *str, len);
        str++;
    }
    flag_handshake();
    float baud;
    if (xQueueReceive(s2_queue, &ret, portMAX_DELAY) == pdTRUE)
    {
        if (ret < 0)
        {
            log_msg("write error: %d\n", ret);
            goto out;
        }
    }
    t2 = millis();
    if (verbose)
    {
        log_msg("sent %d chars in ", ret);
        log_msg("%dms(", t2 - t1);
        baud = ((float)ret) / (t2 - t1) * 8000;
        log_msg("%.0f BAUD)\n", baud);
    }
out:
    setup_rcv();
    return ret;
}

void pp_drv::sync4write(void)
{
    unsigned long t = millis();
    flag_handshake();
    while (digitalRead(PA2) == HIGH)
    {
        delay(1);
        if ((millis() - t) > 500)
        {
            flag_handshake();
            t = millis();
            // log_msg("kicking handshake\n");
        }
    }
}
