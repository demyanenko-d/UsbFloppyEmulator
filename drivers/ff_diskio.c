/**
 * @file ff_diskio.c
 * @brief FatFS disk I/O layer for SD card
 */

#include "ff.h"
#include "diskio.h"
#include "sd_card.h"
#include <string.h>

// Physical drive number
#define DEV_SD  0

/**
 * @brief Initialize disk drive
 */
DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv != DEV_SD) {
        return STA_NOINIT;
    }
    
    // SD card is initialized in sdcard_task
    if (sd_card_is_initialized()) {
        return 0;  // Success
    }
    
    return STA_NOINIT;
}

/**
 * @brief Get disk status
 */
DSTATUS disk_status(BYTE pdrv) {
    if (pdrv != DEV_SD) {
        return STA_NOINIT;
    }
    
    if (sd_card_is_initialized()) {
        return 0;  // OK
    }
    
    return STA_NOINIT;
}

/**
 * @brief Read sector(s)
 */
DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv != DEV_SD) {
        return RES_PARERR;
    }
    
    if (!sd_card_is_initialized()) {
        return RES_NOTRDY;
    }
    
    if (count == 1) {
        if (sd_card_read_block(sector, buff)) {
            return RES_OK;
        }
    } else {
        if (sd_card_read_blocks(sector, count, buff)) {
            return RES_OK;
        }
    }
    
    return RES_ERROR;
}

/**
 * @brief Write sector(s)
 */
#if FF_FS_READONLY == 0
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv != DEV_SD) {
        return RES_PARERR;
    }
    
    if (!sd_card_is_initialized()) {
        return RES_NOTRDY;
    }
    
    if (count == 1) {
        if (sd_card_write_block(sector, buff)) {
            return RES_OK;
        }
    } else {
        if (sd_card_write_blocks(sector, count, buff)) {
            return RES_OK;
        }
    }
    
    return RES_ERROR;
}
#endif

/**
 * @brief I/O control
 */
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    if (pdrv != DEV_SD) {
        return RES_PARERR;
    }
    
    if (!sd_card_is_initialized()) {
        return RES_NOTRDY;
    }
    
    const sd_card_info_t *info = sd_card_get_info();
    
    switch (cmd) {
        case CTRL_SYNC:
            // No cache, always synced
            return RES_OK;
            
        case GET_SECTOR_COUNT:
            *(LBA_t*)buff = info->sectors;
            return RES_OK;
            
        case GET_SECTOR_SIZE:
            *(WORD*)buff = 512;
            return RES_OK;
            
        case GET_BLOCK_SIZE:
            *(DWORD*)buff = 1;  // 1 sector
            return RES_OK;
            
        case CTRL_TRIM:
            // Not supported
            return RES_OK;
            
        default:
            return RES_PARERR;
    }
}

/**
 * @brief Get current time (for file timestamps)
 */
DWORD get_fattime(void) {
    // Return a fixed time: 2025-11-07 12:00:00
    // Format: bit31:25=Year(from 1980), bit24:21=Month, bit20:16=Day
    //         bit15:11=Hour, bit10:5=Minute, bit4:0=Second/2
    return ((2025 - 1980) << 25) | (11 << 21) | (7 << 16) | (12 << 11) | (0 << 5) | (0 >> 1);
}
