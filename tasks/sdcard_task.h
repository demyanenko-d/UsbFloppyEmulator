#ifndef SDCARD_TASK_H
#define SDCARD_TASK_H

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include <stdbool.h>

// Команды для SD карты
typedef enum {
    SDCARD_CMD_INIT,            // Инициализация карты
    SDCARD_CMD_LIST_IMAGES,     // Получить список образов
    SDCARD_CMD_LOAD_IMAGE,      // Загрузить образ
    SDCARD_CMD_READ_SECTOR,     // Прочитать сектор
    SDCARD_CMD_WRITE_SECTOR,    // Записать сектор
    SDCARD_CMD_EJECT            // Извлечь диск
} sdcard_cmd_t;

// Структура сообщения для SD карты
typedef struct {
    sdcard_cmd_t command;
    union {
        char filename[64];
        struct {
            uint32_t sector;
            uint8_t *buffer;
        } sector_io;
    } data;
} sdcard_message_t;

// Структура ответа от SD карты
typedef struct {
    bool success;
    union {
        struct {
            char files[32][32];  // До 32 файлов
            uint8_t count;
        } file_list;
        struct {
            uint8_t data[512];
        } sector_data;
    } data;
} sdcard_response_t;

// Глобальная очередь для SD карты
extern QueueHandle_t sdcard_queue;

// Очередь для ответов
extern QueueHandle_t sdcard_response_queue;

// Функция создания задачи
void sdcard_task_init(void);

// Функция задачи
void sdcard_task(void *pvParameters);

// API функции
bool sdcard_is_initialized(void);
bool sdcard_read_sector(uint32_t sector, uint8_t *buffer);
bool sdcard_write_sector(uint32_t sector, const uint8_t *buffer);

#endif // SDCARD_TASK_H
