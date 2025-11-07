#include "menu_task.h"
#include "oled_task.h"
#include "sdcard_task.h"
#include "floppy_emu_task.h"
#include "sd_card.h"
#include "config.h"
#include <stdio.h>
#include <string.h>

#define MAX_IMAGES 32  // Максимум файлов (совпадает с sdcard_task.h)

// Очередь для событий от control_task
QueueHandle_t menu_queue = NULL;

// Текущее состояние меню
static menu_state_t current_state = MENU_STATE_MAIN;
static uint8_t selected_index = 0;
static uint8_t scroll_offset = 0;
static uint8_t selected_file_index = 0;  // Сохраненный индекс выбранного файла
static uint8_t confirm_choice = 0;       // 0=Yes, 1=No для подтверждения

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
            strcpy(msg.data.menu.items[1], "SD Card Info");
            strcpy(msg.data.menu.items[2], "Eject Disk");
            msg.data.menu.item_count = 3;
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
            
        case MENU_STATE_SD_INFO:
            strcpy(msg.data.menu.items[0], "SD Card Info");
            strcpy(msg.data.menu.items[1], "Press OK");
            msg.data.menu.item_count = 2;
            msg.data.menu.selected_index = 0;
            break;
            
        case MENU_STATE_FILE_CONFIRM:
            snprintf(msg.data.menu.items[0], 32, "Load %.20s?", file_list[selected_file_index]);
            if (confirm_choice == 0) {
                strcpy(msg.data.menu.items[1], "> Yes");
                strcpy(msg.data.menu.items[2], "  No");
            } else {
                strcpy(msg.data.menu.items[1], "  Yes");
                strcpy(msg.data.menu.items[2], "> No");
            }
            msg.data.menu.item_count = 3;
            msg.data.menu.selected_index = confirm_choice + 1;  // +1 т.к. первая строка - вопрос
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
            max_index = 2;  // 3 пункта: Select Image, SD Card Info, Eject Disk
            break;
            
        case MENU_STATE_FILE_LIST:
            max_index = file_count > 0 ? file_count - 1 : 0;
            break;
            
        case MENU_STATE_FILE_CONFIRM:
            // Переключение между Yes/No
            confirm_choice = is_up ? 0 : 1;
            update_oled_menu();
            return;
            
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
                
                // Переходим в состояние загрузки и ждем ответ
                current_state = MENU_STATE_LOADING;
                update_oled_menu();
                
            } else if (selected_index == 1) {
                // SD Card Info - показать информацию о карте
                printf("[MENU] Showing SD card info\n");
                current_state = MENU_STATE_SD_INFO;
                
                // Запросить информацию о карте через OLED
                oled_message_t oled_msg;
                oled_msg.command = OLED_CMD_SHOW_STATUS;
                
                if (sdcard_is_initialized()) {
                    const sd_card_info_t* info = sd_card_get_info();
                    if (info != NULL) {
                        const char* card_type_str = "Unknown";
                        if (info->type == SD_CARD_TYPE_SD1) {
                            card_type_str = "SD v1";
                        } else if (info->type == SD_CARD_TYPE_SD2) {
                            card_type_str = "SD v2";
                        } else if (info->type == SD_CARD_TYPE_SDHC) {
                            card_type_str = "SDHC";
                        }
                        
                        snprintf(oled_msg.data.status.status_line1, 32, "%s %lu MB", 
                                card_type_str, info->capacity_mb);
                        snprintf(oled_msg.data.status.status_line2, 32, "%lu sectors", 
                                info->sectors);
                    } else {
                        strcpy(oled_msg.data.status.status_line1, "Card Info");
                        strcpy(oled_msg.data.status.status_line2, "Not available");
                    }
                } else {
                    strcpy(oled_msg.data.status.status_line1, "SD Card");
                    strcpy(oled_msg.data.status.status_line2, "Not initialized");
                }
                
                xQueueSend(oled_queue, &oled_msg, portMAX_DELAY);
                
            } else if (selected_index == 2) {
                // Eject disk
                printf("[MENU] Ejecting disk\n");
                sdcard_message_t sd_msg;
                sd_msg.command = SDCARD_CMD_EJECT;
                xQueueSend(sdcard_queue, &sd_msg, portMAX_DELAY);
            }
            break;
            
        case MENU_STATE_FILE_LIST:
            if (file_count > 0) {
                printf("[MENU] File selected: %s\n", file_list[selected_index]);
                selected_file_index = selected_index;  // Сохранить выбранный файл
                confirm_choice = 0;  // По умолчанию Yes
                current_state = MENU_STATE_FILE_CONFIRM;
                update_oled_menu();
            }
            break;
            
        case MENU_STATE_FILE_CONFIRM:
            // Проверка выбора пользователя
            if (confirm_choice == 0) {
                // Yes - загрузка образа в эмулятор
                printf("[MENU] Loading image: %s\n", file_list[selected_file_index]);
                current_state = MENU_STATE_LOADING;
                update_oled_menu();
                
                // Отправить команду на загрузку образа в floppy эмулятор
                floppy_message_t floppy_msg;
                floppy_msg.command = FLOPPY_CMD_LOAD_IMAGE;
                strncpy(floppy_msg.data.filename, file_list[selected_file_index], 64);
                xQueueSend(floppy_queue, &floppy_msg, portMAX_DELAY);
            } else {
                // No - вернуться в список файлов
                printf("[MENU] Load cancelled\n");
                current_state = MENU_STATE_FILE_LIST;
                selected_index = selected_file_index;
                // Восстановить прокрутку
                if (selected_index >= MENU_ITEMS_PER_PAGE) {
                    scroll_offset = selected_index - MENU_ITEMS_PER_PAGE + 1;
                } else {
                    scroll_offset = 0;
                }
                update_oled_menu();
            }
            break;
            
        case MENU_STATE_SD_INFO:
            // Возврат в главное меню
            current_state = MENU_STATE_MAIN;
            selected_index = 0;
            update_oled_menu();
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
 * @brief Обработка кнопки "Назад" (длительное нажатие)
 */
static void handle_back_press(void) {
    printf("[MENU] Back pressed, state=%d\n", current_state);
    
    switch (current_state) {
        case MENU_STATE_FILE_LIST:
            // Из списка файлов в главное меню
            current_state = MENU_STATE_MAIN;
            selected_index = 0;
            scroll_offset = 0;
            update_oled_menu();
            break;
            
        case MENU_STATE_FILE_CONFIRM:
            // Из подтверждения обратно в список файлов
            current_state = MENU_STATE_FILE_LIST;
            selected_index = selected_file_index;
            // Восстановить прокрутку
            if (selected_index >= MENU_ITEMS_PER_PAGE) {
                scroll_offset = selected_index - MENU_ITEMS_PER_PAGE + 1;
            } else {
                scroll_offset = 0;
            }
            update_oled_menu();
            break;
            
        case MENU_STATE_SD_INFO:
        case MENU_STATE_ERROR:
            // Из информации/ошибки в главное меню
            current_state = MENU_STATE_MAIN;
            selected_index = 0;
            update_oled_menu();
            break;
            
        default:
            // В других состояниях игнорируем
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
        // Проверка ответов от SD карты
        sdcard_response_t sd_response;
        if (xQueueReceive(sdcard_response_queue, &sd_response, 0) == pdTRUE) {
            printf("[MENU] Received SD card response\n");
            
            if (current_state == MENU_STATE_LOADING) {
                if (sd_response.success && sd_response.data.file_list.count > 0) {
                    // Получен список файлов
                    file_count = sd_response.data.file_list.count;
                    for (uint8_t i = 0; i < file_count && i < MAX_IMAGES; i++) {
                        strncpy(file_list[i], sd_response.data.file_list.files[i], 31);
                        file_list[i][31] = '\0';
                    }
                    
                    printf("[MENU] Loaded %d files from SD card\n", file_count);
                    
                    current_state = MENU_STATE_FILE_LIST;
                    selected_index = 0;
                    scroll_offset = 0;
                    update_oled_menu();
                } else {
                    // Ошибка или нет файлов
                    printf("[MENU] No files found or SD error\n");
                    current_state = MENU_STATE_ERROR;
                    update_oled_menu();
                }
            }
        }
        
        // Ожидание событий от control_task
        if (xQueueReceive(menu_queue, &msg, pdMS_TO_TICKS(10)) == pdTRUE) {
            printf("[MENU] Received event: %d\n", msg.event);
            
            // Любое нажатие кнопки убирает экран информации о SD карте при старте
            if (current_state == MENU_STATE_SD_INFO) {
                current_state = MENU_STATE_MAIN;
                selected_index = 0;
                update_oled_menu();
                continue;  // Пропустить обработку события
            }
            
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
                    
                case CONTROL_EVENT_LONG_PRESS:
                    handle_back_press();
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
