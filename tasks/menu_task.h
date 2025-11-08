#ifndef MENU_TASK_H
#define MENU_TASK_H

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "control_task.h"

// Состояния меню
typedef enum {
    MENU_STATE_MAIN,          // Главное меню
    MENU_STATE_FILE_LIST,     // Список файлов
    MENU_STATE_FILE_CONFIRM,  // Подтверждение загрузки файла
    MENU_STATE_LOADING,       // Загрузка образа
    MENU_STATE_DISK_LOADED,   // Диск загружен - Eject Yes/No
    MENU_STATE_SD_INFO,       // Информация о SD карте
    MENU_STATE_ERROR          // Ошибка
} menu_state_t;

// Структура сообщения для menu_task
typedef struct {
    control_event_t event;
    uint32_t timestamp;
} menu_message_t;

// Глобальная очередь для menu_task
extern QueueHandle_t menu_queue;

// Функция создания задачи
void menu_task_init(void);

// Функция задачи
void menu_task(void *pvParameters);

#endif // MENU_TASK_H
