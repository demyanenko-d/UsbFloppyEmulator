# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(UsbFloppyEmu C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# FreeRTOS kernel location
set(FREERTOS_KERNEL_PATH ${CMAKE_CURRENT_LIST_DIR}/extras/FreeRTOS-Kernel)

# SSD1306 library location
set(SSD1306_PATH ${CMAKE_CURRENT_LIST_DIR}/extras/pico-ssd1306)

# FatFS library location
set(FATFS_PATH ${CMAKE_CURRENT_LIST_DIR}/extras/fatfs)

# Add FreeRTOS as a subdirectory
add_subdirectory(${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2040 FreeRTOS-Kernel)

# Collect all task source files
set(TASK_SOURCES
    tasks/oled_task.c
    tasks/control_task.c
    tasks/menu_task.c
    tasks/sdcard_task.c
    tasks/floppy_emu_task.c
    tasks/usb_task.c
    tasks/led_task.c
    tasks.c
)

# SSD1306 library sources
set(SSD1306_SOURCES
    ${SSD1306_PATH}/ssd1306.c
)

# SD card driver sources
set(SD_SOURCES
    drivers/sd_card.c
    drivers/ff_diskio.c
)

# FatFS library sources
set(FATFS_SOURCES
    ${FATFS_PATH}/source/ff.c
    ${FATFS_PATH}/source/ffsystem.c
    ${FATFS_PATH}/source/ffunicode.c
)

# Add executable with all sources
add_executable(UsbFloppyEmu 
    UsbFloppyEmu.cpp
    usb_descriptors.c
    ${TASK_SOURCES}
    ${SSD1306_SOURCES}
    ${SD_SOURCES}
    ${FATFS_SOURCES}
)

pico_set_program_name(UsbFloppyEmu "UsbFloppyEmu")
pico_set_program_version(UsbFloppyEmu "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(UsbFloppyEmu 1)
pico_enable_stdio_usb(UsbFloppyEmu 0)

# Add the standard library to the build
target_link_libraries(UsbFloppyEmu
    pico_stdlib
    pico_unique_id
    hardware_spi
    hardware_i2c
    hardware_gpio
    tinyusb_device
    tinyusb_board
    FreeRTOS-Kernel
    FreeRTOS-Kernel-Heap4
)

# Add the standard include files to the build
target_include_directories(UsbFloppyEmu PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/tasks
    ${CMAKE_CURRENT_LIST_DIR}/drivers
    ${FREERTOS_KERNEL_PATH}/include
    ${SSD1306_PATH}
    ${FATFS_PATH}/source
)

pico_add_extra_outputs(UsbFloppyEmu)

