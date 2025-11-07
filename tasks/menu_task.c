#include "menu_task.h"
#include "oled_task.h"
#include "sdcard_task.h"
#include "config.h"
#include <stdio.h>
#include <string.h>

// Очередь для событий от control_task
QueueHandle_t menu_queue = NULL;

// Текущее состояние меню
static menu_state_t current_state = MENU_STATE_MAIN;
static uint8_t selected_index = 0;
static uint8_t scroll_offset = 0;

// Список файлов (будет заполняться из sdcard_task)
static char file_list[MAX_IMAGES][32];
static uint8_t file_count = 0;

/**
 * @brief Обновление отображения меню на OLED
 */
static void update_oled_menu(void) {
    oled_message_t msg;
    msg.command = OLED_CMD_UPDATE_MENU;
    
    switch (current_state) {
        case MENU_STATE_MAIN:
            strcpy(msg.data.menu.items[0], "Select Image");
            strcpy(msg.data.menu.items[1], "Eject Disk");
            msg.data.menu.item_count = 2;
            msg.data.menu.selected_index = selected_index;
            break;
            
        case MENU_STATE_FILE_LIST:
            // Показываем файлы с учетом прокрутки
            msg.data.menu.item_count = 0;
            for (uint8_t i = 0; i < MENU_ITEMS_PER_PAGE && (scroll_offset + i) < file_count; i++) {
                strcpy(msg.data.menu.items[i], file_list[scroll_offset + i]);
                msg.data.menu.item_count++;
            }
            msg.data.menu.selected_index = selected_index - scroll_offset;
            break;
            
        case MENU_STATE_FILE_SELECTED:
            strcpy(msg.data.menu.items[0], "Load Image?");
            snprintf(msg.data.menu.items[1], 32, "%.28s", file_list[selected_index]);
            msg.data.menu.item_count = 2;
            msg.data.menu.selected_index = 0;
            break;
            
        case MENU_STATE_LOADING:
            strcpy(msg.data.menu.items[0], "Loading...");
            msg.data.menu.item_count = 1;
            msg.data.menu.selected_index = 0;
            break;
            
        case MENU_STATE_ERROR:
            strcpy(msg.data.menu.items[0], "Error!");
            strcpy(msg.data.menu.items[1], "Press OK");
            msg.data.menu.item_count = 2;
            msg.data.menu.selected_index = 0;
            break;
    }
    
    xQueueSend(oled_queue, &msg, portMAX_DELAY);
}

/**
 * @brief Обработка событий навигации вверх/вниз
 */
static void handle_navigation(bool is_up) {
    uint8_t max_index = 0;
    
    switch (current_state) {
        case MENU_STATE_MAIN:
            max_index = 1;  // 2 пункта: Select Image, Eject Disk
            break;
            
        case MENU_STATE_FILE_LIST:
            max_index = file_count > 0 ? file_count - 1 : 0;
            break;
            
        default:
            return;
    }
    
    if (is_up) {
        if (selected_index > 0) {
            selected_index--;
            // Прокрутка вверх если нужно
            if (selected_index < scroll_offset) {
                scroll_offset = selected_index;
            }
        }
    } else {
        if (selected_index < max_index) {
            selected_index++;
            // Прокрутка вниз если нужно
            if (selected_index >= scroll_offset + MENU_ITEMS_PER_PAGE) {
                scroll_offset = selected_index - MENU_ITEMS_PER_PAGE + 1;
            }
        }
    }
    
    update_oled_menu();
}

/**
 * @brief Обработка нажатия OK
 */
static void handle_ok_press(void) {
    switch (current_state) {
        case MENU_STATE_MAIN:
            if (selected_index == 0) {
                // Запрос списка файлов из sdcard_task
                printf("[MENU] Requesting file list from SD card\n");
                
                sdcard_message_t sd_msg;
                sd_msg.command = SDCARD_CMD_LIST_IMAGES;
                xQueueSend(sdcard_queue, &sd_msg, portMAX_DELAY);
                
                // TODO: Ждем ответа и переходим в MENU_STATE_FILE_LIST
                current_state = MENU_STATE_FILE_LIST;
                selected_index = 0;
                scroll_offset = 0;
                
                // Временная заглушка с тестовыми файлами
                strcpy(file_list[0], "DOS622.IMG");
                strcpy(file_list[1], "WIN98.IMG");
                strcpy(file_list[2], "FREEDOS.IMG");
                file_count = 3;
                
                update_oled_menu();
            } else if (selected_index == 1) {
                // Eject disk
                printf("[MENU] Ejecting disk\n");
                // TODO: Отправить команду в USB task
            }
            break;
            
        case MENU_STATE_FILE_LIST:
            if (file_count > 0) {
                printf("[MENU] File selected: %s\n", file_list[selected_index]);
                current_state = MENU_STATE_FILE_SELECTED;
                update_oled_menu();
            }
            break;
            
        case MENU_STATE_FILE_SELECTED:
            // Загрузка образа
            printf("[MENU] Loading image: %s\n", file_list[selected_index]);
            current_state = MENU_STATE_LOADING;
            update_oled_menu();
            
            // TODO: Отправить команду на загрузку образа
            sdcard_message_t sd_msg;
            sd_msg.command = SDCARD_CMD_LOAD_IMAGE;
            strncpy(sd_msg.data.filename, file_list[selected_index], 64);
            xQueueSend(sdcard_queue, &sd_msg, portMAX_DELAY);
            break;
            
        case MENU_STATE_ERROR:
            // Возврат в главное меню
            current_state = MENU_STATE_MAIN;
            selected_index = 0;
            update_oled_menu();
            break;
            
        default:
            break;
    }
}

/**
 * @brief Основная функция задачи меню
 */
void menu_task(void *pvParameters) {
    (void)pvParameters;
    
    printf("[MENU] Task started\n");
    
    menu_message_t msg;
    
    // Отобразить главное меню
    vTaskDelay(pdMS_TO_TICKS(500));  // Дать время на инициализацию OLED
    update_oled_menu();
    
    while (1) {
        // Ожидание событий от control_task
        if (xQueueReceive(menu_queue, &msg, portMAX_DELAY) == pdTRUE) {
            printf("[MENU] Received event: %d\n", msg.event);
            
            switch (msg.event) {
                case CONTROL_EVENT_UP:
                case CONTROL_EVENT_ENCODER_CCW:
                    handle_navigation(true);
                    break;
                    
                case CONTROL_EVENT_DOWN:
                case CONTROL_EVENT_ENCODER_CW:
                    handle_navigation(false);
                    break;
                    
                case CONTROL_EVENT_OK:
                    handle_ok_press();
                    break;
                    
                default:
                    break;
            }
        }
    }
}

/**
 * @brief Инициализация задачи меню
 */
void menu_task_init(void) {
    // Создание очереди для событий
    menu_queue = xQueueCreate(10, sizeof(menu_message_t));
    
    if (menu_queue == NULL) {
        printf("[MENU] Failed to create queue!\n");
        return;
    }
    
    // Создание задачи
    BaseType_t result = xTaskCreate(
        menu_task,              // Функция задачи
        "MENU",                 // Имя задачи
        512,                    // Размер стека
        NULL,                   // Параметры
        TASK_PRIORITY_UI,       // Приоритет
        NULL                    // Дескриптор задачи
    );
    
    if (result != pdPASS) {
        printf("[MENU] Failed to create task!\n");
    } else {
        printf("[MENU] Task created successfully\n");
    }
}
