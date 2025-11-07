#include "floppy_emu_task.h"
#include "sdcard_task.h"
#include "oled_task.h"
#include "config.h"
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include <stdio.h>
#include <string.h>

// Очередь для команд эмулятора
QueueHandle_t floppy_queue = NULL;

// Блок кеша
typedef struct {
    uint32_t start_sector;      // Начальный сектор блока
    uint32_t timestamp;         // Время последнего доступа (1MHz counter)
    bool valid;                 // Валидность блока
    bool dirty;                 // Блок изменен (для записи)
    uint8_t data[CACHE_BLOCK_SIZE];
} cache_block_t;

// Кеш FAT области (постоянный)
static cache_block_t fat_cache[CACHE_FAT_BLOCKS];

// Кеш данных (LRU замещение)
static cache_block_t data_cache[CACHE_DATA_BLOCKS];

// Информация о состоянии
static floppy_info_t floppy_info = {
    .status = FLOPPY_STATUS_NO_IMAGE,
    .current_image = "",
    .loaded_kb = 0,
    .total_fat_kb = 0,
    .cache_hits = 0,
    .cache_misses = 0
};

// Mutex для защиты кеша
static SemaphoreHandle_t cache_mutex = NULL;

/**
 * @brief Получить текущее время в микросекундах
 */
static inline uint32_t get_timestamp(void) {
    return time_us_32();
}

/**
 * @brief Инициализация кеша
 */
static void cache_init(void) {
    printf("[FLOPPY] Initializing cache...\n");
    
    // Очистка FAT кеша
    for (int i = 0; i < CACHE_FAT_BLOCKS; i++) {
        fat_cache[i].start_sector = 0;
        fat_cache[i].timestamp = 0;
        fat_cache[i].valid = false;
        fat_cache[i].dirty = false;
    }
    
    // Очистка кеша данных
    for (int i = 0; i < CACHE_DATA_BLOCKS; i++) {
        data_cache[i].start_sector = 0;
        data_cache[i].timestamp = 0;
        data_cache[i].valid = false;
        data_cache[i].dirty = false;
    }
    
    floppy_info.cache_hits = 0;
    floppy_info.cache_misses = 0;
    
    printf("[FLOPPY] Cache initialized: FAT=%d blocks, Data=%d blocks\n",
           CACHE_FAT_BLOCKS, CACHE_DATA_BLOCKS);
}

/**
 * @brief Найти блок в кеше
 * @param sector Номер сектора
 * @param is_fat true если это FAT область
 * @return Указатель на блок или NULL
 */
static cache_block_t* cache_find_block(uint32_t sector, bool is_fat) {
    uint32_t block_start = (sector / CACHE_BLOCK_SECTORS) * CACHE_BLOCK_SECTORS;
    
    if (is_fat) {
        // Поиск в FAT кеше
        for (int i = 0; i < CACHE_FAT_BLOCKS; i++) {
            if (fat_cache[i].valid && fat_cache[i].start_sector == block_start) {
                fat_cache[i].timestamp = get_timestamp();
                return &fat_cache[i];
            }
        }
    } else {
        // Поиск в кеше данных
        for (int i = 0; i < CACHE_DATA_BLOCKS; i++) {
            if (data_cache[i].valid && data_cache[i].start_sector == block_start) {
                data_cache[i].timestamp = get_timestamp();
                return &data_cache[i];
            }
        }
    }
    
    return NULL;
}

/**
 * @brief Найти свободный или самый старый блок для замещения
 * @param is_fat true если это FAT область
 * @return Указатель на блок
 */
static cache_block_t* cache_get_free_block(bool is_fat) {
    if (is_fat) {
        // FAT кеш - ищем свободный или самый старый
        cache_block_t* oldest = &fat_cache[0];
        for (int i = 0; i < CACHE_FAT_BLOCKS; i++) {
            if (!fat_cache[i].valid) {
                return &fat_cache[i];
            }
            if (fat_cache[i].timestamp < oldest->timestamp) {
                oldest = &fat_cache[i];
            }
        }
        return oldest;
    } else {
        // Кеш данных - LRU
        cache_block_t* oldest = &data_cache[0];
        for (int i = 0; i < CACHE_DATA_BLOCKS; i++) {
            if (!data_cache[i].valid) {
                return &data_cache[i];
            }
            if (data_cache[i].timestamp < oldest->timestamp) {
                oldest = &data_cache[i];
            }
        }
        
        // Если блок грязный, нужно записать его обратно
        if (oldest->dirty) {
            printf("[FLOPPY] Writing back dirty block at sector %lu\n", oldest->start_sector);
            for (uint32_t i = 0; i < CACHE_BLOCK_SECTORS; i++) {
                sdcard_write_sector(oldest->start_sector + i, 
                                   &oldest->data[i * FLOPPY_SECTOR_SIZE]);
            }
            oldest->dirty = false;
        }
        
        return oldest;
    }
}

/**
 * @brief Загрузить блок с SD карты в кеш
 * @param sector Номер сектора
 * @param is_fat true если это FAT область
 * @return Указатель на блок или NULL при ошибке
 */
static cache_block_t* cache_load_block(uint32_t sector, bool is_fat) {
    uint32_t block_start = (sector / CACHE_BLOCK_SECTORS) * CACHE_BLOCK_SECTORS;
    cache_block_t* block = cache_get_free_block(is_fat);
    
    if (block == NULL) {
        printf("[FLOPPY] Failed to get cache block\n");
        return NULL;
    }
    
    printf("[FLOPPY] Loading block starting at sector %lu\n", block_start);
    
    // Чтение блока с SD карты (упреждающее чтение)
    for (uint32_t i = 0; i < CACHE_BLOCK_SECTORS; i++) {
        if (block_start + i >= FLOPPY_SECTORS) {
            break;  // Не выходим за пределы образа
        }
        
        if (!sdcard_read_sector(block_start + i, &block->data[i * FLOPPY_SECTOR_SIZE])) {
            printf("[FLOPPY] Failed to read sector %lu\n", block_start + i);
            return NULL;
        }
    }
    
    block->start_sector = block_start;
    block->timestamp = get_timestamp();
    block->valid = true;
    block->dirty = false;
    
    return block;
}

/**
 * @brief Чтение сектора через кеш
 */
static bool cache_read_sector(uint32_t sector, uint8_t *buffer) {
    if (sector >= FLOPPY_SECTORS) {
        printf("[FLOPPY] Invalid sector: %lu\n", sector);
        return false;
    }
    
    xSemaphoreTake(cache_mutex, portMAX_DELAY);
    
    bool is_fat = (sector < FLOPPY_FAT12_SECTORS);
    cache_block_t* block = cache_find_block(sector, is_fat);
    
    if (block == NULL) {
        // Промах кеша - загружаем блок
        floppy_info.cache_misses++;
        block = cache_load_block(sector, is_fat);
        
        if (block == NULL) {
            xSemaphoreGive(cache_mutex);
            return false;
        }
    } else {
        // Попадание в кеш
        floppy_info.cache_hits++;
    }
    
    // Копируем нужный сектор из блока
    uint32_t offset = (sector - block->start_sector) * FLOPPY_SECTOR_SIZE;
    memcpy(buffer, &block->data[offset], FLOPPY_SECTOR_SIZE);
    
    xSemaphoreGive(cache_mutex);
    return true;
}

/**
 * @brief Запись сектора через кеш
 */
static bool cache_write_sector(uint32_t sector, const uint8_t *buffer) {
    if (sector >= FLOPPY_SECTORS) {
        printf("[FLOPPY] Invalid sector: %lu\n", sector);
        return false;
    }
    
    xSemaphoreTake(cache_mutex, portMAX_DELAY);
    
    bool is_fat = (sector < FLOPPY_FAT12_SECTORS);
    cache_block_t* block = cache_find_block(sector, is_fat);
    
    if (block == NULL) {
        // Промах кеша - загружаем блок
        floppy_info.cache_misses++;
        block = cache_load_block(sector, is_fat);
        
        if (block == NULL) {
            xSemaphoreGive(cache_mutex);
            return false;
        }
    } else {
        floppy_info.cache_hits++;
    }
    
    // Записываем сектор в блок
    uint32_t offset = (sector - block->start_sector) * FLOPPY_SECTOR_SIZE;
    memcpy(&block->data[offset], buffer, FLOPPY_SECTOR_SIZE);
    block->dirty = true;
    block->timestamp = get_timestamp();
    
    xSemaphoreGive(cache_mutex);
    return true;
}

/**
 * @brief Загрузка образа
 */
static void floppy_load_image(const char *filename) {
    printf("[FLOPPY] Loading image: %s\n", filename);
    
    floppy_info.status = FLOPPY_STATUS_LOADING;
    strncpy(floppy_info.current_image, filename, sizeof(floppy_info.current_image) - 1);
    floppy_info.loaded_kb = 0;
    floppy_info.total_fat_kb = (FLOPPY_FAT12_SECTORS * FLOPPY_SECTOR_SIZE) / 1024;
    
    // Очистка кеша
    cache_init();
    
    // Отправка команды на загрузку образа в sdcard_task
    sdcard_message_t sd_msg;
    sd_msg.command = SDCARD_CMD_LOAD_IMAGE;
    strncpy(sd_msg.data.filename, filename, 64);
    xQueueSend(sdcard_queue, &sd_msg, portMAX_DELAY);
    
    // Отображение статуса загрузки на OLED
    oled_message_t oled_msg;
    oled_msg.command = OLED_CMD_SHOW_STATUS;
    strcpy(oled_msg.data.status.status_line1, "Loading FAT...");
    snprintf(oled_msg.data.status.status_line2, 32, "0 / %lu KB", floppy_info.total_fat_kb);
    
    extern QueueHandle_t oled_queue;
    if (oled_queue != NULL) {
        xQueueSend(oled_queue, &oled_msg, pdMS_TO_TICKS(100));
    }
    
    // Предзагрузка FAT области в кеш
    printf("[FLOPPY] Preloading FAT area (%d sectors)...\n", FLOPPY_FAT12_SECTORS);
    
    for (uint32_t sector = 0; sector < FLOPPY_FAT12_SECTORS; sector++) {
        uint8_t temp_buffer[FLOPPY_SECTOR_SIZE];
        
        if (!cache_read_sector(sector, temp_buffer)) {
            printf("[FLOPPY] Failed to preload sector %lu\n", sector);
            floppy_info.status = FLOPPY_STATUS_ERROR;
            return;
        }
        
        // Обновление прогресса каждые 4 сектора (2KB)
        if ((sector % 4) == 0) {
            floppy_info.loaded_kb = (sector * FLOPPY_SECTOR_SIZE) / 1024;
            
            oled_msg.command = OLED_CMD_SHOW_STATUS;
            strcpy(oled_msg.data.status.status_line1, "Loading FAT...");
            snprintf(oled_msg.data.status.status_line2, 32, "%lu / %lu KB", 
                    floppy_info.loaded_kb, floppy_info.total_fat_kb);
            
            if (oled_queue != NULL) {
                xQueueSend(oled_queue, &oled_msg, 0);
            }
        }
    }
    
    floppy_info.status = FLOPPY_STATUS_READY;
    floppy_info.loaded_kb = floppy_info.total_fat_kb;
    
    printf("[FLOPPY] Image loaded successfully\n");
    printf("[FLOPPY] FAT area: %lu KB in cache\n", floppy_info.total_fat_kb);
    
    // Показать готовность
    oled_msg.command = OLED_CMD_SHOW_STATUS;
    strcpy(oled_msg.data.status.status_line1, "Disk Ready");
    snprintf(oled_msg.data.status.status_line2, 32, "%.20s", filename);
    
    if (oled_queue != NULL) {
        xQueueSend(oled_queue, &oled_msg, pdMS_TO_TICKS(100));
    }
    
    vTaskDelay(pdMS_TO_TICKS(1500));
}

/**
 * @brief Извлечение образа
 */
static void floppy_eject_image(void) {
    printf("[FLOPPY] Ejecting image\n");
    
    xSemaphoreTake(cache_mutex, portMAX_DELAY);
    
    // Записать все грязные блоки
    for (int i = 0; i < CACHE_DATA_BLOCKS; i++) {
        if (data_cache[i].valid && data_cache[i].dirty) {
            printf("[FLOPPY] Flushing dirty block at sector %lu\n", data_cache[i].start_sector);
            for (uint32_t s = 0; s < CACHE_BLOCK_SECTORS; s++) {
                sdcard_write_sector(data_cache[i].start_sector + s,
                                   &data_cache[i].data[s * FLOPPY_SECTOR_SIZE]);
            }
        }
    }
    
    // Очистка кеша
    cache_init();
    
    xSemaphoreGive(cache_mutex);
    
    // Извлечь образ из SD карты
    sdcard_message_t sd_msg;
    sd_msg.command = SDCARD_CMD_EJECT;
    xQueueSend(sdcard_queue, &sd_msg, portMAX_DELAY);
    
    floppy_info.status = FLOPPY_STATUS_NO_IMAGE;
    floppy_info.current_image[0] = '\0';
    floppy_info.loaded_kb = 0;
    
    printf("[FLOPPY] Image ejected\n");
}

/**
 * @brief Основная задача эмулятора флоппи-диска
 */
void floppy_emu_task(void *pvParameters) {
    (void)pvParameters;
    
    printf("[FLOPPY] Task started\n");
    
    floppy_message_t msg;
    
    while (1) {
        if (xQueueReceive(floppy_queue, &msg, pdMS_TO_TICKS(100)) == pdTRUE) {
            switch (msg.command) {
                case FLOPPY_CMD_LOAD_IMAGE:
                    floppy_load_image(msg.data.filename);
                    break;
                    
                case FLOPPY_CMD_EJECT_IMAGE:
                    floppy_eject_image();
                    break;
                    
                case FLOPPY_CMD_READ_SECTOR:
                    cache_read_sector(msg.data.io.sector, msg.data.io.buffer);
                    break;
                    
                case FLOPPY_CMD_WRITE_SECTOR:
                    cache_write_sector(msg.data.io.sector, msg.data.io.buffer);
                    break;
                    
                default:
                    printf("[FLOPPY] Unknown command: %d\n", msg.command);
                    break;
            }
        }
    }
}

/**
 * @brief Инициализация задачи эмулятора
 */
void floppy_emu_task_init(void) {
    printf("[FLOPPY] Initializing task...\n");
    
    // Создание mutex для кеша
    cache_mutex = xSemaphoreCreateMutex();
    if (cache_mutex == NULL) {
        printf("[FLOPPY] Failed to create mutex!\n");
        return;
    }
    
    // Создание очереди
    floppy_queue = xQueueCreate(8, sizeof(floppy_message_t));
    if (floppy_queue == NULL) {
        printf("[FLOPPY] Failed to create queue!\n");
        return;
    }
    
    // Инициализация кеша
    cache_init();
    
    // Создание задачи
    BaseType_t result = xTaskCreate(
        floppy_emu_task,
        "FLOPPY",
        2048,  // Большой стек для работы с кешем
        NULL,
        TASK_PRIORITY_STORAGE,
        NULL
    );
    
    if (result != pdPASS) {
        printf("[FLOPPY] Failed to create task!\n");
        return;
    }
    
    printf("[FLOPPY] Task initialized successfully\n");
    printf("[FLOPPY] Cache size: FAT=%d KB, Data=%d KB, Total=%d KB\n",
           (CACHE_FAT_BLOCKS * CACHE_BLOCK_SIZE) / 1024,
           (CACHE_DATA_BLOCKS * CACHE_BLOCK_SIZE) / 1024,
           CACHE_TOTAL_SIZE / 1024);
}

/**
 * @brief API: Чтение сектора (для USB MSC)
 */
bool floppy_read_sector(uint32_t sector, uint8_t *buffer) {
    if (floppy_info.status != FLOPPY_STATUS_READY) {
        return false;
    }
    return cache_read_sector(sector, buffer);
}

/**
 * @brief API: Запись сектора (для USB MSC)
 */
bool floppy_write_sector(uint32_t sector, const uint8_t *buffer) {
    if (floppy_info.status != FLOPPY_STATUS_READY) {
        return false;
    }
    return cache_write_sector(sector, buffer);
}

/**
 * @brief API: Проверка готовности
 */
bool floppy_is_ready(void) {
    return (floppy_info.status == FLOPPY_STATUS_READY);
}

/**
 * @brief API: Получить информацию
 */
const floppy_info_t* floppy_get_info(void) {
    return &floppy_info;
}
