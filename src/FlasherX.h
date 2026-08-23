#ifndef __FLASHERX_H_
#define __FLASHERX_H_

#define NATIVE_SD 0
#define DISABLE_CODE_CHECK 1
#define CHECK_EEPROM_UPDATE_ENABLED 1
#define FLASHERX_EEPROM_ID 100
#define FLASHERX_FILESYSTEM_EEPROM_ID 105

#include "crc.h"
#include "FlasherXStorage.h"

/*#include <SD.h>*/

extern bool is_sd_flash;
extern uint32_t sd_file_checksum;
extern FileSystemType flasherx_update_file_system_type;

#define FLASHERX_EHEX_SUPPORT 1

#define FLASHERX_HEX_FILE_NAME "FlasherX.hex"
#define FLASHERX_CHECKSUM_FILE_NAME "checksum.txt"

void FlasherX(bool is_secure);
void FlasherXRemoveUpdateFiles();

#endif
