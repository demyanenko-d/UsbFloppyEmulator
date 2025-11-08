#include "usb_task.h"
#include "floppy_emu_task.h"
#include "config.h"
#include "pico/stdlib.h"
#include "tusb.h"
#include <stdio.h>
#include <string.h>

// Очередь для команд
QueueHandle_t usb_queue = NULL;

// Состояние USB
static bool usb_mounted = false;
static bool media_changed = false;  // Флаг изменения медиа
static bool last_ready_state = false;  // Предыдущее состояние готовности

//--------------------------------------------------------------------+
// USB MSC Callbacks (TinyUSB)
//--------------------------------------------------------------------+

/**
 * @brief Callback: Вызывается при подключении устройства
 */
void tud_mount_cb(void) {
    printf("[USB] Device mounted\n");
    usb_mounted = true;
}

/**
 * @brief Callback: Вызывается при отключении устройства
 */
void tud_umount_cb(void) {
    printf("[USB] Device unmounted\n");
    usb_mounted = false;
}

/**
 * @brief Callback: Проверка готовности диска
 */
bool tud_msc_test_unit_ready_cb(uint8_t lun) {
    (void)lun;
    
    // Диск готов если образ загружен в эмулятор
    bool ready = floppy_is_ready();
    
    // Отслеживание изменения медиа
    if (ready != last_ready_state) {
        media_changed = true;
        last_ready_state = ready;
        printf("[USB] Media state changed: %s\n", ready ? "READY" : "NOT READY");
    }
    
    if (!ready) {
        // Установить код ошибки "medium not present"
        tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3A, 0x00);
    } else if (media_changed) {
        // Носитель был изменен - сообщить хосту
        tud_msc_set_sense(lun, SCSI_SENSE_UNIT_ATTENTION, 0x28, 0x00);
        media_changed = false;
        return false;  // Вернуть false чтобы хост переопросил
    }
    
    return ready;
}

/**
 * @brief Callback: Получить емкость диска
 */
void tud_msc_capacity_cb(uint8_t lun, uint32_t* block_count, uint16_t* block_size) {
    (void)lun;
    
    // Получить информацию о загруженном образе
    const floppy_info_t* info = floppy_get_info();
    
    if (info != NULL && info->status == FLOPPY_STATUS_READY && info->total_sectors > 0) {
        // Использовать реальный размер загруженного образа
        *block_count = info->total_sectors;
    } else {
        // По умолчанию - максимальный размер (1.44MB)
        *block_count = FLOPPY_SECTORS;
    }
    
    *block_size = FLOPPY_SECTOR_SIZE;  // 512 bytes
    
    printf("[USB] Capacity request: %lu sectors x %u bytes\n", *block_count, *block_size);
}

/**
 * @brief Callback: Получить информацию об устройстве (INQUIRY)
 */
void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4]) {
    (void)lun;
    
    const char vid[] = "RaspPi";
    const char pid[] = "Floppy Emulator";
    const char rev[] = "1.0";
    
    memcpy(vendor_id, vid, strlen(vid));
    memcpy(product_id, pid, strlen(pid));
    memcpy(product_rev, rev, strlen(rev));
}

/**
 * @brief Callback: Чтение блоков (READ10 команда)
 */
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
    (void)lun;
    (void)offset;  // offset всегда 0, так как читаем по одному сектору
    
    // Получить информацию о загруженном образе для проверки границ
    const floppy_info_t* info = floppy_get_info();
    uint32_t max_sectors = (info != NULL && info->total_sectors > 0) ? info->total_sectors : FLOPPY_SECTORS;
    
    // Проверка границ
    if (lba >= max_sectors) {
        printf("[USB] Read error: LBA %lu out of range (max: %lu)\n", lba, max_sectors);
        return -1;
    }
    
    // Чтение через эмулятор (с кешем)
    if (!floppy_read_sector(lba, (uint8_t*)buffer)) {
        printf("[USB] Read error at LBA %lu\n", lba);
        return -1;
    }
    
    return bufsize;
}

/**
 * @brief Callback: Запись блоков (WRITE10 команда)
 */
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
    (void)lun;
    (void)offset;
    
    // Получить информацию о загруженном образе для проверки границ
    const floppy_info_t* info = floppy_get_info();
    uint32_t max_sectors = (info != NULL && info->total_sectors > 0) ? info->total_sectors : FLOPPY_SECTORS;
    
    // Проверка границ
    if (lba >= max_sectors) {
        printf("[USB] Write error: LBA %lu out of range (max: %lu)\n", lba, max_sectors);
        return -1;
    }
    
    // Запись через эмулятор (с кешем)
    if (!floppy_write_sector(lba, buffer)) {
        printf("[USB] Write error at LBA %lu\n", lba);
        return -1;
    }
    
    return bufsize;
}

/**
 * @brief Callback: Завершение операции записи (flush)
 */
void tud_msc_write10_complete_cb(uint8_t lun) {
    (void)lun;
    // Здесь можно принудительно сбросить кеш, но у нас он и так умный
}

/**
 * @brief Callback: SCSI команды (опциональные)
 */
int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void* buffer, uint16_t bufsize) {
    void const* response = NULL;
    int32_t resplen = 0;
    
    // Большинство команд обрабатываются TinyUSB автоматически
    // Здесь можно добавить кастомные команды
    
    switch (scsi_cmd[0]) {
        default:
            // Неизвестная команда - установить ошибку
            tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
            resplen = -1;
            break;
    }
    
    // Копировать ответ в буфер
    if (resplen > 0) {
        if (resplen > bufsize) resplen = bufsize;
        memcpy(buffer, response, resplen);
    }
    
    return resplen;
}

//--------------------------------------------------------------------+
// USB Task
//--------------------------------------------------------------------+

/**
 * @brief Основная задача USB
 */
void usb_task(void *pvParameters) {
    (void)pvParameters;
    
    printf("[USB] Task started\n");
    
    // Инициализация TinyUSB
    printf("[USB] Initializing TinyUSB MSC device...\n");
    tusb_init();
    
    usb_message_t msg;
    
    while (1) {
        // Обработка событий TinyUSB
        tud_task();
        
        // Обработка сообщений из очереди
        if (xQueueReceive(usb_queue, &msg, 0) == pdTRUE) {
            switch (msg.command) {
                case USB_CMD_MOUNT:
                    printf("[USB] Mount disk\n");
                    break;
                    
                case USB_CMD_UNMOUNT:
                    printf("[USB] Unmount disk\n");
                    break;
                    
                case USB_CMD_EJECT:
                    printf("[USB] Eject disk\n");
                    break;
            }
        }
        
        // Небольшая задержка чтобы не загружать CPU
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/**
 * @brief Инициализация задачи USB
 */
void usb_task_init(void) {
    printf("[USB] Initializing task...\n");
    
    // Создание очереди
    usb_queue = xQueueCreate(8, sizeof(usb_message_t));
    
    if (usb_queue == NULL) {
        printf("[USB] Failed to create queue!\n");
        return;
    }
    
    // Создание задачи
    BaseType_t result = xTaskCreate(
        usb_task,
        "USB",
        STACK_SIZE_USB,
        NULL,
        TASK_PRIORITY_USB,
        NULL
    );
    
    if (result != pdPASS) {
        printf("[USB] Failed to create task!\n");
        return;
    }
    
    printf("[USB] Task initialized successfully\n");
}
