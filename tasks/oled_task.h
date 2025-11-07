#ifndef OLED_TASK_H
#define OLED_TASK_H

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

// Команды для OLED дисплея
typedef enum {
    OLED_CMD_CLEAR,
    OLED_CMD_UPDATE_MENU,
    OLED_CMD_SHOW_MESSAGE,
    OLED_CMD_SHOW_STATUS,
    OLED_CMD_POWER_ON,
    OLED_CMD_POWER_OFF
} oled_cmd_t;

// Структура сообщения для OLED
typedef struct {
    oled_cmd_t command;
    union {
        struct {
            char items[4][32];      // До 4 пунктов меню (для 64px) или 2 для 32px
            uint8_t item_count;
            uint8_t selected_index;
        } menu;
        struct {
            char text[128];
            uint8_t line;
        } message;
        struct {
            char status_line1[32];
            char status_line2[32];
        } status;
    } data;
} oled_message_t;

// Глобальная очередь для OLED задачи
extern QueueHandle_t oled_queue;

// Функция создания задачи
void oled_task_init(void);

// Функция задачи
void oled_task(void *pvParameters);

#endif // OLED_TASK_H
