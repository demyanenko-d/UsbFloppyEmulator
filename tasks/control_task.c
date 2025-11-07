#include "control_task.h"
#include "menu_task.h"
#include "config.h"
#include "pico/stdlib.h"
#include <stdio.h>

// Очередь для событий управления
QueueHandle_t control_queue = NULL;

// Переменные для дебаунса
static uint32_t last_button_time[3] = {0, 0, 0};
static uint8_t last_button_state[3] = {0, 0, 0};

#ifdef USE_ENCODER
// Переменные для энкодера
static int8_t encoder_position = 0;
static uint8_t encoder_state = 0;
static const int8_t encoder_table[] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
#endif

/**
 * @brief Инициализация GPIO для управления
 */
static void control_init_gpio(void) {
#ifdef USE_BUTTONS
    // Инициализация кнопок
    gpio_init(BTN_UP_PIN);
    gpio_set_dir(BTN_UP_PIN, GPIO_IN);
    gpio_pull_up(BTN_UP_PIN);
    
    gpio_init(BTN_DOWN_PIN);
    gpio_set_dir(BTN_DOWN_PIN, GPIO_IN);
    gpio_pull_up(BTN_DOWN_PIN);
    
    gpio_init(BTN_OK_PIN);
    gpio_set_dir(BTN_OK_PIN, GPIO_IN);
    gpio_pull_up(BTN_OK_PIN);
    
    printf("[CONTROL] Buttons initialized (UP: %d, DOWN: %d, OK: %d)\n", 
           BTN_UP_PIN, BTN_DOWN_PIN, BTN_OK_PIN);
#endif

#ifdef USE_ENCODER
    // Инициализация энкодера
    gpio_init(ENC_A_PIN);
    gpio_set_dir(ENC_A_PIN, GPIO_IN);
    gpio_pull_up(ENC_A_PIN);
    
    gpio_init(ENC_B_PIN);
    gpio_set_dir(ENC_B_PIN, GPIO_IN);
    gpio_pull_up(ENC_B_PIN);
    
    gpio_init(ENC_BTN_PIN);
    gpio_set_dir(ENC_BTN_PIN, GPIO_IN);
    gpio_pull_up(ENC_BTN_PIN);
    
    printf("[CONTROL] Encoder initialized (A: %d, B: %d, BTN: %d)\n", 
           ENC_A_PIN, ENC_B_PIN, ENC_BTN_PIN);
#endif
}

/**
 * @brief Обработка кнопки с дебаунсом
 */
static bool read_button_debounced(uint8_t pin, uint8_t index) {
    bool current_state = !gpio_get(pin);  // Инвертируем (pull-up)
    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    
    if (current_state != last_button_state[index]) {
        if (current_time - last_button_time[index] > DEBOUNCE_TIME_MS) {
            last_button_state[index] = current_state;
            last_button_time[index] = current_time;
            return current_state;  // Возвращаем новое состояние
        }
    }
    
    return false;  // Нет изменений или в пределах дебаунса
}

#ifdef USE_ENCODER
/**
 * @brief Чтение энкодера
 */
static int8_t read_encoder(void) {
    uint8_t a = gpio_get(ENC_A_PIN) ? 1 : 0;
    uint8_t b = gpio_get(ENC_B_PIN) ? 1 : 0;
    
    encoder_state = ((encoder_state << 2) | (a << 1) | b) & 0x0F;
    int8_t delta = encoder_table[encoder_state];
    
    encoder_position += delta;
    
    // Срабатывание после нескольких микрошагов
    if (encoder_position >= ENC_MICROSTEPS) {
        encoder_position = 0;
        return 1;  // По часовой
    } else if (encoder_position <= -ENC_MICROSTEPS) {
        encoder_position = 0;
        return -1;  // Против часовой
    }
    
    return 0;  // Нет движения
}
#endif

/**
 * @brief Отправка события в menu_task
 */
static void send_event_to_menu(control_event_t event) {
    menu_message_t msg;
    msg.event = event;
    msg.timestamp = to_ms_since_boot(get_absolute_time());
    
    if (xQueueSend(menu_queue, &msg, 0) != pdTRUE) {
        printf("[CONTROL] Failed to send event to menu\n");
    }
}

/**
 * @brief Основная функция задачи управления
 */
void control_task(void *pvParameters) {
    (void)pvParameters;
    
    printf("[CONTROL] Task started\n");
    
    // Инициализация GPIO
    control_init_gpio();
    
    while (1) {
#ifdef USE_BUTTONS
        // Обработка кнопок
        if (read_button_debounced(BTN_UP_PIN, 0)) {
            printf("[CONTROL] Button UP pressed\n");
            send_event_to_menu(CONTROL_EVENT_UP);
        }
        
        if (read_button_debounced(BTN_DOWN_PIN, 1)) {
            printf("[CONTROL] Button DOWN pressed\n");
            send_event_to_menu(CONTROL_EVENT_DOWN);
        }
        
        if (read_button_debounced(BTN_OK_PIN, 2)) {
            printf("[CONTROL] Button OK pressed\n");
            send_event_to_menu(CONTROL_EVENT_OK);
        }
#endif

#ifdef USE_ENCODER
        // Обработка энкодера
        int8_t encoder_delta = read_encoder();
        if (encoder_delta > 0) {
            printf("[CONTROL] Encoder CW\n");
            send_event_to_menu(CONTROL_EVENT_ENCODER_CW);
        } else if (encoder_delta < 0) {
            printf("[CONTROL] Encoder CCW\n");
            send_event_to_menu(CONTROL_EVENT_ENCODER_CCW);
        }
        
        // Кнопка энкодера
        if (read_button_debounced(ENC_BTN_PIN, 2)) {
            printf("[CONTROL] Encoder button pressed\n");
            send_event_to_menu(CONTROL_EVENT_OK);
        }
#endif
        
        // Опрос с частотой 1кГц (1мс) - быстрая реакция на энкодер
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/**
 * @brief Инициализация задачи управления
 */
void control_task_init(void) {
    // Создание очереди для событий
    control_queue = xQueueCreate(10, sizeof(control_message_t));
    
    if (control_queue == NULL) {
        printf("[CONTROL] Failed to create queue!\n");
        return;
    }
    
    // Создание задачи с высоким приоритетом для быстрой реакции
    BaseType_t result = xTaskCreate(
        control_task,           // Функция задачи
        "CONTROL",              // Имя задачи
        256,                    // Размер стека
        NULL,                   // Параметры
        TASK_PRIORITY_UI + 1,   // Высокий приоритет
        NULL                    // Дескриптор задачи
    );
    
    if (result != pdPASS) {
        printf("[CONTROL] Failed to create task!\n");
    } else {
        printf("[CONTROL] Task created successfully\n");
    }
}
