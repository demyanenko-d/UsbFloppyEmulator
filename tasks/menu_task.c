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
static uint8_t eject_choice = 0;         // 0=Yes, 1=No для извлечения

// Навигация по каталогам
static char current_path[128] = "/";     // Текущий путь
static bool in_subdirectory = false;     // Находимся в подкаталоге

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
            strcpy(msg.data.menu.items[2], "");
            msg.data.menu.item_count = 3;
            msg.data.menu.selected_index = selected_index;
            break;
            
        case MENU_STATE_FILE_LIST: {
            // Показываем файлы с учетом прокрутки
            // Первый пункт всегда "< Back"
            msg.data.menu.item_count = 0;
            
            if (scroll_offset == 0) {
                // Первая страница - показываем "< Back"
                strcpy(msg.data.menu.items[0], "< Back");
                msg.data.menu.item_count = 1;
                
                // Заполняем остальные пункты файлами
                for (uint8_t i = 1; i < MENU_ITEMS_PER_PAGE && i < file_count; i++) {
                    strcpy(msg.data.menu.items[i], file_list[i]);
                    msg.data.menu.item_count++;
                }
            } else {
                // Не первая страница - показываем только файлы
                for (uint8_t i = 0; i < MENU_ITEMS_PER_PAGE && (scroll_offset + i) < file_count; i++) {
                    strcpy(msg.data.menu.items[i], file_list[scroll_offset + i]);
                    msg.data.menu.item_count++;
                }
            }
            
            msg.data.menu.selected_index = selected_index - scroll_offset;
            break;
        }
            
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
            
        case MENU_STATE_DISK_LOADED: {
            // Экран после загрузки: Disk Ready / Имя файла / Eject >> Yes No
            const floppy_info_t* info = floppy_get_info();
            strcpy(msg.data.menu.items[0], "Disk Ready");
            if (info != NULL && info->current_image[0] != '\0') {
                snprintf(msg.data.menu.items[1], 32, "%.20s", info->current_image);
            } else {
                strcpy(msg.data.menu.items[1], "");
            }
            
            // Третья строка с переключением Yes/No
            if (eject_choice == 0) {
                strcpy(msg.data.menu.items[2], "Eject >> Yes  No");
            } else {
                strcpy(msg.data.menu.items[2], "Eject    Yes >>No");
            }
            msg.data.menu.item_count = 3;
            msg.data.menu.selected_index = 2;  // Курсор всегда на третьей строке
            break;
        }
            
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
            max_index = 1;  // 2 пункта: Select Image, SD Card Info
            break;
            
        case MENU_STATE_FILE_LIST:
            max_index = file_count > 0 ? file_count - 1 : 0;
            break;
            
        case MENU_STATE_FILE_CONFIRM:
            // Переключение между Yes/No
            confirm_choice = is_up ? 0 : 1;
            update_oled_menu();
            return;
            
        case MENU_STATE_DISK_LOADED:
            // Переключение между Yes/No для извлечения
            eject_choice = is_up ? 0 : 1;
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
                sd_msg.data.path[0] = '\0';  // Пустой путь = корневой каталог
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
            }
            break;
            
        case MENU_STATE_FILE_LIST:
            if (file_count > 0) {
                // Проверяем, выбран ли "< Back"
                if (selected_index == 0) {
                    // Возврат в главное меню (или в родительский каталог)
                    printf("[MENU] Back selected\n");
                    
                    if (in_subdirectory) {
                        // Вернуться в родительский каталог
                        // Найти последний слэш в пути
                        char *last_slash = strrchr(current_path, '/');
                        if (last_slash != NULL && last_slash != current_path) {
                            // Обрезать путь до родительского каталога
                            *last_slash = '\0';
                        } else {
                            // Вернуться в корень
                            strcpy(current_path, "/");
                        }
                        
                        // Проверить, остались ли в подкаталоге
                        if (strcmp(current_path, "/") == 0) {
                            in_subdirectory = false;
                        }
                        
                        printf("[MENU] Returning to: %s\n", current_path);
                        
                        // Запросить список файлов
                        sdcard_message_t sd_msg;
                        sd_msg.command = SDCARD_CMD_LIST_IMAGES;
                        strncpy(sd_msg.data.path, current_path, sizeof(sd_msg.data.path) - 1);
                        sd_msg.data.path[sizeof(sd_msg.data.path) - 1] = '\0';
                        xQueueSend(sdcard_queue, &sd_msg, portMAX_DELAY);
                        
                        current_state = MENU_STATE_LOADING;
                        selected_index = 0;
                        scroll_offset = 0;
                        update_oled_menu();
                    } else {
                        // Возврат в главное меню
                        current_state = MENU_STATE_MAIN;
                        selected_index = 0;
                        update_oled_menu();
                    }
                } else {
                    // Файл или каталог выбран
                    printf("[MENU] File selected: %s\n", file_list[selected_index]);
                    
                    // Проверка, это каталог или файл
                    if (file_list[selected_index][0] == '[') {
                        // Это каталог - входим в него
                        char dirname[32];
                        int len = strlen(file_list[selected_index]);
                        // Извлечь имя каталога без скобок [dirname] -> dirname
                        strncpy(dirname, file_list[selected_index] + 1, len - 2);
                        dirname[len - 2] = '\0';
                        
                        printf("[MENU] Entering directory: %s\n", dirname);
                        printf("[MENU] Current path before: %s\n", current_path);
                        
                        // Обновить путь
                        if (strlen(current_path) > 1) {
                            strcat(current_path, "/");
                        }
                        strcat(current_path, dirname);
                        in_subdirectory = true;
                        
                        printf("[MENU] New path: %s\n", current_path);
                        
                        // Очистить старый список файлов
                        file_count = 0;
                        
                        // Запросить список файлов в подкаталоге
                        sdcard_message_t sd_msg;
                        sd_msg.command = SDCARD_CMD_LIST_IMAGES;
                        strncpy(sd_msg.data.path, current_path, sizeof(sd_msg.data.path) - 1);
                        sd_msg.data.path[sizeof(sd_msg.data.path) - 1] = '\0';
                        xQueueSend(sdcard_queue, &sd_msg, portMAX_DELAY);
                        
                        printf("[MENU] Switching to LOADING state\n");
                        
                        // Переход в состояние загрузки
                        current_state = MENU_STATE_LOADING;
                        selected_index = 0;
                        scroll_offset = 0;
                        update_oled_menu();
                    } else {
                        // Это файл образа
                        selected_file_index = selected_index;
                        confirm_choice = 0;  // По умолчанию Yes
                        current_state = MENU_STATE_FILE_CONFIRM;
                        update_oled_menu();
                    }
                }
            }
            break;
            
        case MENU_STATE_FILE_CONFIRM:
            // Проверка выбора пользователя
            if (confirm_choice == 0) {
                // Yes - загрузка образа в эмулятор
                printf("[MENU] Loading image: %s\n", file_list[selected_file_index]);
                current_state = MENU_STATE_LOADING;
                update_oled_menu();
                
                // Построить полный путь к файлу
                char full_path[128];
                if (in_subdirectory && strcmp(current_path, "/") != 0) {
                    // Файл в подкаталоге
                    snprintf(full_path, sizeof(full_path), "%s/%s", current_path, file_list[selected_file_index]);
                } else {
                    // Файл в корне
                    snprintf(full_path, sizeof(full_path), "/%s", file_list[selected_file_index]);
                }
                
                printf("[MENU] Full path: %s\n", full_path);
                
                // Отправить команду на загрузку образа в floppy эмулятор
                floppy_message_t floppy_msg;
                floppy_msg.command = FLOPPY_CMD_LOAD_IMAGE;
                strncpy(floppy_msg.data.filename, full_path, 64);
                xQueueSend(floppy_queue, &floppy_msg, portMAX_DELAY);
                
                // Ждем пока загрузится (floppy_emu покажет прогресс)
                // После загрузки автоматически перейдем в DISK_LOADED
                // Это произойдет в основном цикле menu_task
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
            
        case MENU_STATE_DISK_LOADED:
            // Обработка выбора Yes/No для извлечения
            if (eject_choice == 0) {
                // Yes - извлечь диск
                printf("[MENU] Ejecting disk\n");
                
                // Отправить команду извлечения в floppy emulator
                floppy_message_t floppy_msg;
                floppy_msg.command = FLOPPY_CMD_EJECT_IMAGE;
                xQueueSend(floppy_queue, &floppy_msg, portMAX_DELAY);
                
                // Показать статус извлечения
                oled_message_t oled_msg;
                oled_msg.command = OLED_CMD_SHOW_STATUS;
                strcpy(oled_msg.data.status.status_line1, "Disk Ejected");
                strcpy(oled_msg.data.status.status_line2, "");
                xQueueSend(oled_queue, &oled_msg, pdMS_TO_TICKS(100));
                
                vTaskDelay(pdMS_TO_TICKS(1000));
                
                // Вернуться в главное меню
                current_state = MENU_STATE_MAIN;
                selected_index = 0;
                eject_choice = 0;
                update_oled_menu();
            } else {
                // No - остаться на экране Disk Ready
                // Можно вернуться в главное меню или оставить как есть
                printf("[MENU] Eject cancelled\n");
                // Просто остаемся на том же экране
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
                printf("[MENU] SD response: success=%d, count=%d\n", 
                       sd_response.success, sd_response.data.file_list.count);
                
                if (sd_response.success && sd_response.data.file_list.count > 0) {
                    // Получен список файлов
                    // Сдвинем массив на 1 чтобы освободить место для "< Back"
                    file_count = sd_response.data.file_list.count + 1;  // +1 для "< Back"
                    
                    // Первый элемент - "< Back"
                    strcpy(file_list[0], "< Back");
                    
                    // Остальные - файлы с SD карты
                    for (uint8_t i = 0; i < sd_response.data.file_list.count && i < (MAX_IMAGES - 1); i++) {
                        strncpy(file_list[i + 1], sd_response.data.file_list.files[i], 31);
                        file_list[i + 1][31] = '\0';
                    }
                    
                    printf("[MENU] Loaded %d files from SD card (+ Back button)\n", file_count - 1);
                    
                    current_state = MENU_STATE_FILE_LIST;
                    selected_index = 0;
                    scroll_offset = 0;
                    update_oled_menu();
                } else if (sd_response.success && sd_response.data.file_list.count == 0) {
                    // Нет файлов, но SD карта работает
                    printf("[MENU] No .img files found in root directory\n");
                    
                    // Показать сообщение вместо ошибки
                    file_count = 1;
                    strcpy(file_list[0], "< Back");
                    
                    current_state = MENU_STATE_FILE_LIST;
                    selected_index = 0;
                    scroll_offset = 0;
                    update_oled_menu();
                } else {
                    // Ошибка SD карты
                    printf("[MENU] SD card error\n");
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
        
        // Проверка: если в состоянии LOADING и диск загрузился, переходим в DISK_LOADED
        if (current_state == MENU_STATE_LOADING && floppy_is_ready()) {
            printf("[MENU] Disk loaded, switching to DISK_LOADED state\n");
            current_state = MENU_STATE_DISK_LOADED;
            eject_choice = 0;  // По умолчанию Yes
            update_oled_menu();
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
