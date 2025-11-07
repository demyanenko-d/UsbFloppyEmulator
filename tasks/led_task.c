#include "led_task.h"
#include "pico/stdlib.h"
#include <stdio.h>

// Очередь для команд
QueueHandle_t led_queue = NULL;

// Пин светодиода (встроенный на Pico)
#define LED_PIN PICO_DEFAULT_LED_PIN

// Текущий режим
static led_mode_t current_mode = LED_MODE_OFF;
static uint32_t mode_start_time = 0;
static uint32_t mode_duration = 0;

/**
 * @brief Инициализация GPIO для LED
 */
static void led_init_gpio(void) {
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 0);
    
    printf("[LED] GPIO initialized (pin: %d)\n", LED_PIN);
}

/**
 * @brief Установка состояния LED
 */
static void led_set_state(bool on) {
    gpio_put(LED_PIN, on ? 1 : 0);
}

/**
 * @brief Обработка различных режимов LED
 */
static void led_update(void) {
    static uint32_t last_toggle = 0;
    static bool led_state = false;
    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    
    // Проверка истечения времени режима
    if (mode_duration > 0) {
        if (current_time - mode_start_time >= mode_duration) {
            current_mode = LED_MODE_OFF;
            mode_duration = 0;
        }
    }
    
    switch (current_mode) {
        case LED_MODE_OFF:
            led_set_state(false);
            break;
            
        case LED_MODE_ON:
            led_set_state(true);
            break;
            
        case LED_MODE_BLINK_SLOW:
            // Мигание 1 Гц (500мс вкл, 500мс выкл)
            if (current_time - last_toggle >= 500) {
                led_state = !led_state;
                led_set_state(led_state);
                last_toggle = current_time;
            }
            break;
            
        case LED_MODE_BLINK_FAST:
            // Мигание 5 Гц (100мс вкл, 100мс выкл)
            if (current_time - last_toggle >= 100) {
                led_state = !led_state;
                led_set_state(led_state);
                last_toggle = current_time;
            }
            break;
            
        case LED_MODE_PULSE:
            // Простая пульсация (можно улучшить с PWM)
            if (current_time - last_toggle >= 50) {
                led_state = !led_state;
                led_set_state(led_state);
                last_toggle = current_time;
            }
            break;
            
        case LED_MODE_ACTIVITY:
            // Короткая вспышка
            if (current_time - mode_start_time < 50) {
                led_set_state(true);
            } else {
                led_set_state(false);
                current_mode = LED_MODE_OFF;
            }
            break;
            
        default:
            led_set_state(false);
            break;
    }
}

/**
 * @brief Смена режима работы LED
 */
static void led_change_mode(led_mode_t new_mode, uint32_t duration) {
    current_mode = new_mode;
    mode_start_time = to_ms_since_boot(get_absolute_time());
    mode_duration = duration;
    
    printf("[LED] Mode changed to: %d, duration: %lu ms\n", new_mode, duration);
}

/**
 * @brief Основная функция задачи LED
 */
void led_task(void *pvParameters) {
    (void)pvParameters;
    
    printf("[LED] Task started\n");
    
    // Инициализация GPIO
    led_init_gpio();
    
    // Приветственная анимация - быстрое мигание 3 раза
    for (int i = 0; i < 3; i++) {
        led_set_state(true);
        vTaskDelay(pdMS_TO_TICKS(100));
        led_set_state(false);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    led_message_t msg;
    
    while (1) {
        // Проверка новых команд из очереди
        if (xQueueReceive(led_queue, &msg, 0) == pdTRUE) {
            led_change_mode(msg.mode, msg.duration_ms);
        }
        
        // Обновление состояния LED
        led_update();
        
        // Обновление каждые 10мс (достаточно для визуальной плавности)
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/**
 * @brief Инициализация задачи LED
 */
void led_task_init(void) {
    // Создание очереди для команд
    led_queue = xQueueCreate(5, sizeof(led_message_t));
    
    if (led_queue == NULL) {
        printf("[LED] Failed to create queue!\n");
        return;
    }
    
    // Создание задачи с низким приоритетом
    BaseType_t result = xTaskCreate(
        led_task,               // Функция задачи
        "LED",                  // Имя задачи
        256,                    // Размер стека
        NULL,                   // Параметры
        tskIDLE_PRIORITY + 1,   // Низкий приоритет
        NULL                    // Дескриптор задачи
    );
    
    if (result != pdPASS) {
        printf("[LED] Failed to create task!\n");
    } else {
        printf("[LED] Task created successfully\n");
    }
}

/**
 * @brief API: Установить режим LED
 */
void led_set_mode(led_mode_t mode) {
    led_message_t msg;
    msg.mode = mode;
    msg.duration_ms = 0;  // Бесконечно
    
    xQueueSend(led_queue, &msg, 0);
}

/**
 * @brief API: Короткая вспышка активности
 */
void led_activity(void) {
    led_message_t msg;
    msg.mode = LED_MODE_ACTIVITY;
    msg.duration_ms = 50;
    
    xQueueSend(led_queue, &msg, 0);
}
