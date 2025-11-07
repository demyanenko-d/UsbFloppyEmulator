#include "oled_task.h"
#include "config.h"
#include <stdio.h>
#include <string.h>

// Очередь для получения команд
QueueHandle_t oled_queue = NULL;

// Внутренние переменные
static bool display_initialized = false;

/**
 * @brief Инициализация OLED дисплея
 */
static void oled_init_display(void) {
    // TODO: Инициализация I2C
    // TODO: Инициализация SSD1306
    
    printf("[OLED] Initializing display...\n");
    
    // Заглушка - будет заменена на реальную инициализацию
    vTaskDelay(pdMS_TO_TICKS(100));
    
    display_initialized = true;
    printf("[OLED] Display initialized\n");
}

/**
 * @brief Очистка дисплея
 */
static void oled_clear(void) {
    if (!display_initialized) return;
    
    // TODO: Очистка SSD1306
    printf("[OLED] Clear display\n");
}

/**
 * @brief Отрисовка меню
 */
static void oled_draw_menu(oled_message_t *msg) {
    if (!display_initialized) return;
    
    printf("[OLED] Drawing menu: %d items, selected: %d\n", 
           msg->data.menu.item_count, 
           msg->data.menu.selected_index);
    
    // TODO: Отрисовка пунктов меню на SSD1306
    for (uint8_t i = 0; i < msg->data.menu.item_count; i++) {
        printf("[OLED]   %c %s\n", 
               (i == msg->data.menu.selected_index) ? '>' : ' ',
               msg->data.menu.items[i]);
    }
}

/**
 * @brief Показ сообщения
 */
static void oled_show_message(oled_message_t *msg) {
    if (!display_initialized) return;
    
    printf("[OLED] Message (line %d): %s\n", 
           msg->data.message.line,
           msg->data.message.text);
    
    // TODO: Вывод текста на SSD1306
}

/**
 * @brief Показ статуса
 */
static void oled_show_status(oled_message_t *msg) {
    if (!display_initialized) return;
    
    printf("[OLED] Status:\n");
    printf("[OLED]   %s\n", msg->data.status.status_line1);
    printf("[OLED]   %s\n", msg->data.status.status_line2);
    
    // TODO: Вывод статуса на SSD1306
}

/**
 * @brief Основная функция задачи OLED
 */
void oled_task(void *pvParameters) {
    (void)pvParameters;
    
    printf("[OLED] Task started\n");
    
    // Инициализация дисплея
    oled_init_display();
    
    oled_message_t msg;
    
    while (1) {
        // Ожидание команд из очереди
        if (xQueueReceive(oled_queue, &msg, pdMS_TO_TICKS(100)) == pdTRUE) {
            switch (msg.command) {
                case OLED_CMD_CLEAR:
                    oled_clear();
                    break;
                    
                case OLED_CMD_UPDATE_MENU:
                    oled_draw_menu(&msg);
                    break;
                    
                case OLED_CMD_SHOW_MESSAGE:
                    oled_show_message(&msg);
                    break;
                    
                case OLED_CMD_SHOW_STATUS:
                    oled_show_status(&msg);
                    break;
                    
                case OLED_CMD_POWER_ON:
                    printf("[OLED] Power ON\n");
                    // TODO: Включить дисплей
                    break;
                    
                case OLED_CMD_POWER_OFF:
                    printf("[OLED] Power OFF\n");
                    // TODO: Выключить дисплей
                    break;
                    
                default:
                    printf("[OLED] Unknown command: %d\n", msg.command);
                    break;
            }
        }
        
        // Здесь можно добавить периодическое обновление если нужно
    }
}

/**
 * @brief Инициализация задачи OLED
 */
void oled_task_init(void) {
    // Создание очереди для команд (до 10 сообщений)
    oled_queue = xQueueCreate(10, sizeof(oled_message_t));
    
    if (oled_queue == NULL) {
        printf("[OLED] Failed to create queue!\n");
        return;
    }
    
    // Создание задачи
    BaseType_t result = xTaskCreate(
        oled_task,              // Функция задачи
        "OLED",                 // Имя задачи
        512,                    // Размер стека (в словах)
        NULL,                   // Параметры
        TASK_PRIORITY_UI,       // Приоритет
        NULL                    // Дескриптор задачи
    );
    
    if (result != pdPASS) {
        printf("[OLED] Failed to create task!\n");
    } else {
        printf("[OLED] Task created successfully\n");
    }
}
