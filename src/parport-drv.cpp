#include <Arduino.h>

#include "parport-drv.h"
#include "misc.h"
#include "logger.h"

static void udelay(unsigned long us)
{
    unsigned long st = micros();
    while ((micros() - st) < us)
        ;
}

/*
 * static members
 */
pp_drv *pp_drv::active_drv = nullptr;
void IRAM_ATTR pp_drv::isr_wrapper_pa2(void)
{
    active_drv->pa2_isr();
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
void pp_drv::pa2_isr(void)
{
    return;

    // PA2 signaled by C64, LOW: ESP->C64, HIGH: C64->ESP
    if (digitalRead(PA2) == LOW)
    {
        //mode = OUTPUT;
        digitalWrite(LED_BUILTIN, LOW);
        log_msg_isr("pa2(LOW) isr - mode ESP->C64\n");
    }
    else
    {
        //mode = INPUT;
        digitalWrite(LED_BUILTIN, HIGH);
        log_msg_isr("pa2(HIGH) isr - mode C64->ESP\n");
    }
    for (uint8_t i = _PB0; i <= _PB7; i++)
    {
        pinMode(PAR(i), mode);
    }
}

/*
    static unsigned long sot = millis();
    if ((millis() - sot) > 50)
    {
        curr_char = 0;
    }
    */
//blink(100, 0);
//sot = millis();

static volatile bool not_ready = false;
/* ISRs */
void pp_drv::pc2_isr(void)
{
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
        //log_msg_isr("ps2isr: %c\n", c);
        if (xQueueSendToBackFromISR(rx_queue, &c, &higherPriorityTaskWoken) == errQUEUE_FULL)
            blink(150, 0); // signal that we've just discarded a char
        flag_handshake();
        if (higherPriorityTaskWoken)
            portYIELD_FROM_ISR();
    }
    if (mode == OUTPUT)
    {
        //log_msg_isr("pc2 isr - output\n");
        BaseType_t xTaskWokenByReceive = pdFALSE;
        char c;
        if (uxQueueMessagesWaitingFromISR(tx_queue) > 0)
        {
            if (xQueueReceiveFromISR(tx_queue,
                                     (void *)&c,
                                     &xTaskWokenByReceive) == pdTRUE)
            {
                //log_msg_isr("would send from ISR" + c + '\n');
                outchar(c);
                flag_handshake();
                udelay(50);
                while (digitalRead(PA2) != HIGH)
                    ;
            }
        }
        else
        {
            //log_msg_isr("last char sent, releasing mutex\n");
            V(isr_mutex);
        }
        if (xTaskWokenByReceive != pdFALSE)
            taskYIELD();

        while (digitalRead(PA2) != LOW)
            ;
        udelay(50);
    }
}

/*
 * member functions
 */
pp_drv::pp_drv(uint16_t qs, uint16_t bs)
    : qs(qs), bs(bs)
{
    mutex = xSemaphoreCreateBinary();
    V(mutex);
    out_mutex = xSemaphoreCreateBinary();
    isr_mutex = xSemaphoreCreateBinary();
    read_mutex = xSemaphoreCreateBinary();
    active_drv = this;
    rx_queue = xQueueCreate(qs, sizeof(char));
    tx_queue = xQueueCreate(qs, sizeof(char));
    xTaskCreate(th_wrapper1, "pp-drv-rcv", 4000, this, uxTaskPriorityGet(nullptr) + 1, &th);
    xTaskCreate(th_wrapper2, "pp-drv-snd", 2000, this, uxTaskPriorityGet(nullptr) + 1, &th);
}

pp_drv::~pp_drv()
{
    vSemaphoreDelete(mutex);
    vSemaphoreDelete(out_mutex);
    vQueueDelete(rx_queue);
    vQueueDelete(tx_queue);
    vTaskDelete(th);
}

void pp_drv::drv_body(void)
{
    char c, *buffer;
    uint16_t idx;
    log_msg("driver launched at priority %d\n", uxTaskPriorityGet(nullptr));

    buffer = new char[bs]; // never delete[]ed
    idx = 0;
    while (true)
    {
        while (xQueueReceive(rx_queue, &c, portMAX_DELAY) == pdTRUE)
        {
            ring_buf.put(c);
#if 0            
            //log_msg("pp - rcv: '%c'/0x%02x/%d\n", c, c, idx);
            buffer[idx++] = c;
            if (c == '\0')
            {
                String *line = new String{buffer};
                // log_msg("line: %s\n", line->c_str());
                idx = 0;
                P(mutex);
                rqueue.push_back(line);
                V(mutex);
            }
            if (idx >= bs)
            {
                log_msg("queue overflow, discarding: %s(%d)\n", buffer, idx);
                idx = 0;
            }
#endif
        }
    }
}

void pp_drv::drv_ackrcv(void)
{
    log_msg("drv rcv task launched with priority %d\n", uxTaskPriorityGet(nullptr));
    while (true)
    {
        P(isr_mutex);
        V(out_mutex);
    }
}

void pp_drv::setup_snd(void)
{
    //log_msg("C64 Terminal - sender");
    //detachInterrupt(digitalPinToInterrupt(PC2));
    for (uint8_t i = _PB0; i <= _PB7; i++)
    {
        pinMode(PAR(i), OUTPUT);
        digitalWrite(PAR(i), LOW);
    }
    mode = OUTPUT;
}

void pp_drv::setup_rcv(void)
{
    for (uint8_t i = _PB0; i <= _PB7; i++)
    {
        pinMode(PAR(i), INPUT);
    }
    mode = INPUT;
    //attachInterrupt(digitalPinToInterrupt(PC2), isr_wrapper_pc2, HIGH);
}

void pp_drv::open(void)
{
    blink(100, 3);
    pinMode(PA2, INPUT);
    pinMode(PC2, INPUT_PULLDOWN);
    pinMode(FLAG, OUTPUT);
    digitalWrite(FLAG, HIGH);
    mode = INPUT;
    //attachInterrupt(digitalPinToInterrupt(PA2), isr_wrapper_pa2, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PC2), isr_wrapper_pc2, HIGH);

    setup_rcv();
}

void pp_drv::close(void)
{
    blink(500, 2);
    //detachInterrupt(digitalPinToInterrupt(PA2));
    detachInterrupt(digitalPinToInterrupt(PC2));
}

int pp_drv::readstr(String **s)
{
    int av = 0;
    while (av == 0)
    {
        P(mutex);
        av = rqueue.size();
        V(mutex);
        delay(1);
    }
    P(mutex);
    *s = rqueue.front();
    rqueue.pop_front();
    V(mutex);
    return 0;
}

int pp_drv::read(char *buf, int len, bool block)
{
    int count = 0;
    char c;
    while (len && ring_buf.get(c, block))
    {
        buf[count++] = c;
        len--;
        if (c == '\0')
            break;
    }
    return count;
}

void pp_drv::outchar(const char ct) // also called from ISR context!
{
    for (uint8_t s = _PB0; s <= _PB7; s++)
    {
        uint8_t bit = (ct & (1 << s)) ? HIGH : LOW;
        digitalWrite(PAR(s), bit);
        //log_msg("%d", bit);
    }
}

int pp_drv::write(const char *str, int len)
{
    //log_msg("write of %d chars - %s\n", len, str);
    if (!len && (len > qs))
        return -1;
    setup_snd();
    if (len--)
    {
        while (digitalRead(PA2) == HIGH)
        {
            log_msg("c64 busy...\n");
            delay(500);
        }
        outchar(*str);
        str++;
    }
    while (len--)
    {
        if (xQueueSend(tx_queue, str, 200 * portTICK_PERIOD_MS) != pdTRUE)
            log_msg("xQueue failed for %c\n", *str);
        str++;
    }
    flag_handshake();
    log_msg("waiting for chars to be sent...\n");
    unsigned long t1 = millis();
    P(out_mutex);
    log_msg("out took: %dms\n", millis() - t1);
    log_msg("done.\n");

    //delay(20);
    setup_rcv();
    return 0;
}
