#ifndef CONTROL_TASK_H
#define CONTROL_TASK_H

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

// События от элементов управления
typedef enum {
    CONTROL_EVENT_UP,           // Вверх (кнопка или энкодер)
    CONTROL_EVENT_DOWN,         // Вниз (кнопка или энкодер)
    CONTROL_EVENT_OK,           // Нажатие кнопки OK/Enter
    CONTROL_EVENT_LONG_PRESS,   // Длительное нажатие
    CONTROL_EVENT_ENCODER_CW,   // Энкодер по часовой
    CONTROL_EVENT_ENCODER_CCW   // Энкодер против часовой
} control_event_t;

// Структура события управления
typedef struct {
    control_event_t event;
    uint32_t timestamp;
} control_message_t;

// Глобальная очередь для событий управления
extern QueueHandle_t control_queue;

// Функция создания задачи
void control_task_init(void);

// Функция задачи
void control_task(void *pvParameters);

#endif // CONTROL_TASK_H
