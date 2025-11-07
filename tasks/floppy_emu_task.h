#ifndef FLOPPY_EMU_TASK_H
#define FLOPPY_EMU_TASK_H

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include <stdbool.h>
#include <stdint.h>

// Размеры флоппи-диска 1.44MB
#define FLOPPY_SECTOR_SIZE      512
#define FLOPPY_SECTORS          2880  // 1.44MB / 512 bytes
#define FLOPPY_FAT12_SECTORS    33    // FAT12: boot sector + 2 FATs (9 sectors each) + root dir (14 sectors)

// Конфигурация кеша
#define CACHE_TOTAL_SIZE        (320 * 1024)  // 320 KB общий кеш
#define CACHE_BLOCK_SECTORS     8              // Блок = 8 секторов (4KB)
#define CACHE_BLOCK_SIZE        (CACHE_BLOCK_SECTORS * FLOPPY_SECTOR_SIZE)
#define CACHE_FAT_BLOCKS        ((FLOPPY_FAT12_SECTORS + CACHE_BLOCK_SECTORS - 1) / CACHE_BLOCK_SECTORS) // ~5 блоков для FAT
#define CACHE_DATA_SIZE         (CACHE_TOTAL_SIZE - (CACHE_FAT_BLOCKS * CACHE_BLOCK_SIZE))
#define CACHE_DATA_BLOCKS       (CACHE_DATA_SIZE / CACHE_BLOCK_SIZE)  // ~75 блоков данных

// Команды для эмулятора
typedef enum {
    FLOPPY_CMD_LOAD_IMAGE,      // Загрузить образ
    FLOPPY_CMD_EJECT_IMAGE,     // Извлечь образ
    FLOPPY_CMD_READ_SECTOR,     // Прочитать сектор (от USB MSC)
    FLOPPY_CMD_WRITE_SECTOR,    // Записать сектор (от USB MSC)
    FLOPPY_CMD_GET_STATUS       // Получить статус
} floppy_cmd_t;

// Структура сообщения для эмулятора
typedef struct {
    floppy_cmd_t command;
    union {
        char filename[64];          // Для LOAD_IMAGE
        struct {
            uint32_t sector;        // Номер сектора
            uint8_t *buffer;        // Буфер данных
            void *callback_param;   // Параметр для callback
        } io;
    } data;
} floppy_message_t;

// Статус эмулятора
typedef enum {
    FLOPPY_STATUS_NO_IMAGE,         // Нет загруженного образа
    FLOPPY_STATUS_LOADING,          // Загрузка FAT области
    FLOPPY_STATUS_READY,            // Готов к работе
    FLOPPY_STATUS_ERROR             // Ошибка
} floppy_status_t;

// Информация о загрузке
typedef struct {
    floppy_status_t status;
    char current_image[64];
    uint32_t loaded_kb;             // Загружено KB (для FAT области)
    uint32_t total_fat_kb;          // Размер FAT области в KB
    uint32_t cache_hits;            // Попадания в кеш
    uint32_t cache_misses;          // Промахи кеша
} floppy_info_t;

// Глобальная очередь для эмулятора
extern QueueHandle_t floppy_queue;

// Функция создания задачи
void floppy_emu_task_init(void);

// Функция задачи
void floppy_emu_task(void *pvParameters);

// API функции
bool floppy_read_sector(uint32_t sector, uint8_t *buffer);
bool floppy_write_sector(uint32_t sector, const uint8_t *buffer);
bool floppy_is_ready(void);
const floppy_info_t* floppy_get_info(void);

#endif // FLOPPY_EMU_TASK_H
