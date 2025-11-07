#ifndef CONFIG_H
#define CONFIG_H

// Pin Configuration
// SPI for SD Card
#define SD_SPI_PORT     spi0
#define SD_PIN_MISO     16
#define SD_PIN_CS       17
#define SD_PIN_SCK      18
#define SD_PIN_MOSI     19

// I2C for OLED Display
#define OLED_I2C_PORT   i2c0
#define OLED_I2C_SDA    8
#define OLED_I2C_SCL    9
#define OLED_I2C_ADDR   0x3C
#define OLED_WIDTH      128
#define OLED_HEIGHT     32  // Можно изменить на 64

// Input Configuration
// Uncomment one of these to select input mode
// #define USE_BUTTONS     // 3 buttons mode
#define USE_ENCODER     // Rotary encoder mode

#ifdef USE_BUTTONS
    #define BTN_UP_PIN      10
    #define BTN_DOWN_PIN    11
    #define BTN_OK_PIN      12
#endif

#ifdef USE_ENCODER
    #define ENC_A_PIN       10
    #define ENC_B_PIN       11
    #define ENC_BTN_PIN     12
    #define ENC_MICROSTEPS  4  // For stability
#endif

// USB Configuration
#define USB_VID         0x2E8A  // Raspberry Pi
#define USB_PID         0x000A  // Mass Storage Device

// Floppy Configuration
#define FLOPPY_SECTOR_SIZE      512
#define FLOPPY_SECTORS_PER_TRACK 18
#define FLOPPY_HEADS            2
#define FLOPPY_TRACKS           80
#define FLOPPY_TOTAL_SECTORS    (FLOPPY_SECTORS_PER_TRACK * FLOPPY_HEADS * FLOPPY_TRACKS)
#define FLOPPY_IMAGE_SIZE       (FLOPPY_TOTAL_SECTORS * FLOPPY_SECTOR_SIZE)  // 1.44MB

// SD Card Configuration
#define MAX_IMAGES      32
#define IMAGE_EXTENSION ".img"

// Display Configuration
#if OLED_HEIGHT == 32
    #define MENU_ITEMS_PER_PAGE     2  // 2 элемента для дисплея 32px
#elif OLED_HEIGHT == 64
    #define MENU_ITEMS_PER_PAGE     4  // 4 элемента для дисплея 64px
#else
    #define MENU_ITEMS_PER_PAGE     2  // По умолчанию
#endif

#define DEBOUNCE_TIME_MS        50

// FreeRTOS Configuration
#define TASK_PRIORITY_UI        2
#define TASK_PRIORITY_USB       3
#define TASK_PRIORITY_STORAGE   2

#define STACK_SIZE_UI           512
#define STACK_SIZE_USB          1024
#define STACK_SIZE_STORAGE      1024

#endif // CONFIG_H
