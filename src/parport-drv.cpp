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
#include <Adafruit_SSD1306.h>
#include <ctype.h>
#include "parport-drv.h"
#include "misc.h"
#include "logger.h"
#include "ssd1306-display.h"

//#define DUMP_TRAFFIC
#ifdef AMIGA
#define WRIND SELECT
#define WRITING LOW
#define flag_handshake ack_handshake
#else
#define WRIND SP2
#define WRITING LOW
#endif

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
    active_drv->pc2_isr_c64();
}

void IRAM_ATTR pp_drv::isr_wrapper_strobe(void)
{
    active_drv->strobe_isr_amiga();
}

void IRAM_ATTR pp_drv::isr_wrapper_select(void)
{
    active_drv->select_isr();
}

void IRAM_ATTR pp_drv::isr_wrapper_reset(void)
{
    active_drv->reset_isr();
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
    if (digitalRead(WRIND) == WRITING)
    {
        //log_msg_isr(true, "SP2(LOW) isr - mode C64->ESP\n");
        setup_rcv(); // make sure I/Os are setup to input to avoid conflicts on lines
    }
    else
    {
        //log_msg_isr(true, "SP2(HIGH) isr - mode ESP->C64\n");
        if (in_write)   // race management for read/write conflict
        {
            //log_msg_isr(true, "read/write race: SP2(HIGH) isr - mode ESP->C64\n");
            setup_snd();
            digitalWrite(2, ~digitalRead(2));
        }
    }
}

void pp_drv::select_isr(void)
{
    // select signaled by Amiga:
    //   HIGH: Amiga writes
    //   LOW: Amiga idle or read
    if (digitalRead(SELECT) == HIGH)
    {
        //log_msg_isr(true, "SELECT(HIGH) isr - mode CBM->ESP\n");
        setup_rcv(); // make sure I/Os are setup to input to avoid conflicts on lines
    }
    else
    {
        //log_msg_isr(true, "SELECT(LOW) isr - mode ESP->CBM\n");
        if (in_write)   // race management for read/write conflict
        {
            //log_msg_isr(true, "read/write race: SELECT(LOW) isr - mode ESP->host\n");
            setup_snd();
        }
    }
}

void pp_drv::reset_isr(void)
{
    log_msg_isr(true, "Amiga RESET\n");
}

void pp_drv::pc2_isr_c64(void)
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
        //log_msg_isr(true, "%s: %c(0x%02x)\n", __FUNCTION__, (isprint(c) ? c : '~'), c);
        if (xQueueSendToBackFromISR(rx_queue, &c, &higherPriorityTaskWoken) == errQUEUE_FULL)
        {
            log_msg_isr(true, "PC2 ISR input queue full.\n");
        }
        flag_handshake();
#if 0        
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
    }
    if (mode == OUTPUT)
    {   
        BaseType_t higherPriorityTaskWoken = pdFALSE;
        // log_msg_isr(true, "pc2 isr - output1 - %d\n", higherPriorityTaskWoken);
        char c;
#if 0        
        unsigned long to = micros();
        while (0 && gpio_get_level(PC2) == 0) // wait until /STROBE is de-asserted
        {
            if ((micros() - to) > 500)
            {
                log_msg_isr(true, "/PC2 not deasserted for >500us.\n");
                break;
            }
            blink(0, 0);
            log_msg_isr(true, "/PC2 not deasserted...\n");
        }
#endif       
        if (uxQueueMessagesWaitingFromISR(tx_queue) > 0)
        {
            if (xQueueReceiveFromISR(tx_queue, (void *)&c, &higherPriorityTaskWoken) == pdTRUE)
            {
                //log_msg_isr(true, "would send from ISR '%c'\n", c);
                if (outchar(c, true))
                {
                    csent++;
                    //udelay(40);
                    flag_handshake();
                }
                else
                {
                    err = -EBUSY;
                    log_msg_isr(true, "PC2 ISR write error EBUSY (%d).\n", err);
                }
#if 0
                unsigned long to = micros();
                while ((digitalRead(PA2) != HIGH) && ((micros() - to) < 2500))
                    blink(0, 0);
                if ((micros() - to) > 2000) // was 500, 1850 seen once.
                {
                    log_msg_isr(true, "PC2 ISR write handshake1 (PA==HIGH)- C64 not responding for %dus (-2).\n", micros() - to);
                    err = -2;
                }
                // log_msg_isr(true, "pc2 handshake 1 took %ldus\n", micros() - to);
                while ((digitalRead(PA2) != LOW) && ((micros() - to) < 2500))
                    ;
                if ((micros() - to) > 2000) // was 500, 1850 seen once.
                {
                    log_msg_isr(true, "PC2 ISR write handshake1 (PA==LOW)- C64 not responding for %dus (-2).\n", micros() - to);
                    err = -2;
                }
#endif                
            }
            else
            {
                err = -EIO;
                log_msg_isr(true, "PC2 ISR xQueueReceive failed, EIO (%d).\n", err);
            }
        }
        else
        {
            // even sending 0 is OK, to enable retry
            if (xQueueSendToBackFromISR(s1_queue, &csent, &higherPriorityTaskWoken) == errQUEUE_FULL)
            {
                err = -EIO;
                log_msg_isr(true, "PC2 ISR can't release write, EIO (%d).\n", err);
            }
            csent = 0;
        }
        if (err < 0)
        {
            // in case of error empty queue
            while (uxQueueMessagesWaitingFromISR(tx_queue))
            {
                xQueueReceiveFromISR(tx_queue,
                                     (void *)&c,
                                     &higherPriorityTaskWoken);
            }
            log_msg_isr(true, "ISR emptied queue because of error %d.\n", err);
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

#ifdef NOT_OPTIMIZEIO
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
        unsigned long to = micros();
        while (digitalRead(PC2) == LOW) // wait until /STROBE is de-asserted
        {
            if ((micros() - to) > 500)
            {
                log_msg_isr(true, "/STROBE not deasserted for >500us.\n");
                //blink(150, 2);
                break;
            }
        }
        UBaseType_t fr, itemsWaiting = uxQueueMessagesWaitingFromISR(rx_queue);
        if ((fr = (qs - itemsWaiting)) > 0)
        {
            // log_msg_isr(true, "%s: %c(0x%02x)\n", __FUNCTION__, (isprint(c) ? c : '~'), c);
            if (xQueueSendToBackFromISR(rx_queue, &c, &higherPriorityTaskWoken) == errQUEUE_FULL)
            {
                log_msg_isr(true, "/STROBE ISR input queue full.\n");
                blink(150, 3); // signal that we've just discarded a char
            }

            // blink(50);
            if (fr == 1) // now nothing is free anymore
            {
                digitalWrite(2, HIGH); // indicate congestion and wait until queue is sufficiently empty (hysteresis)
                block_ack = true;
            }
            else
                digitalWrite(BUSY, LOW); // ok ready for the next byte
        }
        digitalWrite(FLAG, HIGH); // finish ACK
        // blink(150, 0);
    }
    if (mode == OUTPUT)
    {
        //log_msg_isr(true, "PC2 ISR write.\n");
        unsigned long to = micros();
        while (digitalRead(PC2) == LOW) // wait until /STROBE is de-asserted
        {
            if ((micros() - to) > 500)
            {
                log_msg_isr(true, "PC2 ISR input handshake (PA2==LOW) - host not responding (-1).\n");
                err = -1; 
                blink(150, 4);
                break;
            }
        }
        BaseType_t higherPriorityTaskWoken = pdFALSE;
        
        char c;
        if (uxQueueMessagesWaitingFromISR(tx_queue) > 0)
        {
            //log_msg_isr(true, "pc2 isr - output1 - %d pending\n", uxQueueMessagesWaitingFromISR(tx_queue));
            if (xQueueReceiveFromISR(tx_queue, (void *)&c, &higherPriorityTaskWoken) == pdTRUE)
            {
                //log_msg_isr(true, "would send from ISR" + c + '\n');
                if (outchar(c, true))
                {
                    csent++;
                    flag_handshake();
                } 
                else
                {
                    log_msg_isr(true, "PC2 ISR write error (-1).\n");
                    err = -1;
                    digitalWrite(BUSY, LOW); // ok ready for the next byte
                }
            }
            else
            {
                err = -100;
                log_msg_isr(true, "PC2 ISR xQueueReceive failed (%d).\n", err);
                digitalWrite(BUSY, LOW); // ok ready for the next byte
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
            digitalWrite(BUSY, LOW); // ok ready for the next byte
        }
        if (err < 0)
        {
            UBaseType_t no = uxQueueMessagesWaitingFromISR(tx_queue);
            // in case of error empty queue
            log_msg_isr(true, "ISR emptied queue (%d bytes) because of error %d.\n", no, err);
            while (no--)
            {
                xQueueReceiveFromISR(tx_queue,
                                     (void *)&c,
                                     &higherPriorityTaskWoken);
                //log_msg_isr(true, "%c(%02x), ", (isprint(c) ? c : '~'), c);
            }
            if (xQueueSendToBackFromISR(s1_queue, &csent, &higherPriorityTaskWoken) == errQUEUE_FULL)
            {
                err = -101;
                log_msg_isr(true, "PC2 ISR can't release write (%d).\n", err);
            }
            csent = 0;
            digitalWrite(BUSY, LOW); // ok ready for the next byte
        }
        //udelay(25); // was 15, testing for soft80
    }
    if (higherPriorityTaskWoken != pdFALSE)
        portYIELD_FROM_ISR();
}
#else

void pp_drv::strobe_isr_amiga(void)
{
    int32_t err = 0;
    BaseType_t higherPriorityTaskWoken = pdFALSE;
    gpio_set_level(BUSY, 1);   // tell we're busy now
    if (mode == INPUT)
    {
        char c1 = 0;
        int i;

        for (i = _PB7; i >= _PB0; i--)
        {
            char b1 = (gpio_get_level(PAR(i)) == 0) ? 0 : 1;
            c1 = (c1 << 1) | b1;
        }
        //log_msg_isr(true, "%s: %c(0x%02x)\n", __FUNCTION__, (isprint(c1) ? c1 : '~'), c1);
        gpio_set_level(FLAG, 0); // start ACK
        unsigned long to = micros();
        while (gpio_get_level(PC2) == 0) // wait until /STROBE is de-asserted
        {
            if ((micros() - to) > 500)
            {
                log_msg_isr(true, "/STROBE not deasserted for >500us.\n");
                //blink(150, 2);
                break;
            }
        }
        UBaseType_t fr, itemsWaiting = uxQueueMessagesWaitingFromISR(rx_queue);
        if ((fr = (qs - itemsWaiting)) > 0)
        {
            if (xQueueSendToBackFromISR(rx_queue, &c1, &higherPriorityTaskWoken) == errQUEUE_FULL)
            {
                log_msg_isr(true, "/STROBE ISR input queue full.\n");
                //blink(150, 3);
            }
            if (fr == 1)
            {
                gpio_set_level(static_cast<gpio_num_t>(2), 1); // indicate congestion
                block_ack = true;
            }
            else
                gpio_set_level(BUSY, 0); // ok ready for the next byte
        }
        gpio_set_level(FLAG, 1); // finish ACK
    }
    if (mode == OUTPUT)
    {
        unsigned long to = micros();
        while (gpio_get_level(PC2) == 0) // wait until /STROBE is de-asserted
        {
            if ((micros() - to) > 500)
            {
                log_msg_isr(true, "PC2 ISR input handshake (PA2==LOW) - host not responding (-1).\n");
                err = -1; 
                //blink(150, 4);
                break;
            }
        }
        BaseType_t higherPriorityTaskWoken = pdFALSE;
        //log_msg_isr(true, "csent %d bytes (err = %d)\n", csent, err);
        char c;
        if (uxQueueMessagesWaitingFromISR(tx_queue) > 0)
        {
            if (xQueueReceiveFromISR(tx_queue, (void *)&c, &higherPriorityTaskWoken) == pdTRUE)
            {
                //log_msg_isr(true, "%s: %c(0x%02x)\n", __FUNCTION__, (isprint(c) ? c : '~'), c);
                if (outchar(c, true))
                {
                    csent++;
                    flag_handshake();
                } 
                else
                {
                    log_msg_isr(true, "PC2 ISR write error (-1).\n");
                    err = -1;
                    gpio_set_level(BUSY, 0);
                }
            }
            else
            {
                err = -100;
                log_msg_isr(true, "PC2 ISR xQueueReceive failed (%d).\n", err);
                gpio_set_level(BUSY, 0);
            }
        }
        else
        {
            if (err < 0)
                log_msg_isr(true, "PC2 ISR write error, sent so far: %d bytes (%d). SHALL NEVER HAPPEN!!!", csent, err);

            if (xQueueSendToBackFromISR(s1_queue, &csent, &higherPriorityTaskWoken) == errQUEUE_FULL)
            {
                err = -101;
                log_msg_isr(true, "PC2 ISR can't release write (%d).\n", err);
            }
            csent = 0;
            gpio_set_level(BUSY, 0);
        }
        if (err < 0)
        {
            UBaseType_t no = uxQueueMessagesWaitingFromISR(tx_queue);
            log_msg_isr(true, "ISR emptied queue (%d bytes) because of error %d.\n", no, err);
            while (no--)
            {
                xQueueReceiveFromISR(tx_queue, (void *)&c, &higherPriorityTaskWoken);
            }
            if (xQueueSendToBackFromISR(s1_queue, &csent, &higherPriorityTaskWoken) == errQUEUE_FULL)
            {
                err = -101;
                log_msg_isr(true, "PC2 ISR can't release write (%d).\n", err);
            }
            csent = 0;
            gpio_set_level(BUSY, 0);
        }
    }
    if (higherPriorityTaskWoken != pdFALSE)
        portYIELD_FROM_ISR();
}
#endif // NOT_OPTIMIZEIO

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

#ifdef NOT_OPTIMIZEIO
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
#else
#include "driver/gpio.h"

void pp_drv::setup_snd(void)
{
    digitalWrite(BUSY, HIGH);
#pragma GCC unroll 8
    for (uint8_t i = _PB0; i <= _PB7; i++)
    {
        gpio_set_direction(PAR(i), GPIO_MODE_OUTPUT);
        gpio_set_level(PAR(i), 0);
    }
    mode = OUTPUT;
    //log_msg_isr(true, "%s: set to SEND mode\n", __FUNCTION__);
}

void pp_drv::setup_rcv(void)
{
#pragma GCC unroll 8    
    for (uint8_t i = _PB0; i <= _PB7; i++)
    {
        gpio_set_direction(PAR(i), GPIO_MODE_INPUT);
        // Optionally set pull mode if needed:
        gpio_set_pull_mode(PAR(i), GPIO_FLOATING);
    }
    mode = INPUT;
    digitalWrite(BUSY, LOW);
    //log_msg_isr(true, "%s: set to RECEIVE mode\n", __FUNCTION__);
}

void pp_drv::open(void)
{
    // One-time pad selection for parallel port pins
    for (uint8_t i = _PB0; i <= _PB7; i++)
    {
        gpio_pad_select_gpio(PAR(i));
    }

    // Other pins
    gpio_pad_select_gpio(PA2);
    gpio_pad_select_gpio(PC2);
    gpio_pad_select_gpio(SP2);
    gpio_pad_select_gpio(SELECT);
    gpio_pad_select_gpio(FLAG);
    gpio_pad_select_gpio(2);
    gpio_pad_select_gpio(BUSY);
    gpio_pad_select_gpio(POUT);
    gpio_pad_select_gpio(RESET);

    // Now set directions and pull modes for non-parallel pins
    gpio_set_direction(PA2, GPIO_MODE_INPUT);
    gpio_set_direction(PC2, GPIO_MODE_INPUT);
    gpio_set_pull_mode(PC2, GPIO_PULLUP_ONLY);
    gpio_set_direction(SP2, GPIO_MODE_INPUT);
    gpio_set_direction(SELECT, GPIO_MODE_INPUT);
    gpio_set_direction(RESET, GPIO_MODE_INPUT);
    gpio_set_pull_mode(RESET, GPIO_PULLDOWN_ONLY);

    gpio_set_direction(FLAG, GPIO_MODE_OUTPUT);
    gpio_set_direction(static_cast<gpio_num_t>(2), GPIO_MODE_OUTPUT);
    gpio_set_direction(BUSY, GPIO_MODE_OUTPUT);
    //gpio_set_direction(POUT, GPIO_MODE_OUTPUT);
    gpio_set_level(FLAG, 1);
    gpio_set_level(BUSY, 0);
    //gpio_set_level(POUT, 0);

    mode = INPUT;
    in_write = false;
    csent = 0;
    to = DEFAULT_WTIMEOUT;
    xTaskCreate(th_wrapper1, "pp-drv-rcv", 4000, this, uxTaskPriorityGet(nullptr) + 1, &th1);
    xTaskCreate(th_wrapper2, "pp-drv-snd", 4000, this, uxTaskPriorityGet(nullptr) + 1, &th2);
    delay(50); // give logger time to setup everything before first interrupts happen
    is_amiga = false;
    char *machine;
    if (gpio_get_level(RESET) == HIGH)
    {
        log_msg("Amiga detected...\n");
        is_amiga = true;
        machine = (char *)"Amiga";
        attachInterrupt(digitalPinToInterrupt(SELECT), isr_wrapper_select, CHANGE);
        //attachInterrupt(digitalPinToInterrupt(RESET), isr_wrapper_reset, FALLING); // doesn't work on Amiga, so not used
    }
    else
    {
        log_msg("C64 detected...\n");
        machine = (char *)"C64";
        attachInterrupt(digitalPinToInterrupt(SP2), isr_wrapper_sp2, CHANGE);
        lcd->orientation(2); // upside down for C64
    }
    lcd->printf("%s detected...\n", machine);

    attachInterrupt(digitalPinToInterrupt(PC2), isr_wrapper_pc2, FALLING);

    setup_rcv();

}
#endif


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
#ifdef NOT_OPTIMIZEIO
// also called from ISR context!
bool pp_drv::outchar(const char ct, bool from_isr)
{
    unsigned long t, t1;
    bool ret = true;
    //log_msg("%s: %c - ", __FUNCTION__, ct);
    for (uint8_t s = _PB0; s <= _PB7; s++)
    {
        t = micros();
        while ((digitalRead(WRIND) == WRITING) && ((micros() - t) < 1000 * 100)) // allow 100ms to pass
            ;
        if ((t1 = (micros() - t)) > 1000)
            log_msg_isr(from_isr, "outchar: host busy for %dus\n", t1);
        if (digitalRead(WRIND) != WRITING)
        {
            uint8_t bit = (ct & (1 << s)) ? HIGH : LOW;
            //log_msg("%c", (bit ? '1' : '0'));
            digitalWrite(PAR(s), bit);
        }
        else
        {
            log_msg_isr(from_isr, "host ist writing - cowardly refusing to write.\n"); // may fail as ISR printfs ar no-good...
            ret = false;
            break;
        }
    }
    //log_msg("\n");
    return ret;
}

#else
// also called from ISR context!
bool pp_drv::outchar(const char ct, bool from_isr)
{
    unsigned long t, t1;
    bool ret = true;
    for (uint8_t s = _PB0; s <= _PB7; s++)
    {
        t = micros();
        while ((gpio_get_level(WRIND) == WRITING) && ((micros() - t) < 1000 * 100)) // allow 100ms to pass
            ;
        if ((t1 = (micros() - t)) > 1000)
            log_msg_isr(from_isr, "outchar: host busy for %dus\n", t1);
        if (gpio_get_level(WRIND) != WRITING)
        {
            uint8_t bit = (ct & (1 << s)) ? 1 : 0;
            //log_msg("%c", (bit ? '1' : '0'));
            gpio_set_level(PAR(s), bit);
        }
        else
        {
            log_msg_isr(from_isr, "host is writing - cowardly refusing to write.\n");
            ret = false;
            break;
        }
    }
    //log_msg("\n");
    return ret;
}
#endif
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
            //log_msg("write error, ret = %d\n", ret);
            return ret;
        }
        wlen += ret;
        if (wlen < len)
            log_msg("write: only %d of %d bytes written, retrying...\n", wlen, len);
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
    while (digitalRead(WRIND) == WRITING)
    {
        counter_SP2++;
        was_busy = true;
        if ((millis() - t1) > 5000) // give up after 5s
        {
            log_msg("waiting for host to read...\n");
            t1 = millis();
            //ret = -1;
            //goto out;
        }
    }
#if 1
    if (!is_amiga)
    {
        t1 = millis();
        while (digitalRead(PA2) == HIGH)
        {
            counter_PA2++;
            // log_msg("PA2 == HIGH: %d\n", counter_PA2);
            // udelay(25);
            was_busy = true;
            if ((millis() - t1) > 5000) // give up after 5s
            {
                log_msg("C64 not responding, giving up...\n");
                ret = -1;
                goto out;
            }
        }
    }
#endif    
#if 0
    if (was_busy)
        log_msg("C64 was busy for %ldus: counter PA2=%d, counter SP2=%d.\n", micros() - t2, counter_PA2, counter_SP2);
#endif
    in_write = true;
    setup_snd();
    csent = 0;
    if (!outchar(*str, false))
    {
        log_msg("write error %db, retrying...\n", len);
        if (!outchar(*str, false))
        {
            ret = -EBUSY;
            log_msg("presistent write error: %d bytes not written (%d).\n", len, ret);
            goto out;
        }
        log_msg("...oisdaun, ged eh!\n");
    }
    csent++;
    len--;
    str++;
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
        uint32_t t = (save_len / 4) * portTICK_PERIOD_MS;
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
        char c;
        ret = csent - 1;
        log_msg("write error: failed to write %d bytes (%db sent), timeout, discarding\n", save_len - csent + 1, ret);
        if (ret == 0) 
            ret = -ETIMEDOUT;
        while (uxQueueMessagesWaiting(tx_queue))
            xQueueReceive(tx_queue, (void *)&c, portTICK_PERIOD_MS);
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
