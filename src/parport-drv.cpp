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
#include <ctype.h>
#include "parport-drv.h"
#include "misc.h"
#include "logger.h"

//#define DUMP_TRAFFIC

inline void udelay(unsigned long us)
{
    delayMicroseconds(us);
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
    active_drv->pc2_isr_amiga();
}

void IRAM_ATTR pp_drv::isr_wrapper_select(void)
{
    active_drv->select_isr();
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
        //log_msg_isr(true, "SP2(LOW) isr - mode C64->ESP\n");
        setup_rcv(); // make sure I/Os are setup to input to avoid conflicts on lines
    }
    else
    {
        //log_msg_isr(true, "SP2(HIGH) isr - mode ESP->C64\n");
        if (in_write)   // race management for read/write conflict
        {
            log_msg_isr(true, "read/write race: SP2(HIGH) isr - mode ESP->C64\n");
            setup_snd();
        }
    }
}

void pp_drv::select_isr(void)
{
    // select signaled by Amiga:
    //   LOW: ESP -> Amiga
    //   HIGH: Amiga -> ESP 
    if (digitalRead(SELECT) == HIGH)
    {
        log_msg_isr(true, "SELECT(HIGH) isr - mode Amiga->ESP\n");
        //setup_rcv(); // make sure I/Os are setup to input to avoid conflicts on lines
    }
    else
    {
        log_msg_isr(true, "SELECT(LOW) isr - mode ESP->Amiga\n");
        if (in_write)   // race management for read/write conflict
        {
            log_msg_isr(true, "read/write race: SP2(HIGH) isr - mode ESP->C64\n");
            setup_snd();
        }
    }

}

void pp_drv::pc2_isr_amiga(void)
{
    int32_t err = 0;
    BaseType_t higherPriorityTaskWoken = pdFALSE;
    digitalWrite(BUSY, HIGH);   // tell we're busy now
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
        digitalWrite(FLAG, LOW); // start ACK
        UBaseType_t itemsWaiting = uxQueueMessagesWaitingFromISR(rx_queue);
        if ((qs - itemsWaiting) > 0)
        {
            // log_msg_isr(true, "%s: %c(0x%02x)\n", __FUNCTION__, (isprint(c) ? c : '~'), c);
            if (xQueueSendToBackFromISR(rx_queue, &c, &higherPriorityTaskWoken) == errQUEUE_FULL)
            {
                log_msg_isr(true, "PC2 ISR input queue full.\n");
                blink(150, 0); // signal that we've just discarded a char
            }
#if 1
            unsigned long to = micros();
            while (digitalRead(PC2) == LOW)
            {
                if ((micros() - to) > 500)
                {
                    log_msg_isr(true, "PC2 ISR input handshake (PA2==LOW) - host not responding (-1).\n");
                    err = -1; // nothing happens with err during INPUT
                    blink(150, 0);
                    break;
                }
            }
            // blink(50);
            digitalWrite(BUSY, LOW); // ok ready for the next byte
        }
        else
        {
            digitalWrite(2, HIGH);  // indicate congestion and wait until queue is sufficiently empty (hysteresis)
            block_ack = true;
        }
        digitalWrite(FLAG, HIGH);   // finish ACK
#else
        // C64 code
        unsigned long to = micros();
        while (digitalRead(PA2) == HIGH)
        {
            if ((micros() - to) > 500)
            {
                log_msg_isr(true, "PC2 ISR input handshake (PA2==HIGH) - C64 not responding (-1).\n");
                err = -1;   // nothing happens with err during INPUT
                // blink(150, 0);
                flag_handshake();
            }
        }
#endif        
        //blink(150, 0);
    }
    if (mode == OUTPUT)
    {   
        BaseType_t higherPriorityTaskWoken = pdFALSE;
        // log_msg_isr(true, "pc2 isr - output1 - %d\n", higherPriorityTaskWoken);
        char c;
        if (uxQueueMessagesWaitingFromISR(tx_queue) > 0)
        {
            if (xQueueReceiveFromISR(tx_queue, (void *)&c, &higherPriorityTaskWoken) == pdTRUE)
            {
                // log_msg_isr(true, "would send from ISR" + c + '\n');
                if (outchar(c, true))
                {
                    csent++;
                    flag_handshake();
                } 
                else
                {
                    log_msg_isr(true, "PC2 ISR write error (-1).\n");
                    err = -1;
                }
             
                unsigned long to = micros();
                while ((digitalRead(PA2) != HIGH) && ((micros() - to) < 2500))
                    ;
                if ((micros() - to) > 2000) // was 500, 1850 seen once.
                {
                    log_msg_isr(true, "PC2 ISR write handshake1 (PA==HIGH)- C64 not responding for %dus (-2).\n", micros() - to);
                    err = -2;
                }
                // log_msg_isr(true, "pc2 handshake 1 took %ldus\n", micros() - to);
            }
            else
            {
                err = -100;
                log_msg_isr(true, "PC2 ISR xQueueReceive failed (%d).\n", err);
            }
        }
        else
        {
            // log_msg_isr(true, "last char sent, releasing mutex\n");
            if (err < 0)
                log_msg_isr(true, "PC2 ISR write error, sent so far: %d bytes (%d). SHALL NEVER HAPPEN!!!", csent, err);

            // even sending 0 is OK, to enable retry
            if (xQueueSendToBackFromISR(s1_queue, &csent, &higherPriorityTaskWoken) == errQUEUE_FULL)
            {
                err = -101;
                log_msg_isr(true, "PC2 ISR can't release write (%d).\n", err);
            }
            csent = 0;
        }
#if 0 
        // removce me       
        if (0 && err >= 0)
        {
            unsigned long to = micros();
            while (digitalRead(PA2) != LOW)
            {
                if ((micros() - to) > 2250) // saw delays up to 1860us
                {
                    while (digitalRead(PA2) != LOW)
                        ;
                    err = -3;
                    log_msg_isr(true, "PC2 ISR write handshake2 (PA2==LOW) - C64 not responding for %dus(%d).\n", micros() - to, err);
                    break;
                }
            }
            // log_msg_isr(true, "pc2 ISR handshake 2 took %dus\n", micros() - to);
        }
#endif
        if ((err < 0) && uxQueueMessagesWaitingFromISR(tx_queue))
        {
            log_msg_isr(true, "discarding: \"");
            // in case of error empty queue
            while (uxQueueMessagesWaitingFromISR(tx_queue))
            {
                xQueueReceiveFromISR(tx_queue,
                                     (void *)&c,
                                     &higherPriorityTaskWoken);
                log_msg_isr(true, "%c(%02x), ", (isprint(c) ? c : '~'), c);
            }
            log_msg_isr(true, "\"\nISR emptied queue because of error %d.\n", err);
            // even sending 0 is OK, to enable retry
            if (xQueueSendToBackFromISR(s1_queue, &csent, &higherPriorityTaskWoken) == errQUEUE_FULL)
            {
                err = -101;
                log_msg_isr(true, "PC2 ISR can't release write (%d).\n", err);
            }
            csent = 0;
        }
        //udelay(25); // was 15, testing for soft80
    }
    if (higherPriorityTaskWoken != pdFALSE)
        portYIELD_FROM_ISR();
}

/*
 * member functions
 */
pp_drv::pp_drv(uint16_t qs, uint16_t bs)
    : qs(qs), bs(bs), verbose(false), in_write(false), to(DEFAULT_WTIMEOUT)
{
    rx_queue = xQueueCreate(qs, sizeof(char));
    tx_queue = xQueueCreate(qs, sizeof(char));
    s1_queue = xQueueCreate(1, sizeof(int32_t));
    s2_queue = xQueueCreate(1, sizeof(int32_t));
    active_drv = this;
    csent = 0;
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
    log_msg("driver(reader) launched at priority %d\n", uxTaskPriorityGet(nullptr));

    while (true)
    {
        while (xQueueReceive(rx_queue, &c, portMAX_DELAY) == pdTRUE)
        {
            //log_msg("pp - rcv: '%c'/0x%02x\n", (c? c : '~'), c);
            ring_buf.put(c);
            if (block_ack && (uxQueueSpacesAvailable( rx_queue ) > MAX_AUX - 1))
            {
                block_ack = false;
                digitalWrite(BUSY, LOW);
                digitalWrite(2, LOW);
                log_msg("%s: released block for sender, queue sufficiently free\n", __FUNCTION__);
            }
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
    //log_msg("C64 Terminal - sender");
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
    pinMode(PC2, INPUT_PULLUP);
    pinMode(SP2, INPUT);
    pinMode(SELECT, INPUT);
    pinMode(FLAG, OUTPUT);
    pinMode(2, OUTPUT);
    digitalWrite(FLAG, HIGH);
    mode = INPUT;
    in_write = false;
    csent = 0;
    to = DEFAULT_WTIMEOUT;
    xTaskCreate(th_wrapper1, "pp-drv-rcv", 4000, this, uxTaskPriorityGet(nullptr) + 1, &th1);
    xTaskCreate(th_wrapper2, "pp-drv-snd", 4000, this, uxTaskPriorityGet(nullptr) + 1, &th2);
    delay(50); // give logger time to setup everything before first interrupts happen
    attachInterrupt(digitalPinToInterrupt(SP2), isr_wrapper_sp2, CHANGE);
    attachInterrupt(digitalPinToInterrupt(SELECT), isr_wrapper_select, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PC2), isr_wrapper_pc2, FALLING);

    //pinMode(OE, OUTPUT);
    //digitalWrite(OE, HIGH);

    pinMode(BUSY, OUTPUT);
    digitalWrite(BUSY, LOW);

    setup_rcv();
}

void pp_drv::close(void)
{
    in_write = false;
    to = DEFAULT_WTIMEOUT;
    csent = 0;
    detachInterrupt(digitalPinToInterrupt(SP2));
    detachInterrupt(digitalPinToInterrupt(PC2));
    vTaskDelete(th1);
    vTaskDelete(th2);

    digitalWrite(OE, LOW);
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

#if 0
// also called from ISR context!
bool pp_drv::outchar(const char ct, bool from_isr)
{
    unsigned long t, t1;
    bool ret = true;
    //log_msg("%s: %c - ", __FUNCTION__, ct);
    t = micros();
    while (((digitalRead(SP2) == LOW) ||
            (digitalRead(PA2) == HIGH)) && 
            ((micros() - t) < 1000 * 5)) // allow 5ms to pass
        ;
    if ((t1 = (micros() - t)) > 100)
        log_msg_isr(from_isr, "outchar: C64 busy for %dus\n", t1);
    for (uint8_t s = _PB0; s <= _PB7; s++)
    {
        if ((digitalRead(SP2) == HIGH) && (digitalRead(PA2) == LOW))
        {
            uint8_t bit = (ct & (1 << s)) ? HIGH : LOW;
            //log_msg("%c", (bit ? '1' : '0'));
            digitalWrite(PAR(s), bit);
        }
        else
        {
            log_msg_isr(from_isr, "C64 SP2=%d (0 == C64 writing), PA2=%d (1 == C64 busy) - cowardly refusing to write.\n",
                        digitalRead(SP2), digitalRead(PA2)); 
            ret = false;
            goto out;

        }
    }
    // log_msg("\n");
    flag_handshake();
out:
    return ret;
}

#else
// also called from ISR context!
bool pp_drv::outchar(const char ct, bool from_isr)
{
    unsigned long t, t1;
    bool ret = true;
    //log_msg("%s: %c - ", __FUNCTION__, ct);
    for (uint8_t s = _PB0; s <= _PB7; s++)
    {
        t = micros();
        while ((digitalRead(SP2) == LOW) && ((micros() - t) < 1000 * 100)) // allow 100ms to pass
            ;
        if ((t1 = (micros() - t)) > 1000)
            log_msg_isr(from_isr, "outchar: C64 busy for %dus\n", t1);
        if (digitalRead(SP2) == HIGH)
        {
            uint8_t bit = (ct & (1 << s)) ? HIGH : LOW;
            //log_msg("%c", (bit ? '1' : '0'));
            digitalWrite(PAR(s), bit);
        }
        else
        {
            log_msg_isr(from_isr, "C64 SP2 low (=writing) - cowardly refusing to write.\n"); // may fail as ISR printfs ar no-good...
            ret = false;
            break;
        }
    }
    // log_msg("\n");
    return ret;
}
#endif

size_t pp_drv::write(const void *buf, size_t len)
{
    int32_t wlen = 0, ret;
    unsigned long t1, t2;

    t1 = millis();
    do {
        ret = _write((const char *)buf + wlen, len - wlen);
        if (ret < 0) 
        {
            log_msg("write error, ret = %d\n", ret);
            return wlen;
        }
        wlen += ret;
    } while (wlen < len);   // unless some real error arrives, retry until full length is written
    t2 = millis();
    if (verbose)
    {
        float baud;
        log_msg("sent %d chars in ", wlen);
        log_msg("%dms(", t2 - t1);
        baud = ((float)wlen) / (t2 - t1) * 8000;
        log_msg("%.0f BAUD)\n", baud);
    }
    return wlen;
}

size_t pp_drv::_write(const void *buf, size_t len)
{
    const char *str = static_cast<const char *>(buf);
    int32_t ret = -1;
    unsigned long t1, t2;
    int counter_PA2 = 0;
    int counter_SP2 = 0;
    //log_msg("write of %d chars\n", len);
    if ((len == 0) || (len >= qs))
        return -1;
    size_t save_len = len;
#ifdef DUMP_TRAFFIC
    char c = *str;
    char cd;
    if ((c == 0xa) || (c == 0xd))
        cd = '~';
    else
        cd = c;
    log_msg("0x%02x, /*'%c'*/\n", c, cd);
#endif
    bool was_busy = false;
    t1 = millis();
    t2 = micros();
    while (digitalRead(SP2) == LOW)
    {
        counter_SP2++;
        //log_msg("Input interfered: %d\n", counter_SP2);
        //udelay(25);
        was_busy = true;
        if ((millis() - t1) > 5000) // give up after 5s
        {
            log_msg("C64 reading too long, giving up writing...\n");
            ret = -1;
            goto out;
        }
    }
    t1 = millis();
    while (digitalRead(PA2) == HIGH)
    {
        counter_PA2++;
        //log_msg("PA2 == HIGH: %d\n", counter_PA2);
        //udelay(25);
        was_busy = true;
        if ((millis() - t1) > 5000) // give up after 5s
        {
            log_msg("C64 not responding, giving up...\n");
            ret = -1;
            goto out;
        }
    }
#if 0
    if (was_busy)
        log_msg("C64 was busy for %ldus: counter PA2=%d, counter SP2=%d.\n", micros() - t2, counter_PA2, counter_SP2);
#endif
    in_write = true;
    setup_snd();
    if (!outchar(*str, false))
    {
        log_msg("write error %db, retrying...\n", len);
        if (!outchar(*str, false))
        {
            ret = -4;
            log_msg("presistent write error: %d bytes not written (%d).\n", len, ret);
            goto out;
        }
        log_msg("...oisdaun, ged eh!\n");
    }
    csent = 1;
    str++;
    len--;
    while (len--)
    {
        if (xQueueSend(tx_queue, str, DEFAULT_WTIMEOUT) != pdTRUE)
            log_msg("xQueueSend failed for %c, remaining: %d\n", *str, len);
        str++;
    }
    flag_handshake();

    // qs is typically 8kB, with 60-64kBit/s -> 8kB/s -> ~1s maximum time.
    // in sync-mode even faster (x2)
    uint32_t _to;
    if (to == DEFAULT_WTIMEOUT)
    {
        uint32_t t = (save_len / 8) * portTICK_PERIOD_MS;
        _to = std::max(DEFAULT_WTIMEOUT, t);
    }
    else
        _to = to;
    
    if (xQueueReceive(s2_queue, &ret, _to) == pdTRUE)
    {
        if (ret < 0)
        {
            log_msg("write error: %d\n", ret);
        }
    }
    else
    {
        ret = csent - 1;
        log_msg("write error: failed to write %d bytes, timeout (%d)\n", save_len - csent + 1, ret);
    }
out:
    in_write = false;
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
