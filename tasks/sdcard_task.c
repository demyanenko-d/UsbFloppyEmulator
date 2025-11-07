#include "sdcard_task.h"
#include "config.h"
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include <stdio.h>
#include <string.h>

// Очереди
QueueHandle_t sdcard_queue = NULL;
QueueHandle_t sdcard_response_queue = NULL;

// Состояние SD карты
static bool card_initialized = false;
static char current_image[64] = "";
static bool image_loaded = false;

/**
 * @brief Инициализация SPI для SD карты
 */
static void sdcard_init_spi(void) {
    // Инициализация SPI
    spi_init(SD_SPI_PORT, 1000 * 1000);  // 1 MHz для начала
    
    // Настройка пинов
    gpio_set_function(SD_PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(SD_PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(SD_PIN_MOSI, GPIO_FUNC_SPI);
    
    // CS как обычный GPIO
    gpio_init(SD_PIN_CS);
    gpio_set_dir(SD_PIN_CS, GPIO_OUT);
    gpio_put(SD_PIN_CS, 1);  // CS high (неактивен)
    
    printf("[SDCARD] SPI initialized (MISO: %d, CS: %d, SCK: %d, MOSI: %d)\n",
           SD_PIN_MISO, SD_PIN_CS, SD_PIN_SCK, SD_PIN_MOSI);
}

/**
 * @brief Инициализация SD карты и FatFS
 */
static bool sdcard_init_card(void) {
    printf("[SDCARD] Initializing SD card...\n");
    
    // TODO: Реальная инициализация SD карты через SPI
    // TODO: Монтирование FatFS
    
    vTaskDelay(pdMS_TO_TICKS(500));  // Заглушка
    
    card_initialized = true;
    printf("[SDCARD] Card initialized successfully\n");
    
    return true;
}

/**
 * @brief Получение списка .img файлов
 */
static void sdcard_list_images(void) {
    printf("[SDCARD] Listing .img files...\n");
    
    if (!card_initialized) {
        printf("[SDCARD] Card not initialized!\n");
        return;
    }
    
    // TODO: Сканирование директории через FatFS
    // TODO: Фильтрация по расширению .img
    
    sdcard_response_t response;
    response.success = true;
    
    // Временная заглушка
    strcpy(response.data.file_list.files[0], "DOS622.IMG");
    strcpy(response.data.file_list.files[1], "WIN98.IMG");
    strcpy(response.data.file_list.files[2], "FREEDOS.IMG");
    strcpy(response.data.file_list.files[3], "LINUX.IMG");
    response.data.file_list.count = 4;
    
    printf("[SDCARD] Found %d image files\n", response.data.file_list.count);
    
    // Отправка ответа
    xQueueSend(sdcard_response_queue, &response, portMAX_DELAY);
}

/**
 * @brief Загрузка образа в память/кэш
 */
static void sdcard_load_image(const char *filename) {
    printf("[SDCARD] Loading image: %s\n", filename);
    
    if (!card_initialized) {
        printf("[SDCARD] Card not initialized!\n");
        return;
    }
    
    // TODO: Открыть файл через FatFS
    // TODO: Проверить размер (должен быть FLOPPY_IMAGE_SIZE)
    // TODO: Возможно, закэшировать часть образа
    
    strncpy(current_image, filename, sizeof(current_image) - 1);
    image_loaded = true;
    
    printf("[SDCARD] Image loaded: %s\n", current_image);
}

/**
 * @brief Чтение сектора из образа
 */
static bool sdcard_read_sector_internal(uint32_t sector, uint8_t *buffer) {
    if (!image_loaded) {
        printf("[SDCARD] No image loaded!\n");
        return false;
    }
    
    if (sector >= FLOPPY_TOTAL_SECTORS) {
        printf("[SDCARD] Invalid sector: %lu\n", sector);
        return false;
    }
    
    // TODO: Вычислить позицию в файле
    // uint32_t file_offset = sector * FLOPPY_SECTOR_SIZE;
    // TODO: Прочитать через FatFS
    
    // Заглушка - заполняем нулями
    memset(buffer, 0, FLOPPY_SECTOR_SIZE);
    
    return true;
}

/**
 * @brief Запись сектора в образ
 */
static bool sdcard_write_sector_internal(uint32_t sector, const uint8_t *buffer) {
    if (!image_loaded) {
        printf("[SDCARD] No image loaded!\n");
        return false;
    }
    
    if (sector >= FLOPPY_TOTAL_SECTORS) {
        printf("[SDCARD] Invalid sector: %lu\n", sector);
        return false;
    }
    
    // TODO: Вычислить позицию в файле
    // uint32_t file_offset = sector * FLOPPY_SECTOR_SIZE;
    // TODO: Записать через FatFS
    
    printf("[SDCARD] Sector %lu written\n", sector);
    
    return true;
}

/**
 * @brief Извлечение диска
 */
static void sdcard_eject(void) {
    printf("[SDCARD] Ejecting disk\n");
    
    // TODO: Закрыть файл
    // TODO: Сбросить кэши
    
    image_loaded = false;
    current_image[0] = '\0';
    
    printf("[SDCARD] Disk ejected\n");
}

/**
 * @brief Основная функция задачи SD карты
 */
void sdcard_task(void *pvParameters) {
    (void)pvParameters;
    
    printf("[SDCARD] Task started\n");
    
    // Инициализация SPI
    sdcard_init_spi();
    
    // Инициализация карты
    sdcard_init_card();
    
    sdcard_message_t msg;
    
    while (1) {
        // Ожидание команд
        if (xQueueReceive(sdcard_queue, &msg, portMAX_DELAY) == pdTRUE) {
            switch (msg.command) {
                case SDCARD_CMD_INIT:
                    sdcard_init_card();
                    break;
                    
                case SDCARD_CMD_LIST_IMAGES:
                    sdcard_list_images();
                    break;
                    
                case SDCARD_CMD_LOAD_IMAGE:
                    sdcard_load_image(msg.data.filename);
                    break;
                    
                case SDCARD_CMD_READ_SECTOR:
                    sdcard_read_sector_internal(msg.data.sector_io.sector, 
                                               msg.data.sector_io.buffer);
                    break;
                    
                case SDCARD_CMD_WRITE_SECTOR:
                    sdcard_write_sector_internal(msg.data.sector_io.sector, 
                                                 msg.data.sector_io.buffer);
                    break;
                    
                case SDCARD_CMD_EJECT:
                    sdcard_eject();
                    break;
                    
                default:
                    printf("[SDCARD] Unknown command: %d\n", msg.command);
                    break;
            }
        }
    }
}

/**
 * @brief Инициализация задачи SD карты
 */
void sdcard_task_init(void) {
    // Создание очередей
    sdcard_queue = xQueueCreate(10, sizeof(sdcard_message_t));
    sdcard_response_queue = xQueueCreate(5, sizeof(sdcard_response_t));
    
    if (sdcard_queue == NULL || sdcard_response_queue == NULL) {
        printf("[SDCARD] Failed to create queues!\n");
        return;
    }
    
    // Создание задачи
    BaseType_t result = xTaskCreate(
        sdcard_task,            // Функция задачи
        "SDCARD",               // Имя задачи
        1024,                   // Размер стека
        NULL,                   // Параметры
        TASK_PRIORITY_STORAGE,  // Приоритет
        NULL                    // Дескриптор задачи
    );
    
    if (result != pdPASS) {
        printf("[SDCARD] Failed to create task!\n");
    } else {
        printf("[SDCARD] Task created successfully\n");
    }
}

/**
 * @brief API: Проверка инициализации карты
 */
bool sdcard_is_initialized(void) {
    return card_initialized;
}

/**
 * @brief API: Чтение сектора (вызывается из других задач)
 */
bool sdcard_read_sector(uint32_t sector, uint8_t *buffer) {
    sdcard_message_t msg;
    msg.command = SDCARD_CMD_READ_SECTOR;
    msg.data.sector_io.sector = sector;
    msg.data.sector_io.buffer = buffer;
    
    return xQueueSend(sdcard_queue, &msg, pdMS_TO_TICKS(100)) == pdTRUE;
}

/**
 * @brief API: Запись сектора (вызывается из других задач)
 */
bool sdcard_write_sector(uint32_t sector, const uint8_t *buffer) {
    sdcard_message_t msg;
    msg.command = SDCARD_CMD_WRITE_SECTOR;
    msg.data.sector_io.sector = sector;
    msg.data.sector_io.buffer = (uint8_t*)buffer;
    
    return xQueueSend(sdcard_queue, &msg, pdMS_TO_TICKS(100)) == pdTRUE;
}
