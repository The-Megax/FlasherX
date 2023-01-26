#ifndef __FLASHERX_H_
#define __FLASHERX_H_

#define NATIVE_SD 0
#define DISABLE_CODE_CHECK 1
#define CHECK_EEPROM_UPDATE_ENABLED 1
#define FLASHERX_EEPROM_ID 100

#include "crc.h"

#if NATIVE_SD
/*#include <SD.h>*/
#define SD_flash SD
#else
#include <sdfat.h>
extern SdFat SD_flash;
#define BUILTIN_SDCARD SdioConfig(DMA_SDIO)
#endif

extern bool is_sd_flash;
extern uint32_t sd_file_checksum;

#define FLASHERX_EHEX_SUPPORT 1

#define FLASHERX_HEX_FILE_NAME "FlasherX.hex"
#define FLASHERX_CHECKSUM_FILE_NAME "checksum.txt"

void FlasherX();

#endif