#ifndef LED_TASK_H
#define LED_TASK_H

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

// Режимы работы светодиода
typedef enum {
    LED_MODE_OFF,           // Выключен
    LED_MODE_ON,            // Постоянно включен
    LED_MODE_BLINK_SLOW,    // Медленное мигание (1 Гц)
    LED_MODE_BLINK_FAST,    // Быстрое мигание (5 Гц)
    LED_MODE_PULSE,         // Пульсация
    LED_MODE_ACTIVITY       // Мигание при активности
} led_mode_t;

// Структура сообщения для LED
typedef struct {
    led_mode_t mode;
    uint32_t duration_ms;   // Длительность режима (0 = бесконечно)
} led_message_t;

// Глобальная очередь для LED задачи
extern QueueHandle_t led_queue;

// Функция создания задачи
void led_task_init(void);

// Функция задачи
void led_task(void *pvParameters);

// API функции для упрощенного управления
void led_set_mode(led_mode_t mode);
void led_activity(void);  // Короткая вспышка для индикации активности

#endif // LED_TASK_H
