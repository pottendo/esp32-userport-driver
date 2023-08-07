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
#include <stdarg.h>
#include <list>
#include "misc.h"
#include "logger.h"

static SemaphoreHandle_t log_mutex, log_sem;
static TaskHandle_t thlog_task, thlog_task4isr;
static std::list<String, Mallocator<String>> log_buffer;
//static std::list<String> log_buffer;
static QueueHandle_t log_queue;
#define LOG_BUF 256

void loop_log(void)
{
    //V(log_sem);
}

void log_task_func(void *p)
{
    printf("log task created with priority %d\n", uxTaskPriorityGet(nullptr));
    fflush(stdout);
    while (true)
    {
        xSemaphoreTake(log_sem, portMAX_DELAY);
        P(log_mutex);
        while (log_buffer.size())
        {
            const char *t = log_buffer.front().c_str();
            V(log_mutex);
            //printf(t);
            Serial.print(t);
            //Serial.flush();
            P(log_mutex);
            log_buffer.pop_front();
        }
        V(log_mutex);
    }
}

void log_task4isr(void *p)
{
    printf("log task4isr created with priority %d\n", uxTaskPriorityGet(nullptr));
    fflush(stdout);
    static char buf[LOG_BUF];
    while (true)
    {
        if (xQueueReceive(log_queue, &buf, portMAX_DELAY) == pdTRUE)
        {
            log_msg(buf);
        }
    }
}

void setup_log(void)
{
    log_mutex = xSemaphoreCreateBinary();
    V(log_mutex);
    log_sem = xSemaphoreCreateCounting(65536, 0);

    log_queue = xQueueCreate(20, 256);
    xTaskCreate(log_task_func, "logger", 4096, nullptr, PRIORITY_LOG, &thlog_task);
    xTaskCreate(log_task4isr, "log4isr", 2096, nullptr, PRIORITY_LOG + 1, &thlog_task4isr);
}

void log_msg(const String &s)
{
    P(log_mutex);
    //s += '\n';
    log_buffer.push_back(s);
    V(log_mutex);
    V(log_sem);
}

void log_msg(const char *s, ...)
{
    char t[256];
    va_list args;
    va_start(args, s);
    vsnprintf(t, 256, s, args);
    log_msg(String{t});
    //printf("%s", t);
}

void log_msg_isr(bool from_isr, BaseType_t *pw, const char *s, ...)
{
    char t[LOG_BUF];
    va_list args;
    va_start(args, s);
    vsnprintf(t, 256, s, args);
    if (from_isr)
        xQueueSendToBackFromISR(log_queue, t, pw);
    else
        xQueueSend(log_queue, t, 50 * portTICK_PERIOD_MS);
}

BaseType_t log_get_stack_wm(void)
{
    _FMUTEX(log_mutex);
    return uxTaskGetStackHighWaterMark(nullptr);
}
