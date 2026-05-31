/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2021 Raspberry Pi (Trading) Ltd.
 * Copyright (c) 2021 Peter Lawrence
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include "FreeRTOS.h"
#include "task.h"

#include <pico/stdlib.h>
#include <stdio.h>
#include <string.h>

#include "hardware/structs/usb.h"

#if PICO_SDK_VERSION_MAJOR >= 2
#include "bsp/board_api.h"
#else
#include "bsp/board.h"
#endif

#include "tusb.h"

#include "autobaud.h"
#include "cdc_perf.h"
#include "get_serial.h"
#include "probe.h"
#include "probe_config.h"

#if USB_DAP_ENABLE
#include "tusb_edpt_handler.h"
#include "DAP.h"
#endif

#include "cdc_uart_ate.h"
#include "cdc_uart_dut.h"
#include "i2c_bridge.h"

// UART0 for debugprobe debug
// UART1 for debugprobe to target device

#define THREADED 1

#define I2C_TASK_PRIO   (tskIDLE_PRIORITY + 2)
#define UART0_TASK_PRIO (tskIDLE_PRIORITY + 3)
#define UART1_TASK_PRIO (tskIDLE_PRIORITY + 3)
#define DAP_TASK_PRIO   (tskIDLE_PRIORITY + 1)

#define TUD_TASK_PRIO   (tskIDLE_PRIORITY + 2)
#define AUTOBAUD_TASK_PRIO  (tskIDLE_PRIORITY + 1)

TaskHandle_t tud_taskhandle, mon_taskhandle;

TaskHandle_t dap_taskhandle;
extern TaskHandle_t uart_dut_taskhandle;
extern TaskHandle_t uart_ate_taskhandle;
extern TaskHandle_t i2c_taskhandle;
extern uint8_t const desc_ms_os_20[];

#if !USB_DAP_ENABLE
static int was_configured;
#endif

void dev_mon(void *ptr)
{
    uint32_t sof[3];
    int i = 0;
    TickType_t wake;

    (void)ptr;

    wake = xTaskGetTickCount();
    do {
        xTaskDelayUntil(&wake, 100);
        if (tud_connected() && !tud_suspended()) {
            sof[i++] = usb_hw->sof_rd & USB_SOF_RD_BITS;
            i = i % 3;
        } else {
            for (i = 0; i < 3; i++) {
                sof[i] = 0;
            }
        }
        if ((sof[0] | sof[1] | sof[2]) != 0) {
            if ((sof[0] == sof[1]) && (sof[1] == sof[2])) {
                probe_info("Watchdog timeout! Resetting USBD\n");
                tud_deinit(0);
                xTaskDelayUntil(&wake, 1);
                tud_init(0);
            }
        }
    } while (1);
}

void tud_event_hook_cb(uint8_t rhport, uint32_t eventid, bool in_isr)
{
    BaseType_t blah;

    (void) rhport;
    (void) eventid;

    if (in_isr) {
        xTaskNotifyFromISR(tud_taskhandle, 0, 0, &blah);
    } else {
        xTaskNotify(tud_taskhandle, 0, 0);
    }
}

void usb_thread(void *ptr)
{
    uint32_t cmd;
    TickType_t wake;

    (void)ptr;

#ifdef PROBE_USB_CONNECTED_LED
    gpio_init(PROBE_USB_CONNECTED_LED);
    gpio_set_dir(PROBE_USB_CONNECTED_LED, GPIO_OUT);
#endif

    wake = xTaskGetTickCount();
    do {
#ifdef PROBE_TUSB_HW_Trigger
        gpio_put(PROBE_TUSB_HW_Trigger, 1);
#endif
        tud_task();
        cdc_perf_periodic_dump();
#ifdef PROBE_TUSB_HW_Trigger
        gpio_put(PROBE_TUSB_HW_Trigger, 0);
#endif

#ifdef PROBE_USB_CONNECTED_LED
        if (!gpio_get(PROBE_USB_CONNECTED_LED) && tud_ready())
            gpio_put(PROBE_USB_CONNECTED_LED, 1);
        else
            gpio_put(PROBE_USB_CONNECTED_LED, 0);
#endif

        if (tud_suspended() || !tud_connected())
            xTaskDelayUntil(&wake, 20);
        else if (!tud_task_event_ready())
            xTaskNotifyWait(0, 0xFFFFFFFFu, &cmd, 1);

    } while (1);
}

#if USB_DAP_ENABLE

int main(void)
{
    bi_decl_config();

    board_init();
#ifdef PROBE_TUSB_HW_Trigger
    gpio_init(PROBE_TUSB_HW_Trigger);
    gpio_set_dir(PROBE_TUSB_HW_Trigger, GPIO_OUT);
#endif

    usb_serial_init();
    cdc_uart_dut_init();
#if CDC_UART_ATE_ENABLE
    cdc_uart_ate_init();
#endif
    i2c_bridge_init();
    tusb_init();
    stdio_uart_init();

    DAP_Setup();

    probe_info("Welcome to debugprobe!\n");

    if (THREADED) {
        xTaskCreate(usb_thread, "TUD", configMINIMAL_STACK_SIZE, NULL, TUD_TASK_PRIO, &tud_taskhandle);
        xTaskCreate(cdc_uart_dut_thread, "UART_DUT", configMINIMAL_STACK_SIZE, NULL, UART1_TASK_PRIO, &uart_dut_taskhandle);
    #if CDC_UART_ATE_ENABLE
        xTaskCreate(cdc_uart_ate_thread, "UART_ATE", configMINIMAL_STACK_SIZE, NULL, UART0_TASK_PRIO, &uart_ate_taskhandle);
    #endif
        xTaskCreate(i2c_bridge_thread, "I2C", configMINIMAL_STACK_SIZE, NULL, I2C_TASK_PRIO, &i2c_taskhandle);
        xTaskCreate(autobaud_thread, "ABR", configMINIMAL_STACK_SIZE, NULL, AUTOBAUD_TASK_PRIO, &autobaud_taskhandle);
        xTaskCreate(dap_thread, "DAP", configMINIMAL_STACK_SIZE, NULL, DAP_TASK_PRIO, &dap_taskhandle);
        vTaskStartScheduler();
    }

    while (!THREADED) {
        tud_task();
        cdc_uart_dut_task();
    #if CDC_UART_ATE_ENABLE
        cdc_uart_ate_task();
    #endif
        i2c_bridge_task();
    }

    return 0;
}

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const * request)
{
    if (stage != CONTROL_STAGE_SETUP) return true;

    switch (request->bmRequestType_bit.type)
    {
        case TUSB_REQ_TYPE_VENDOR:
            switch (request->bRequest)
            {
                case 1:
                    if (request->wIndex == 7)
                    {
                        uint16_t total_len;
                        memcpy(&total_len, desc_ms_os_20 + 8, 2);
                        return tud_control_xfer(rhport, request, (void*) desc_ms_os_20, total_len);
                    }
                    return false;
                default:
                    break;
            }
            break;
        default:
            break;
    }

    return false;
}

#else

int main(void)
{
    bi_decl_config();

    board_init();
#ifdef PROBE_TUSB_HW_Trigger
    gpio_init(PROBE_TUSB_HW_Trigger);
    gpio_set_dir(PROBE_TUSB_HW_Trigger, GPIO_OUT);
#endif

    usb_serial_init();
    cdc_uart_dut_init();
#if CDC_UART_ATE_ENABLE
    cdc_uart_ate_init();
#endif
    i2c_bridge_init();

    tusb_init();
    stdio_uart_init();

    probe_info("Welcome to debugprobe!\n");

    if (THREADED) {
        xTaskCreate(usb_thread, "TUD", configMINIMAL_STACK_SIZE, NULL, TUD_TASK_PRIO, &tud_taskhandle);
#if (configNUMBER_OF_CORES > 1)
        vTaskCoreAffinitySet(tud_taskhandle, (1 << 0));
#endif
#if PICO_RP2040
        xTaskCreate(dev_mon, "WDOG", configMINIMAL_STACK_SIZE, NULL, TUD_TASK_PRIO, &mon_taskhandle);
#if (configNUMBER_OF_CORES > 1)
        vTaskCoreAffinitySet(mon_taskhandle, (1 << 0));
#endif
#endif
        vTaskStartScheduler();
    }

    while (!THREADED) {
        tud_task();
        cdc_uart_dut_task();
#if CDC_UART_ATE_ENABLE
        cdc_uart_ate_task();
#endif
        i2c_bridge_task();
    }

    return 0;
}

void tud_suspend_cb(bool remote_wakeup_en)
{
    (void)remote_wakeup_en;
    probe_info("Suspended\n");

    if (was_configured) {
        vTaskSuspend(uart_dut_taskhandle);
#if CDC_UART_ATE_ENABLE
        vTaskSuspend(uart_ate_taskhandle);
#endif
        vTaskSuspend(i2c_taskhandle);
        if (autobaud_running)
            autobaud_wait_stop();
        vTaskSuspend(autobaud_taskhandle);
    }
}

void tud_resume_cb(void)
{
    probe_info("Resumed\n");
    if (was_configured) {
        vTaskResume(uart_dut_taskhandle);
#if CDC_UART_ATE_ENABLE
        vTaskResume(uart_ate_taskhandle);
#endif
        vTaskResume(i2c_taskhandle);
        vTaskResume(autobaud_taskhandle);
    }
}

void tud_unmount_cb(void)
{
    probe_info("Disconnected\n");
    vTaskSuspend(uart_dut_taskhandle);
#if CDC_UART_ATE_ENABLE
    vTaskSuspend(uart_ate_taskhandle);
#endif
    vTaskSuspend(i2c_taskhandle);
    vTaskDelete(uart_dut_taskhandle);
#if CDC_UART_ATE_ENABLE
    vTaskDelete(uart_ate_taskhandle);
#endif
    vTaskDelete(i2c_taskhandle);
    if (autobaud_running)
        autobaud_wait_stop();
    vTaskSuspend(autobaud_taskhandle);
    vTaskDelete(autobaud_taskhandle);
    was_configured = 0;
}

void tud_mount_cb(void)
{
    probe_info("Connected, Configured\n");
    if (!was_configured) {
        xTaskCreate(cdc_uart_dut_thread, "UART_DUT", configMINIMAL_STACK_SIZE, NULL, UART1_TASK_PRIO, &uart_dut_taskhandle);
#if CDC_UART_ATE_ENABLE
        xTaskCreate(cdc_uart_ate_thread, "UART_ATE", configMINIMAL_STACK_SIZE, NULL, UART0_TASK_PRIO, &uart_ate_taskhandle);
#endif
        xTaskCreate(i2c_bridge_thread, "I2C", configMINIMAL_STACK_SIZE, NULL, I2C_TASK_PRIO, &i2c_taskhandle);
        xTaskCreate(autobaud_thread, "ABR", configMINIMAL_STACK_SIZE, NULL, AUTOBAUD_TASK_PRIO, &autobaud_taskhandle);
#if (configNUMBER_OF_CORES > 1)
        vTaskCoreAffinitySet(autobaud_taskhandle, (1 << 1));
        vTaskCoreAffinitySet(uart_dut_taskhandle, (1 << 0));
#if CDC_UART_ATE_ENABLE
        vTaskCoreAffinitySet(uart_ate_taskhandle, (1 << 0));
#endif
        vTaskCoreAffinitySet(i2c_taskhandle, (1 << 0));
#endif
        was_configured = 1;
    }
}

#endif

void vApplicationTickHook(void)
{
}

void vApplicationStackOverflowHook(TaskHandle_t Task, char *pcTaskName)
{
    panic("stack overflow (not the helpful kind) for %s\n", *pcTaskName);
}

void vApplicationMallocFailedHook(void)
{
    panic("Malloc Failed\n");
}
