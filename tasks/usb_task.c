#include "usb_task.h"
#include "sdcard_task.h"
#include "config.h"
#include <stdio.h>
#include <string.h>

// TODO: #include "tusb.h"

// Очередь для команд
QueueHandle_t usb_queue = NULL;

// Состояние USB
static bool usb_mounted = false;

/**
 * @brief Инициализация TinyUSB
 */
static void usb_init_tinyusb(void) {
    printf("[USB] Initializing TinyUSB...\n");
    
    // TODO: Настройка дескрипторов USB Mass Storage
    // TODO: tud_init(BOARD_TUD_RHPORT);
    
    printf("[USB] TinyUSB initialized\n");
}

/**
 * @brief Подключение USB диска
 */
static void usb_mount_disk(void) {
    printf("[USB] Mounting disk...\n");
    
    if (!sdcard_is_initialized()) {
        printf("[USB] SD card not initialized!\n");
        return;
    }
    
    // TODO: Включить Mass Storage Device
    usb_mounted = true;
    
    printf("[USB] Disk mounted\n");
}

/**
 * @brief Отключение USB диска
 */
static void usb_unmount_disk(void) {
    printf("[USB] Unmounting disk...\n");
    
    // TODO: Отключить Mass Storage Device
    usb_mounted = false;
    
    printf("[USB] Disk unmounted\n");
}

/**
 * @brief Callback: Чтение блока (вызывается TinyUSB)
 */
/*
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
    (void)lun;
    (void)offset;
    
    // Читаем сектор из SD карты
    if (sdcard_read_sector(lba, (uint8_t*)buffer)) {
        return bufsize;
    }
    
    return -1;  // Ошибка
}
*/

/**
 * @brief Callback: Запись блока (вызывается TinyUSB)
 */
/*
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
    (void)lun;
    (void)offset;
    
    // Записываем сектор на SD карту
    if (sdcard_write_sector(lba, buffer)) {
        return bufsize;
    }
    
    return -1;  // Ошибка
}
*/

/**
 * @brief Callback: Получение емкости диска
 */
/*
void tud_msc_capacity_cb(uint8_t lun, uint32_t* block_count, uint16_t* block_size) {
    (void)lun;
    
    *block_count = FLOPPY_TOTAL_SECTORS;
    *block_size  = FLOPPY_SECTOR_SIZE;
}
*/

/**
 * @brief Callback: Готовность к операциям
 */
/*
bool tud_msc_test_unit_ready_cb(uint8_t lun) {
    (void)lun;
    return sdcard_is_initialized() && usb_mounted;
}
*/

/**
 * @brief Основная функция задачи USB
 */
void usb_task(void *pvParameters) {
    (void)pvParameters;
    
    printf("[USB] Task started\n");
    
    // Инициализация TinyUSB
    usb_init_tinyusb();
    
    usb_message_t msg;
    
    while (1) {
        // Обработка USB событий (вызов tud_task)
        // TODO: tud_task();
        
        // Проверка команд из очереди (без блокировки)
        if (xQueueReceive(usb_queue, &msg, 0) == pdTRUE) {
            switch (msg.command) {
                case USB_CMD_MOUNT:
                    usb_mount_disk();
                    break;
                    
                case USB_CMD_UNMOUNT:
                    usb_unmount_disk();
                    break;
                    
                case USB_CMD_EJECT:
                    usb_unmount_disk();
                    break;
                    
                default:
                    printf("[USB] Unknown command: %d\n", msg.command);
                    break;
            }
        }
        
        // TinyUSB рекомендует вызывать tud_task() как можно чаще
        // С частотой 10кГц это будет каждые 100мкс
        vTaskDelay(1);  // 100мкс при 10kHz tick rate
    }
}

/**
 * @brief Инициализация задачи USB
 */
void usb_task_init(void) {
    // Создание очереди для команд
    usb_queue = xQueueCreate(5, sizeof(usb_message_t));
    
    if (usb_queue == NULL) {
        printf("[USB] Failed to create queue!\n");
        return;
    }
    
    // Создание задачи с высоким приоритетом
    BaseType_t result = xTaskCreate(
        usb_task,               // Функция задачи
        "USB",                  // Имя задачи
        1024,                   // Размер стека
        NULL,                   // Параметры
        TASK_PRIORITY_USB,      // Высокий приоритет
        NULL                    // Дескриптор задачи
    );
    
    if (result != pdPASS) {
        printf("[USB] Failed to create task!\n");
    } else {
        printf("[USB] Task created successfully\n");
    }
}
