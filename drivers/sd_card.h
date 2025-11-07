/**
 * @file sd_card.h
 * @brief Low-level SD Card driver for SPI mode
 * 
 * Supports SD, SDHC and SDXC cards in SPI mode
 */

#ifndef SD_CARD_H
#define SD_CARD_H

#include <stdint.h>
#include <stdbool.h>
#include "hardware/spi.h"

// SD Card types
typedef enum {
    SD_CARD_TYPE_UNKNOWN = 0,
    SD_CARD_TYPE_SD1,        // SD v1.x
    SD_CARD_TYPE_SD2,        // SD v2.x (Standard Capacity)
    SD_CARD_TYPE_SDHC        // SDHC/SDXC (High Capacity)
} sd_card_type_t;

// SD Card information
typedef struct {
    sd_card_type_t type;
    uint32_t sectors;        // Total sectors
    uint32_t capacity_mb;    // Capacity in MB
    uint8_t csd[16];         // Card-Specific Data
    uint8_t cid[16];         // Card Identification
    bool initialized;
} sd_card_info_t;

/**
 * @brief Initialize SD card
 * @param spi SPI instance
 * @param cs_pin Chip Select pin
 * @return true on success
 */
bool sd_card_init(spi_inst_t *spi, uint cs_pin);

/**
 * @brief Deinitialize SD card
 */
void sd_card_deinit(void);

/**
 * @brief Check if SD card is initialized
 * @return true if initialized
 */
bool sd_card_is_initialized(void);

/**
 * @brief Get SD card information
 * @return Pointer to card info structure
 */
const sd_card_info_t* sd_card_get_info(void);

/**
 * @brief Read single block from SD card
 * @param block Block number
 * @param buffer Buffer to read into (must be 512 bytes)
 * @return true on success
 */
bool sd_card_read_block(uint32_t block, uint8_t *buffer);

/**
 * @brief Write single block to SD card
 * @param block Block number
 * @param buffer Buffer to write from (must be 512 bytes)
 * @return true on success
 */
bool sd_card_write_block(uint32_t block, const uint8_t *buffer);

/**
 * @brief Read multiple blocks from SD card
 * @param block Starting block number
 * @param count Number of blocks
 * @param buffer Buffer to read into
 * @return true on success
 */
bool sd_card_read_blocks(uint32_t block, uint32_t count, uint8_t *buffer);

/**
 * @brief Write multiple blocks to SD card
 * @param block Starting block number
 * @param count Number of blocks
 * @param buffer Buffer to write from
 * @return true on success
 */
bool sd_card_write_blocks(uint32_t block, uint32_t count, const uint8_t *buffer);

#endif // SD_CARD_H
