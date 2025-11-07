/**
 * @file UsbFloppyEmu.cpp
 * @brief USB Floppy Disk Drive Emulator - Main Entry Point
 * 
 * Эмулятор USB флоппи-дисковода на базе Raspberry Pi Pico
 * с использованием FreeRTOS для управления задачами.
 * 
 * @author USB Floppy Emulator Project
 * @date 2025
 */

#include <stdio.h>
#include "pico/stdlib.h"

// FreeRTOS
#include "FreeRTOS.h"
#include "task.h"

// Конфигурация проекта
#include "config.h"

// Все задачи системы
#include "tasks.h"

/**
 * @brief Основная функция программы
 * 
 * Инициализирует периферию Pico, создает все задачи FreeRTOS
 * и запускает планировщик.
 */
int main() {
    // Инициализация стандартного ввода/вывода (USB Serial)
    stdio_init_all();
    
    // Небольшая задержка для стабилизации USB
    sleep_ms(500);
    
    printf("\n");
    printf("========================================\n");
    printf("  USB Floppy Disk Drive Emulator\n");
    printf("  FreeRTOS @ %d Hz tick rate\n", configTICK_RATE_HZ);
    printf("  RP2040 @ %d MHz\n", configCPU_CLOCK_HZ / 1000000);
    printf("========================================\n");
    printf("\n");
    
    // Инициализация всех задач системы
    // Порядок важен - см. tasks.c
    tasks_init_all();
    
    printf("\n");
    printf("Starting FreeRTOS scheduler...\n");
    printf("\n");
    
    // Запуск планировщика FreeRTOS
    // Эта функция никогда не вернет управление
    vTaskStartScheduler();
    
    // Код ниже никогда не должен выполняться
    // Если мы здесь - что-то пошло не так
    printf("ERROR: Scheduler failed to start!\n");
    
    // Аварийное мигание LED
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    
    while (true) {
        gpio_put(PICO_DEFAULT_LED_PIN, 1);
        sleep_ms(100);
        gpio_put(PICO_DEFAULT_LED_PIN, 0);
        sleep_ms(100);
    }
    
    return 0;
}

/**
 * @brief Hook функция FreeRTOS - вызывается при нехватке памяти
 */
void vApplicationMallocFailedHook(void) {
    printf("FATAL: Malloc failed - out of heap memory!\n");
    taskDISABLE_INTERRUPTS();
    while (1) {
        // Аварийная остановка
    }
}

/**
 * @brief Hook функция FreeRTOS - вызывается при переполнении стека задачи
 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    (void)xTask;
    printf("FATAL: Stack overflow in task: %s\n", pcTaskName);
    taskDISABLE_INTERRUPTS();
    while (1) {
        // Аварийная остановка
    }
}
