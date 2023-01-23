#ifndef __FLASHERX_H_
#define __FLASHERX_H_

#define NATIVE_SD 0
#define DISABLE_CODE_CHECK 1

#if NATIVE_SD
/*#include <SD.h>*/
#define SD_flash SD
#else
#include <sdfat.h>
extern SdFat SD_flash;
#define BUILTIN_SDCARD SdioConfig(DMA_SDIO)
#endif

extern bool is_sd_flash;

#define HEX_FILE_NAME "FlasherX.hex"

void FlasherX();

#endif