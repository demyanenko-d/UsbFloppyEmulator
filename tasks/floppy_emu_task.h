#ifndef FLOPPY_EMU_TASK_H
#define FLOPPY_EMU_TASK_H

#include "config.h"  // Нужно для CACHE_SIZE_KB
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include <stdbool.h>
#include <stdint.h>

// Размеры флоппи-диска
#define FLOPPY_SECTOR_SIZE      512

// Типы флоппи-дисков
typedef enum {
    FLOPPY_TYPE_UNKNOWN = 0,
    FLOPPY_TYPE_720K,       // 720 KB (DD)
    FLOPPY_TYPE_1200K,      // 1.2 MB (HD 5.25")
    FLOPPY_TYPE_1440K       // 1.44 MB (HD 3.5")
} floppy_type_t;

// Параметры разных типов дисков
typedef struct {
    floppy_type_t type;
    const char *name;
    uint32_t sectors;       // Общее количество секторов
    uint32_t fat_sectors;   // Количество секторов для FAT области
} floppy_geometry_t;

// Геометрия различных форматов
static const floppy_geometry_t floppy_formats[] = {
    { FLOPPY_TYPE_720K,  "720K",  1440, 14 },  // 720KB:  boot(1) + FATs(2*3) + root(7) = 14 sectors
    { FLOPPY_TYPE_1200K, "1.2M",  2400, 19 },  // 1.2MB:  boot(1) + FATs(2*7) + root(14) = 29? need check
    { FLOPPY_TYPE_1440K, "1.44M", 2880, 33 }   // 1.44MB: boot(1) + FATs(2*9) + root(14) = 33 sectors
};

// Для обратной совместимости
#define FLOPPY_SECTORS          2880  // 1.44MB / 512 bytes (максимальный размер)
#define FLOPPY_FAT12_SECTORS    33    // FAT12 для 1.44MB (максимальный размер)

// Конфигурация кеша - зависит от платформы
#define CACHE_TOTAL_SIZE        (CACHE_SIZE_KB * 1024)  // 320KB для Pico2, 160KB для Pico1
#define CACHE_BLOCK_SECTORS     8                        // Блок = 8 секторов (4KB)
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
    floppy_type_t disk_type;        // Тип диска (720K/1.2M/1.44M)
    uint32_t total_sectors;         // Общее количество секторов
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
