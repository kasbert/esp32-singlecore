#include <stdio.h>

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "soc/timer_group_struct.h"
#include "soc/timer_group_reg.h"
#include "freertos/portmacro.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"
#include <soc/rtc.h>
#include "esp_system.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/uart.h"
//#include "driver/timer.h"
//#include "esp32/rom/uart.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"

#include <esp_log.h>
#include <sys/param.h>
#include "nvs_flash.h"

#include "soc/soc.h"
#include "soc/gpio_reg.h"

#include <rom/gpio.h>
#include <esp_attr.h>

static const char *TAG = "example";

#define STACK_SIZE 4096

TaskHandle_t xHandle1;
TaskHandle_t xHandle2;
TaskHandle_t TaskA;

// https://sub.nanona.fi/esp8266/timing-and-ticks.html
inline int32_t asm_ccount(void) {
    int32_t r;
    asm volatile ("rsr %0, ccount" : "=r"(r));
    return r;
}

inline uint64_t millis()
{
    return esp_timer_get_time() / 1000l;
}

inline uint64_t micros()
{
    return esp_timer_get_time();
}

/*------------------------------------------------------------------
Core1:
This task runs on core 1 without any interruption:
No taskswitch
No other interrupts
------------------------------------------------------------------*/

uint64_t sum;
uint32_t min = 10000;
uint32_t max, count, late;
volatile uint8_t stop, stopped;
uint16_t lates[1000];

void IRAM_ATTR do_work() {
    // Couple of us or something
    for (volatile int i = 0; i < 14; i++) {
        asm(" nop");
    }
}

#include "xtensa_timer.h"
#include "esp_intr_alloc.h"


void IRAM_ATTR Core1(void *p)
{
    ESP_LOGI(TAG, "Start Core 1\n");
    esp_intr_dump(stdout);

    // I do not want an RTOS-Tick here
    portDISABLE_INTERRUPTS();

    // does not help
    esp_intr_disable_source(0); // FROM_CPU_INTR1
    esp_intr_disable_source(1); // SYSTIMER_TARGET1

    // does not help
    //ESP_INTR_DISABLE(XT_TIMER_INTNUM);
    ESP_INTR_DISABLE(0);
    ESP_INTR_DISABLE(1);

    volatile uint32_t t0, t1;
    while (1) {
        if (stop) {
            stopped = 1;
            while (stop);
            stopped = 0;
        }
        t0 = asm_ccount();
        do_work();
        t1 = asm_ccount();
        uint32_t delta = t1 - t0;
        if (delta > max) max = delta;
        if (delta < min) min = delta;
        sum += delta;
        if (delta > min) {
            late++;
            if (delta < 1000)
                lates[delta]++;
        }
        count++;
    }
}

/*------------------------------------------------------------------
RTOS1:
------------------------------------------------------------------*/
void IRAM_ATTR RTOS_1(void *p)
{
    // esp_task_wdt_deinit();
    ESP_LOGI(TAG, "Start Core 0 RTOS_1\n");

    int c = 0;
    while (1)
    {
        vTaskDelay(CONFIG_FREERTOS_HZ * 10); // 10s
        // esp_task_wdt_reset();
        stop = 1; // stop the work process
        while (!stopped); // wait until stopped
        ESP_LOGI(TAG, "RTOS-1 count %lu, min %lu avg %llu max %lu late %lu", 
            count, min, count ? (sum/count) : 0, max, late);
        for (int i = min; i < 1000 && i <= max; i++) {
            if (lates[i]) {
                ESP_LOGI(TAG, " Late %d: count %d", i, lates[i]);
                lates[i] = 0;
            }
        }
        min = max;
        sum = late = max = count = 0;
        stop = 0; // release the work process
        c++;
        //taskYIELD();
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // ------------------- Create RTOS-Tasks ------------------------------------//

    xTaskCreatePinnedToCore(
        RTOS_1,     // Function that implements the task.
        "RTOS-1",   // Text name for the task.
        STACK_SIZE, // Stack size in bytes, not words.
        (void *)1,  // Parameter passed into the task.
        tskIDLE_PRIORITY + 2,
        &xHandle1, // Variable to hold the task's data structure.
        0);

    // Start the one and only task in core 1
    xTaskCreatePinnedToCore(
        Core1,      // Function that implements the task.
        "Core1",    // Text name for the task.
        STACK_SIZE, // Stack size in bytes, not words.
        (void *)1,  // Parameter passed into the task.
        24,
        &TaskA,
        1); // Core 1
}