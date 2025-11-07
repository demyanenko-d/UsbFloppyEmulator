#include "oled_task.h"
#include "config.h"
#include "ssd1306.h"
#include "hardware/i2c.h"
#include <stdio.h>
#include <string.h>

// Очередь для получения команд
QueueHandle_t oled_queue = NULL;

// Внутренние переменные
static bool display_initialized = false;
static ssd1306_t disp;

/**
 * @brief Инициализация OLED дисплея
 */
static void oled_init_display(void) {
    printf("[OLED] Initializing display...\n");
    
    // Инициализация I2C
    i2c_init(OLED_I2C_PORT, 400 * 1000);  // 400 кГц
    gpio_set_function(OLED_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(OLED_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(OLED_I2C_SDA);
    gpio_pull_up(OLED_I2C_SCL);
    
    printf("[OLED] I2C initialized (SDA: %d, SCL: %d)\n", OLED_I2C_SDA, OLED_I2C_SCL);
    
    // Инициализация SSD1306
    if (ssd1306_init(&disp, OLED_WIDTH, OLED_HEIGHT, OLED_I2C_ADDR, OLED_I2C_PORT)) {
        display_initialized = true;
        printf("[OLED] SSD1306 initialized successfully (%dx%d)\n", OLED_WIDTH, OLED_HEIGHT);
        
        // Очистка и показ стартового экрана
        ssd1306_clear(&disp);
        ssd1306_draw_string(&disp, 0, 0, 1, "USB Floppy Emu");
        ssd1306_draw_string(&disp, 0, 10, 1, "Initializing...");
        ssd1306_show(&disp);
    } else {
        printf("[OLED] Failed to initialize SSD1306!\n");
        display_initialized = false;
    }
}

/**
 * @brief Очистка дисплея
 */
static void oled_clear(void) {
    if (!display_initialized) return;
    
    ssd1306_clear(&disp);
    ssd1306_show(&disp);
    printf("[OLED] Display cleared\n");
}

/**
 * @brief Отрисовка меню
 */
static void oled_draw_menu(oled_message_t *msg) {
    if (!display_initialized) return;
    
    printf("[OLED] Drawing menu: %d items, selected: %d\n", 
           msg->data.menu.item_count, 
           msg->data.menu.selected_index);
    
    // Очистка дисплея
    ssd1306_clear(&disp);
    
    // Вычисляем высоту строки в зависимости от размера дисплея
    // Для 32px: 10 пикселей на строку (3 строки)
    // Для 64px: 12 пикселей на строку (5 строк)
    uint8_t line_height = (OLED_HEIGHT == 32) ? 10 : 12;
    uint8_t y_offset = 1;
    
    // Отрисовка пунктов меню
    for (uint8_t i = 0; i < msg->data.menu.item_count; i++) {
        uint8_t y_pos = y_offset + (i * line_height);
        
        // Индикатор выбора (стрелка или инверсия)
        if (i == msg->data.menu.selected_index) {
            // Рисуем стрелку выбора
            ssd1306_draw_string(&disp, 0, y_pos, 1, ">");
            // Можно добавить инверсию фона
            // ssd1306_draw_square(&disp, 8, y_pos - 1, OLED_WIDTH - 8, 10);
        }
        
        // Текст пункта меню (с отступом для стрелки)
        ssd1306_draw_string(&disp, 10, y_pos, 1, msg->data.menu.items[i]);
    }
    
    // Обновление дисплея
    ssd1306_show(&disp);
}

/**
 * @brief Показ сообщения
 */
static void oled_show_message(oled_message_t *msg) {
    if (!display_initialized) return;
    
    printf("[OLED] Message (line %d): %s\n", 
           msg->data.message.line,
           msg->data.message.text);
    
    // Очистка дисплея
    ssd1306_clear(&disp);
    
    // Вывод текста на указанной строке
    uint8_t y_pos = msg->data.message.line * 10;
    if (y_pos >= OLED_HEIGHT) {
        y_pos = 0;
    }
    
    ssd1306_draw_string(&disp, 0, y_pos, 1, msg->data.message.text);
    ssd1306_show(&disp);
}

/**
 * @brief Показ статуса
 */
static void oled_show_status(oled_message_t *msg) {
    if (!display_initialized) return;
    
    printf("[OLED] Status:\n");
    printf("[OLED]   %s\n", msg->data.status.status_line1);
    printf("[OLED]   %s\n", msg->data.status.status_line2);
    
    // Очистка дисплея
    ssd1306_clear(&disp);
    
    // Вывод двух строк статуса
    ssd1306_draw_string(&disp, 0, 0, 1, msg->data.status.status_line1);
    
    // Вторая строка в центре для дисплеев 32px, ниже для 64px
    uint8_t line2_y = (OLED_HEIGHT == 32) ? 16 : 20;
    ssd1306_draw_string(&disp, 0, line2_y, 1, msg->data.status.status_line2);
    
    ssd1306_show(&disp);
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
                    if (display_initialized) {
                        ssd1306_poweron(&disp);
                    }
                    break;
                    
                case OLED_CMD_POWER_OFF:
                    printf("[OLED] Power OFF\n");
                    if (display_initialized) {
                        ssd1306_poweroff(&disp);
                    }
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
