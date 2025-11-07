#include "tasks.h"
#include <stdio.h>

/**
 * @brief Инициализация всех задач в правильном порядке
 */
void tasks_init_all(void) {
    printf("=== Initializing all tasks ===\n");
    
    // 1. LED задача - первой, для индикации процесса загрузки
    led_task_init();
    
    // 2. SD карта - инициализация хранилища
    sdcard_task_init();
    
    // 3. OLED дисплей - для отображения
    oled_task_init();
    
    // 4. Menu - логика меню
    menu_task_init();
    
    // 5. Control - управление (зависит от menu_queue)
    control_task_init();
    
    // 6. USB - последней, так как зависит от SD карты
    usb_task_init();
    
    printf("=== All tasks initialized ===\n");
}
