#ifndef __FLASHERX_STORAGE_H_
#define __FLASHERX_STORAGE_H_

#ifdef FILE_READ
#undef FILE_READ
#endif
#ifdef FILE_WRITE
#undef FILE_WRITE
#endif

#include <Arduino.h>
#include <LittleFS.h>
#include <SdFat.h>
#include <sdios.h>

typedef File HFsFile;

#if defined(__IMXRT1062__)
using HLittleFSBackend = LittleFS_QPINAND;
#else
using HLittleFSBackend = LittleFS_QSPIFlash;
#endif

enum class FileSystemType : uint8_t {
    SdFat = 0,
    LittleFS_QPINAND = 1,
};

struct HFileSystemCopyStats {
    uint32_t file_count = 0;
    uint32_t directory_count = 0;
    uint64_t byte_count = 0;
};

class HFileSystem {
public:
    bool begin(FileSystemType type);
    bool beginBackend(FileSystemType type);

    HFsFile open(const char* path, int mode = FILE_READ);
    HFsFile open(FileSystemType type, const char* path, int mode = FILE_READ);
    HFsFile openSdFat(const char* path, int mode = FILE_READ);
    bool isOpen(HFsFile& file) const;
    void getName(HFsFile& file, char* buffer, size_t buffer_size) const;

    bool exists(const char* path);
    bool exists(FileSystemType type, const char* path);
    bool mkdir(const char* path);
    bool mkdir(FileSystemType type, const char* path);
    bool rename(const char* old_path, const char* new_path);
    bool rename(FileSystemType type, const char* old_path, const char* new_path);
    bool remove(const char* path);
    bool remove(FileSystemType type, const char* path);
    bool removeSdFat(const char* path);
    bool rmdir(const char* path);
    bool rmdir(FileSystemType type, const char* path);

    bool copy(
        FileSystemType source_type,
        FileSystemType destination_type,
        const char* path,
        HFileSystemCopyStats* stats = nullptr
    );

    void ls(const char* path, uint8_t flags = 0, Print* output = nullptr);
    void errorPrint(print_t* output, const char* message);
    void errorPrint(print_t* output, const __FlashStringHelper* message);
    uint8_t sdErrorCode() const;

    FileSystemType getType() const {
        return _type;
    }

    bool isBackendStarted(FileSystemType type) const {
        return type == FileSystemType::LittleFS_QPINAND
            ? _little_fs_started
            : _sd_fat_started;
    }

    static bool isValidType(uint8_t type) {
        return type <= static_cast<uint8_t>(FileSystemType::LittleFS_QPINAND);
    }

    static const char* getTypeName(FileSystemType type);

private:
    bool beginSdFat();
    bool beginLittleFS();

    SdFat* _sd_fat = nullptr;
    HLittleFSBackend* _little_fs = nullptr;
    FileSystemType _type = FileSystemType::SdFat;
    bool _sd_fat_started = false;
    bool _little_fs_started = false;
};

extern HFileSystem SD;

#endif
