/**
 * @file sd_card.c
 * @brief Low-level SD Card driver implementation
 */

#include "sd_card.h"
#include "pico/stdlib.h"
#include <string.h>
#include <stdio.h>

// SD Card Commands
#define CMD0    0       // GO_IDLE_STATE
#define CMD1    1       // SEND_OP_COND (MMC)
#define CMD8    8       // SEND_IF_COND
#define CMD9    9       // SEND_CSD
#define CMD10   10      // SEND_CID
#define CMD12   12      // STOP_TRANSMISSION
#define CMD16   16      // SET_BLOCKLEN
#define CMD17   17      // READ_SINGLE_BLOCK
#define CMD18   18      // READ_MULTIPLE_BLOCK
#define CMD23   23      // SET_BLOCK_COUNT
#define CMD24   24      // WRITE_BLOCK
#define CMD25   25      // WRITE_MULTIPLE_BLOCK
#define CMD55   55      // APP_CMD
#define CMD58   58      // READ_OCR
#define ACMD41  41      // SD_SEND_OP_COND (must be preceded by CMD55)

// SD Card responses
#define R1_READY_STATE      0x00
#define R1_IDLE_STATE       0x01

// Data tokens
#define TOKEN_START_BLOCK       0xFE
#define TOKEN_START_MULTI       0xFC
#define TOKEN_STOP_MULTI        0xFD

// SPI settings
static spi_inst_t *spi_instance = NULL;
static uint cs_pin = 0;
static sd_card_info_t card_info = {0};

/**
 * @brief Chip select control
 */
static inline void cs_select(void) {
    gpio_put(cs_pin, 0);
    sleep_us(1);
}

static inline void cs_deselect(void) {
    sleep_us(1);
    gpio_put(cs_pin, 1);
    sleep_us(1);
}

/**
 * @brief Send dummy byte and read response
 */
static inline uint8_t spi_transfer(uint8_t data) {
    uint8_t rx_data;
    spi_write_read_blocking(spi_instance, &data, &rx_data, 1);
    return rx_data;
}

/**
 * @brief Wait for SD card ready
 */
static bool sd_wait_ready(uint32_t timeout_ms) {
    absolute_time_t timeout = make_timeout_time_ms(timeout_ms);
    
    while (spi_transfer(0xFF) != 0xFF) {
        if (time_reached(timeout)) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Send command to SD card
 */
static uint8_t sd_send_command(uint8_t cmd, uint32_t arg) {
    // Wait for card ready
    if (!sd_wait_ready(500)) {
        return 0xFF;
    }
    
    // Send command packet
    spi_transfer(0x40 | cmd);
    spi_transfer((uint8_t)(arg >> 24));
    spi_transfer((uint8_t)(arg >> 16));
    spi_transfer((uint8_t)(arg >> 8));
    spi_transfer((uint8_t)arg);
    
    // CRC (only matters for CMD0 and CMD8)
    uint8_t crc = 0xFF;
    if (cmd == CMD0) crc = 0x95;
    if (cmd == CMD8) crc = 0x87;
    spi_transfer(crc);
    
    // Wait for response (not 0xFF)
    uint8_t response;
    for (int i = 0; i < 10; i++) {
        response = spi_transfer(0xFF);
        if (response != 0xFF) {
            return response;
        }
    }
    
    return 0xFF;
}

/**
 * @brief Read data block from SD card
 */
static bool sd_read_data_block(uint8_t *buffer, uint16_t length) {
    absolute_time_t timeout = make_timeout_time_ms(200);
    
    // Wait for start token
    uint8_t token;
    do {
        token = spi_transfer(0xFF);
        if (time_reached(timeout)) {
            return false;
        }
    } while (token == 0xFF);
    
    if (token != TOKEN_START_BLOCK) {
        return false;
    }
    
    // Read data
    for (uint16_t i = 0; i < length; i++) {
        buffer[i] = spi_transfer(0xFF);
    }
    
    // Read CRC (and ignore it)
    spi_transfer(0xFF);
    spi_transfer(0xFF);
    
    return true;
}

/**
 * @brief Write data block to SD card
 */
static bool sd_write_data_block(const uint8_t *buffer, uint8_t token) {
    // Wait for card ready
    if (!sd_wait_ready(500)) {
        return false;
    }
    
    // Send token
    spi_transfer(token);
    
    if (token != TOKEN_STOP_MULTI) {
        // Send data
        for (uint16_t i = 0; i < 512; i++) {
            spi_transfer(buffer[i]);
        }
        
        // Send dummy CRC
        spi_transfer(0xFF);
        spi_transfer(0xFF);
        
        // Check response
        uint8_t response = spi_transfer(0xFF);
        if ((response & 0x1F) != 0x05) {
            return false;
        }
        
        // Wait for write completion
        if (!sd_wait_ready(500)) {
            return false;
        }
    }
    
    return true;
}

/**
 * @brief Initialize SD card
 */
bool sd_card_init(spi_inst_t *spi, uint cs) {
    spi_instance = spi;
    cs_pin = cs;
    
    memset(&card_info, 0, sizeof(card_info));
    
    printf("[SD] Initializing SD card...\n");
    
    // Setup SPI at low speed (400 kHz for initialization)
    spi_init(spi_instance, 400 * 1000);
    
    // Setup CS pin
    gpio_init(cs_pin);
    gpio_set_dir(cs_pin, GPIO_OUT);
    cs_deselect();
    
    // Send 80 dummy clocks with CS high
    for (int i = 0; i < 10; i++) {
        spi_transfer(0xFF);
    }
    
    // Enter SPI mode (CMD0)
    cs_select();
    uint8_t r1;
    int retry = 0;
    do {
        r1 = sd_send_command(CMD0, 0);
        retry++;
        if (retry > 100) {
            printf("[SD] CMD0 failed\n");
            cs_deselect();
            return false;
        }
    } while (r1 != R1_IDLE_STATE);
    
    // Check voltage range (CMD8)
    r1 = sd_send_command(CMD8, 0x1AA);
    if (r1 == R1_IDLE_STATE) {
        // SD v2.x
        uint8_t ocr[4];
        for (int i = 0; i < 4; i++) {
            ocr[i] = spi_transfer(0xFF);
        }
        
        if (ocr[2] == 0x01 && ocr[3] == 0xAA) {
            // Valid response, try to initialize
            retry = 0;
            do {
                // Send ACMD41 with HCS bit
                sd_send_command(CMD55, 0);
                r1 = sd_send_command(ACMD41, 0x40000000);
                retry++;
                sleep_ms(10);
                if (retry > 100) {
                    printf("[SD] ACMD41 timeout\n");
                    cs_deselect();
                    return false;
                }
            } while (r1 != R1_READY_STATE);
            
            // Read OCR to check card type
            if (sd_send_command(CMD58, 0) == R1_READY_STATE) {
                for (int i = 0; i < 4; i++) {
                    ocr[i] = spi_transfer(0xFF);
                }
                
                if (ocr[0] & 0x40) {
                    card_info.type = SD_CARD_TYPE_SDHC;
                    printf("[SD] Card type: SDHC/SDXC\n");
                } else {
                    card_info.type = SD_CARD_TYPE_SD2;
                    printf("[SD] Card type: SD v2\n");
                }
            }
        }
    } else {
        // SD v1.x or MMC
        retry = 0;
        do {
            sd_send_command(CMD55, 0);
            r1 = sd_send_command(ACMD41, 0);
            retry++;
            sleep_ms(10);
            if (retry > 100) {
                printf("[SD] SD v1 init failed\n");
                cs_deselect();
                return false;
            }
        } while (r1 != R1_READY_STATE);
        
        card_info.type = SD_CARD_TYPE_SD1;
        printf("[SD] Card type: SD v1\n");
        
        // Set block size to 512 for SD v1
        if (sd_send_command(CMD16, 512) != R1_READY_STATE) {
            printf("[SD] Failed to set block size\n");
            cs_deselect();
            return false;
        }
    }
    
    // Read CSD register
    if (sd_send_command(CMD9, 0) == R1_READY_STATE) {
        if (sd_read_data_block(card_info.csd, 16)) {
            // Calculate capacity
            if (card_info.type == SD_CARD_TYPE_SDHC) {
                // SDHC/SDXC: C_SIZE is 22 bits
                uint32_t c_size = ((uint32_t)card_info.csd[7] << 16) |
                                 ((uint32_t)card_info.csd[8] << 8) |
                                 card_info.csd[9];
                c_size = (c_size >> 0) & 0x3FFFFF;
                card_info.sectors = (c_size + 1) * 1024;
            } else {
                // SD v1/v2: Calculate from C_SIZE, C_SIZE_MULT, READ_BL_LEN
                uint16_t c_size = ((uint16_t)(card_info.csd[6] & 0x03) << 10) |
                                 ((uint16_t)card_info.csd[7] << 2) |
                                 ((card_info.csd[8] & 0xC0) >> 6);
                uint8_t c_size_mult = ((card_info.csd[9] & 0x03) << 1) |
                                     ((card_info.csd[10] & 0x80) >> 7);
                uint8_t read_bl_len = card_info.csd[5] & 0x0F;
                
                uint32_t block_nr = (c_size + 1) * (1 << (c_size_mult + 2));
                uint32_t block_len = 1 << read_bl_len;
                card_info.sectors = (block_nr * block_len) / 512;
            }
            
            card_info.capacity_mb = (card_info.sectors / 2) / 1024;
            printf("[SD] Capacity: %lu MB (%lu sectors)\n", 
                   card_info.capacity_mb, card_info.sectors);
        }
    }
    
    // Read CID register
    if (sd_send_command(CMD10, 0) == R1_READY_STATE) {
        sd_read_data_block(card_info.cid, 16);
    }
    
    cs_deselect();
    
    // Increase SPI speed to maximum (12.5 MHz for RP2040)
    spi_set_baudrate(spi_instance, 12500 * 1000);
    printf("[SD] SPI speed set to 12.5 MHz\n");
    
    card_info.initialized = true;
    printf("[SD] Initialization complete\n");
    
    return true;
}

/**
 * @brief Deinitialize SD card
 */
void sd_card_deinit(void) {
    card_info.initialized = false;
    spi_instance = NULL;
}

/**
 * @brief Check if SD card is initialized
 */
bool sd_card_is_initialized(void) {
    return card_info.initialized;
}

/**
 * @brief Get SD card information
 */
const sd_card_info_t* sd_card_get_info(void) {
    return &card_info;
}

/**
 * @brief Read single block
 */
bool sd_card_read_block(uint32_t block, uint8_t *buffer) {
    if (!card_info.initialized) {
        return false;
    }
    
    // Convert block number to byte address for non-SDHC cards
    uint32_t address = (card_info.type == SD_CARD_TYPE_SDHC) ? block : block * 512;
    
    cs_select();
    
    bool success = false;
    if (sd_send_command(CMD17, address) == R1_READY_STATE) {
        success = sd_read_data_block(buffer, 512);
    }
    
    cs_deselect();
    return success;
}

/**
 * @brief Write single block
 */
bool sd_card_write_block(uint32_t block, const uint8_t *buffer) {
    if (!card_info.initialized) {
        return false;
    }
    
    uint32_t address = (card_info.type == SD_CARD_TYPE_SDHC) ? block : block * 512;
    
    cs_select();
    
    bool success = false;
    if (sd_send_command(CMD24, address) == R1_READY_STATE) {
        success = sd_write_data_block(buffer, TOKEN_START_BLOCK);
    }
    
    cs_deselect();
    return success;
}

/**
 * @brief Read multiple blocks
 */
bool sd_card_read_blocks(uint32_t block, uint32_t count, uint8_t *buffer) {
    if (!card_info.initialized) {
        return false;
    }
    
    uint32_t address = (card_info.type == SD_CARD_TYPE_SDHC) ? block : block * 512;
    
    cs_select();
    
    bool success = false;
    if (sd_send_command(CMD18, address) == R1_READY_STATE) {
        success = true;
        for (uint32_t i = 0; i < count; i++) {
            if (!sd_read_data_block(buffer + i * 512, 512)) {
                success = false;
                break;
            }
        }
        
        // Stop transmission
        sd_send_command(CMD12, 0);
        spi_transfer(0xFF); // Skip stuff byte
    }
    
    cs_deselect();
    return success;
}

/**
 * @brief Write multiple blocks
 */
bool sd_card_write_blocks(uint32_t block, uint32_t count, const uint8_t *buffer) {
    if (!card_info.initialized) {
        return false;
    }
    
    uint32_t address = (card_info.type == SD_CARD_TYPE_SDHC) ? block : block * 512;
    
    cs_select();
    
    bool success = false;
    if (sd_send_command(CMD25, address) == R1_READY_STATE) {
        success = true;
        for (uint32_t i = 0; i < count; i++) {
            if (!sd_write_data_block(buffer + i * 512, TOKEN_START_MULTI)) {
                success = false;
                break;
            }
        }
        
        // Stop transmission
        sd_write_data_block(NULL, TOKEN_STOP_MULTI);
    }
    
    cs_deselect();
    return success;
}
