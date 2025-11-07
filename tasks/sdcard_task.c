#include "sdcard_task.h"
#include "oled_task.h"
#include "usb_task.h"
#include "config.h"
#include "sd_card.h"
#include "ff.h"
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>  // для strcasecmp

// Очереди
QueueHandle_t sdcard_queue = NULL;
QueueHandle_t sdcard_response_queue = NULL;

// Состояние SD карты
static bool card_initialized = false;
static FATFS fatfs;
static bool fs_mounted = false;
static char current_image[64] = "";
static bool image_loaded = false;
static FIL current_file;
static bool file_opened = false;

/**
 * @brief Инициализация SD карты и FatFS
 */
static bool sdcard_init_card(void) {
    printf("[SDCARD] Initializing SD card...\n");
    
    // Настройка SPI пинов
    gpio_set_function(SD_PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(SD_PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(SD_PIN_MOSI, GPIO_FUNC_SPI);
    
    printf("[SDCARD] SPI pins configured (MISO:%d SCK:%d MOSI:%d CS:%d)\n",
           SD_PIN_MISO, SD_PIN_SCK, SD_PIN_MOSI, SD_PIN_CS);
    
    // Инициализация низкоуровневого драйвера
    if (!sd_card_init(SD_SPI_PORT, SD_PIN_CS)) {
        printf("[SDCARD] Failed to initialize card\n");
        return false;
    }
    
    card_initialized = true;
    
    // Получить информацию о карте
    const sd_card_info_t* info = sd_card_get_info();
    if (info == NULL) {
        printf("[SDCARD] Failed to get card info\n");
        card_initialized = false;
        return false;
    }
    
    // Отображение информации о карте на OLED
    char info_line1[32];
    char info_line2[32];
    
    const char* card_type_str = "Unknown";
    if (info->type == SD_CARD_TYPE_SD1) {
        card_type_str = "SD v1";
    } else if (info->type == SD_CARD_TYPE_SD2) {
        card_type_str = "SD v2";
    } else if (info->type == SD_CARD_TYPE_SDHC) {
        card_type_str = "SDHC";
    }
    
    snprintf(info_line1, sizeof(info_line1), "%s %lu MB", card_type_str, info->capacity_mb);
    snprintf(info_line2, sizeof(info_line2), "%lu sectors", info->sectors);
    
    printf("[SDCARD] Card: %s\n", info_line1);
    printf("[SDCARD] %s\n", info_line2);
    
    // Отправка информации на OLED
    oled_message_t oled_msg;
    oled_msg.command = OLED_CMD_SHOW_STATUS;
    strcpy(oled_msg.data.status.status_line1, info_line1);
    strcpy(oled_msg.data.status.status_line2, info_line2);
    
    extern QueueHandle_t oled_queue;
    if (oled_queue != NULL) {
        xQueueSend(oled_queue, &oled_msg, pdMS_TO_TICKS(100));
    }
    
    vTaskDelay(pdMS_TO_TICKS(2000));  // Показать информацию 2 секунды
    
    // Монтирование файловой системы
    FRESULT res = f_mount(&fatfs, "0:", 1);  // 1 = монтировать сейчас
    if (res != FR_OK) {
        printf("[SDCARD] Failed to mount filesystem (error %d)\n", res);
        card_initialized = false;
        return false;
    }
    
    fs_mounted = true;
    printf("[SDCARD] Filesystem mounted successfully\n");
    
    // Получение информации о свободном месте
    DWORD free_clusters;
    FATFS* fs_ptr = &fatfs;
    res = f_getfree("0:", &free_clusters, &fs_ptr);
    if (res == FR_OK) {
        DWORD total_sectors = (fatfs.n_fatent - 2) * fatfs.csize;
        DWORD free_sectors = free_clusters * fatfs.csize;
        
        printf("[SDCARD] Total: %lu KB, Free: %lu KB\n",
               total_sectors / 2, free_sectors / 2);
    }
    
    return true;
}

/**
 * @brief Размонтирование файловой системы
 */
static void sdcard_unmount(void) {
    if (file_opened) {
        f_close(&current_file);
        file_opened = false;
    }
    
    if (fs_mounted) {
        f_mount(NULL, "0:", 0);
        fs_mounted = false;
    }
    
    card_initialized = false;
    printf("[SDCARD] Card unmounted\n");
}

/**
 * @brief Получение списка .img файлов
 */
static void sdcard_list_images(void) {
    printf("[SDCARD] Listing .img files...\n");
    
    if (!card_initialized || !fs_mounted) {
        printf("[SDCARD] Card not initialized!\n");
        
        sdcard_response_t response;
        response.success = false;
        response.data.file_list.count = 0;
        xQueueSend(sdcard_response_queue, &response, portMAX_DELAY);
        return;
    }
    
    sdcard_response_t response;
    response.success = true;
    response.data.file_list.count = 0;
    
    DIR dir;
    FILINFO fno;
    FRESULT res;
    
    // Открыть корневую директорию
    res = f_opendir(&dir, "/");
    if (res != FR_OK) {
        printf("[SDCARD] Failed to open root directory (error %d)\n", res);
        response.success = false;
        xQueueSend(sdcard_response_queue, &response, portMAX_DELAY);
        return;
    }
    
    // Перебрать все файлы
    while (response.data.file_list.count < 32) {  // Максимум 32 файла из sdcard_task.h
        res = f_readdir(&dir, &fno);
        if (res != FR_OK || fno.fname[0] == 0) {
            break;  // Ошибка или конец директории
        }
        
        // Пропустить директории и скрытые файлы
        if (fno.fattrib & AM_DIR) {
            continue;
        }
        
        // Проверить расширение .img (без учета регистра)
        size_t len = strlen(fno.fname);
        if (len > 4) {
            const char* ext = fno.fname + len - 4;
            if (strcasecmp(ext, ".img") == 0) {
                // Добавить в список
                strncpy(response.data.file_list.files[response.data.file_list.count],
                       fno.fname,
                       31);  // Максимум 32 символа (из sdcard_task.h)
                response.data.file_list.files[response.data.file_list.count][31] = '\0';
                
                printf("[SDCARD] Found: %s (%lu bytes)\n", fno.fname, fno.fsize);
                response.data.file_list.count++;
            }
        }
    }
    
    f_closedir(&dir);
    
    printf("[SDCARD] Found %d image files\n", response.data.file_list.count);
    
    // Отправка ответа
    xQueueSend(sdcard_response_queue, &response, portMAX_DELAY);
}

/**
 * @brief Загрузка образа в память/кэш
 */
static void sdcard_load_image(const char *filename) {
    printf("[SDCARD] Loading image: %s\n", filename);
    
    if (!card_initialized || !fs_mounted) {
        printf("[SDCARD] Card not initialized!\n");
        return;
    }
    
    // Закрыть предыдущий файл, если открыт
    if (file_opened) {
        f_close(&current_file);
        file_opened = false;
    }
    
    // Открыть файл образа
    char filepath[64];
    snprintf(filepath, sizeof(filepath), "/%s", filename);
    
    FRESULT res = f_open(&current_file, filepath, FA_READ);
    if (res != FR_OK) {
        printf("[SDCARD] Failed to open file (error %d)\n", res);
        return;
    }
    
    file_opened = true;
    
    // Проверить размер файла
    FSIZE_t file_size = f_size(&current_file);
    printf("[SDCARD] File size: %lu bytes\n", (unsigned long)file_size);
    
    if (file_size != FLOPPY_IMAGE_SIZE) {
        printf("[SDCARD] Warning: File size != 1.44MB (%d bytes)\n", FLOPPY_IMAGE_SIZE);
        // Можно продолжить, но предупредить пользователя
    }
    
    strncpy(current_image, filename, sizeof(current_image) - 1);
    current_image[sizeof(current_image) - 1] = '\0';
    image_loaded = true;
    
    printf("[SDCARD] Image loaded: %s\n", current_image);
    
    // Уведомить USB task о новом образе (закомментировано, пока USB task не готов)
    /*
    usb_message_t usb_msg;
    usb_msg.type = USB_MSG_LOAD_IMAGE;
    strcpy(usb_msg.data.image_name, current_image);
    
    extern QueueHandle_t usb_queue;
    if (usb_queue != NULL) {
        xQueueSend(usb_queue, &usb_msg, pdMS_TO_TICKS(100));
    }
    */
}

/**
 * @brief Чтение сектора из текущего образа
 */
bool sdcard_read_sector(uint32_t sector, uint8_t *buffer) {
    if (!file_opened || !image_loaded) {
        printf("[SDCARD] No image loaded!\n");
        return false;
    }
    
    if (sector >= FLOPPY_TOTAL_SECTORS) {
        printf("[SDCARD] Invalid sector: %lu\n", sector);
        return false;
    }
    
    // Перемещение к нужному сектору
    FSIZE_t offset = sector * FLOPPY_SECTOR_SIZE;
    FRESULT res = f_lseek(&current_file, offset);
    if (res != FR_OK) {
        printf("[SDCARD] Seek error %d\n", res);
        return false;
    }
    
    // Чтение сектора
    UINT bytes_read;
    res = f_read(&current_file, buffer, FLOPPY_SECTOR_SIZE, &bytes_read);
    if (res != FR_OK || bytes_read != FLOPPY_SECTOR_SIZE) {
        printf("[SDCARD] Read error %d (read %u bytes)\n", res, bytes_read);
        return false;
    }
    
    return true;
}

/**
 * @brief Запись сектора в текущий образ
 */
bool sdcard_write_sector(uint32_t sector, const uint8_t *buffer) {
    if (!file_opened || !image_loaded) {
        printf("[SDCARD] No image loaded!\n");
        return false;
    }
    
    if (sector >= FLOPPY_TOTAL_SECTORS) {
        printf("[SDCARD] Invalid sector: %lu\n", sector);
        return false;
    }
    
    // Перемещение к нужному сектору
    FSIZE_t offset = sector * FLOPPY_SECTOR_SIZE;
    FRESULT res = f_lseek(&current_file, offset);
    if (res != FR_OK) {
        printf("[SDCARD] Seek error %d\n", res);
        return false;
    }
    
    // Запись сектора
    UINT bytes_written;
    res = f_write(&current_file, buffer, FLOPPY_SECTOR_SIZE, &bytes_written);
    if (res != FR_OK || bytes_written != FLOPPY_SECTOR_SIZE) {
        printf("[SDCARD] Write error %d (wrote %u bytes)\n", res, bytes_written);
        return false;
    }
    
    // Синхронизация для надежности
    f_sync(&current_file);
    
    return true;
}

/**
 * @brief Проверка инициализации SD карты
 */
bool sdcard_is_initialized(void) {
    return card_initialized && fs_mounted;
}

/**
 * @brief Основная задача SD карты
 */
void sdcard_task(void *pvParameters) {
    (void)pvParameters;
    
    printf("[SDCARD] Task started\n");
    
    TickType_t last_check_time = xTaskGetTickCount();
    const TickType_t check_interval = pdMS_TO_TICKS(500);  // Проверка каждые 500 мс
    
    while (1) {
        // Проверка наличия карты, если она не инициализирована
        if (!card_initialized) {
            TickType_t current_time = xTaskGetTickCount();
            
            if (current_time - last_check_time >= check_interval) {
                last_check_time = current_time;
                
                // Попытка инициализации
                if (sdcard_init_card()) {
                    // Автоматически получить список файлов после успешной инициализации
                    sdcard_list_images();
                }
            }
        }
        
        // Обработка сообщений из очереди
        sdcard_message_t msg;
        if (xQueueReceive(sdcard_queue, &msg, pdMS_TO_TICKS(100)) == pdTRUE) {
            switch (msg.command) {
                case SDCARD_CMD_LIST_IMAGES:
                    sdcard_list_images();
                    break;
                    
                case SDCARD_CMD_LOAD_IMAGE:
                    sdcard_load_image(msg.data.filename);
                    break;
                    
                case SDCARD_CMD_EJECT:
                    if (file_opened) {
                        f_close(&current_file);
                        file_opened = false;
                    }
                    image_loaded = false;
                    current_image[0] = '\0';
                    printf("[SDCARD] Image ejected\n");
                    break;
                    
                default:
                    printf("[SDCARD] Unknown message type: %d\n", msg.command);
                    break;
            }
        }
    }
}

/**
 * @brief Инициализация задачи SD карты
 */
void sdcard_task_init(void) {
    printf("[SDCARD] Initializing task...\n");
    
    // Создание очередей
    sdcard_queue = xQueueCreate(8, sizeof(sdcard_message_t));  // 8 сообщений
    sdcard_response_queue = xQueueCreate(8, sizeof(sdcard_response_t));
    
    if (sdcard_queue == NULL || sdcard_response_queue == NULL) {
        printf("[SDCARD] Failed to create queues!\n");
        return;
    }
    
    // Создание задачи
    BaseType_t result = xTaskCreate(
        sdcard_task,
        "SDCARD",
        STACK_SIZE_STORAGE,
        NULL,
        TASK_PRIORITY_STORAGE,
        NULL
    );
    
    if (result != pdPASS) {
        printf("[SDCARD] Failed to create task!\n");
        return;
    }
    
    printf("[SDCARD] Task initialized successfully\n");
}
