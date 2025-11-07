#ifndef TASKS_H
#define TASKS_H

/**
 * @file tasks.h
 * @brief Общий заголовочный файл для всех задач проекта
 * 
 * Этот файл включает все задачи и предоставляет функцию
 * инициализации всех задач системы.
 */

#include "tasks/oled_task.h"
#include "tasks/control_task.h"
#include "tasks/menu_task.h"
#include "tasks/sdcard_task.h"
#include "tasks/usb_task.h"
#include "tasks/led_task.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Инициализация всех задач системы
 * 
 * Вызывает init функции всех задач в правильном порядке.
 * Должна быть вызвана до запуска планировщика FreeRTOS.
 */
void tasks_init_all(void);

#ifdef __cplusplus
}
#endif

#endif // TASKS_H
