#ifndef USB_TASK_H
#define USB_TASK_H

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

// Команды для USB задачи
typedef enum {
    USB_CMD_MOUNT,      // Подключить диск
    USB_CMD_UNMOUNT,    // Отключить диск
    USB_CMD_EJECT       // Извлечь диск
} usb_cmd_t;

// Структура сообщения для USB
typedef struct {
    usb_cmd_t command;
} usb_message_t;

// Глобальная очередь для USB задачи
extern QueueHandle_t usb_queue;

// Функция создания задачи
void usb_task_init(void);

// Функция задачи
void usb_task(void *pvParameters);

#endif // USB_TASK_H
