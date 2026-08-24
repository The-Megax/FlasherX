#include "FlasherXStorage.h"

#include <new>

namespace {
constexpr size_t kCopyBufferSize = 4096;
constexpr size_t kCopyPathSize = 512;
constexpr uint8_t kCopyMaxDepth = 24;

class SdFatFileImpl : public FileImpl {
public:
    explicit SdFatFileImpl(const FsFile& file): _file(file) {}

    ~SdFatFileImpl() override {
        close();
    }

    size_t read(void* buffer, size_t length) override {
        int result = _file.read(buffer, length);
        return result > 0 ? static_cast<size_t>(result) : 0;
    }

    size_t write(const void* buffer, size_t length) override {
        return _file.write(buffer, length);
    }

    int available() override {
        return _file.available();
    }

    int peek() override {
        return _file.peek();
    }

    void flush() override {
        _file.flush();
    }

    bool truncate(uint64_t size = 0) override {
        return _file.truncate(size);
    }

    bool seek(uint64_t position, int mode) override {
        if(mode == SeekSet)
            return _file.seekSet(position);
        if(mode == SeekCur)
            return _file.seekCur(position);
        if(mode == SeekEnd)
            return _file.seekEnd(position);
        return false;
    }

    uint64_t position() override {
        return _file.curPosition();
    }

    uint64_t size() override {
        return _file.size();
    }

    void close() override {
        if(_name != nullptr) {
            free(_name);
            _name = nullptr;
        }

        if(_file.isOpen())
            _file.close();
    }

    bool isOpen() override {
        return _file.isOpen();
    }

    const char* name() override {
        if(_name == nullptr) {
            _name = static_cast<char*>(calloc(256, sizeof(char)));
            if(_name != nullptr)
                _file.getName(_name, 256);
        }

        return _name != nullptr ? _name : "";
    }

    bool isDirectory() override {
        return _file.isDirectory();
    }

    File openNextFile(uint8_t mode = FILE_READ) override {
        oflag_t flags = mode == FILE_READ ? O_RDONLY : O_RDWR;
        FsFile file = _file.openNextFile(flags);
        if(!file)
            return File();

        return File(new SdFatFileImpl(file));
    }

    void rewindDirectory() override {
        _file.rewindDirectory();
    }

private:
    FsFile _file;
    char* _name = nullptr;
};

template<typename T>
T* allocateFileSystem() {
    void* memory = malloc(sizeof(T));
    if(memory == nullptr)
        return nullptr;

    return new(memory) T();
}

struct CopyContext {
    HFileSystem* file_system;
    FileSystemType source_type;
    FileSystemType destination_type;
    HFileSystemCopyStats* stats;
    uint8_t* buffer;
    char* path;
    char* entry_name;
};

FLASHMEM bool isRootPath(const char* path) {
    return path[0] == '/' && path[1] == '\0';
}

FLASHMEM bool getPathType(HFileSystem& file_system, FileSystemType type, const char* path, bool& is_directory, uint64_t& size) {
    if(isRootPath(path)) {
        is_directory = true;
        size = 0;
        return file_system.beginBackend(type);
    }

    HFsFile file = file_system.open(type, path, FILE_READ);
    if(!file)
        return false;

    is_directory = file.isDirectory();
    size = file.size();
    file.close();
    return true;
}

FLASHMEM bool ensureDirectory(HFileSystem& file_system, FileSystemType type, const char* path) {
    if(isRootPath(path))
        return file_system.beginBackend(type);

    if(file_system.exists(type, path)) {
        bool is_directory = false;
        uint64_t size = 0;
        return getPathType(file_system, type, path, is_directory, size) && is_directory;
    }

    return file_system.mkdir(type, path);
}

FLASHMEM bool ensureParentDirectories(HFileSystem& file_system, FileSystemType type, char* path) {
    char* separator = path;
    while((separator = strchr(separator + 1, '/')) != nullptr) {
        char saved = *separator;
        *separator = '\0';
        bool result = ensureDirectory(file_system, type, path);
        *separator = saved;

        if(!result)
            return false;
    }

    return true;
}

FLASHMEM bool getDirectoryEntry(CopyContext& context, uint32_t index) {
    HFsFile directory = context.file_system->open(context.source_type, context.path, FILE_READ);
    if(!directory || !directory.isDirectory()) {
        directory.close();
        return false;
    }

    HFsFile entry;
    for(uint32_t current_index = 0; current_index <= index; current_index++) {
        entry = directory.openNextFile(FILE_READ);
        if(!entry) {
            directory.close();
            return false;
        }

        if(current_index != index)
            entry.close();
    }

    const char* name = entry.name();
    const char* last_separator = strrchr(name, '/');
    if(last_separator != nullptr)
        name = last_separator + 1;

    strlcpy(context.entry_name, name, kCopyPathSize);
    entry.close();
    directory.close();
    return context.entry_name[0] != '\0';
}

FLASHMEM bool copyPath(CopyContext& context, uint8_t depth) {
    if(depth > kCopyMaxDepth)
        return false;

    bool is_directory = false;
    uint64_t source_size = 0;
    if(!getPathType(*context.file_system, context.source_type, context.path, is_directory, source_size))
        return false;

    if(is_directory) {
        if(!ensureDirectory(*context.file_system, context.destination_type, context.path))
            return false;

        context.stats->directory_count++;

        for(uint32_t entry_index = 0; ; entry_index++) {
            size_t path_length = strlen(context.path);
            if(!getDirectoryEntry(context, entry_index))
                break;

            size_t name_length = strlen(context.entry_name);
            bool is_root = path_length == 1 && context.path[0] == '/';
            size_t required_size = path_length + (is_root ? 0 : 1) + name_length + 1;
            if(required_size > kCopyPathSize)
                return false;

            if(!is_root)
                context.path[path_length++] = '/';

            memcpy(context.path + path_length, context.entry_name, name_length + 1);
            bool result = copyPath(context, depth + 1);
            context.path[is_root ? 1 : path_length - 1] = '\0';

            if(!result)
                return false;
        }

        return true;
    }

    if(!ensureParentDirectories(*context.file_system, context.destination_type, context.path))
        return false;

    if(context.file_system->exists(context.destination_type, context.path)) {
        bool destination_is_directory = false;
        uint64_t destination_size = 0;
        if(!getPathType(*context.file_system, context.destination_type, context.path, destination_is_directory, destination_size)
            || destination_is_directory
            || !context.file_system->remove(context.destination_type, context.path)) {
            return false;
        }
    }

    if(source_size == 0) {
        HFsFile destination = context.file_system->open(
            context.destination_type,
            context.path,
            O_RDWR | O_CREAT | O_TRUNC
        );
        if(!destination)
            return false;
        destination.flush();
        destination.close();
    }

    uint64_t offset = 0;
    while(offset < source_size) {
        size_t requested_size = static_cast<size_t>(min(static_cast<uint64_t>(kCopyBufferSize), source_size - offset));

        HFsFile source = context.file_system->open(context.source_type, context.path, FILE_READ);
        if(!source || !source.seek(offset)) {
            source.close();
            return false;
        }

        size_t read_size = source.read(context.buffer, requested_size);
        source.close();
        if(read_size == 0)
            return false;

        int destination_mode = O_RDWR | O_CREAT;
        if(offset == 0)
            destination_mode |= O_TRUNC;

        HFsFile destination = context.file_system->open(context.destination_type, context.path, destination_mode);
        if(!destination || !destination.seek(offset)) {
            destination.close();
            return false;
        }

        size_t write_size = destination.write(context.buffer, read_size);
        destination.flush();
        destination.close();
        if(write_size != read_size)
            return false;

        offset += read_size;
        context.stats->byte_count += read_size;
    }

    context.stats->file_count++;
    return true;
}
}

HFileSystem SD;

bool HFileSystem::begin(FileSystemType type) {
    if(!isValidType(static_cast<uint8_t>(type)))
        type = FileSystemType::SdFat;

    _type = type;
    return beginBackend(type);
}

bool HFileSystem::beginBackend(FileSystemType type) {
    if(type == FileSystemType::LittleFS_QPINAND)
        return beginLittleFS();

    return beginSdFat();
}

bool HFileSystem::beginSdFat() {
    if(_sd_fat_started)
        return true;

    if(_sd_fat == nullptr)
        _sd_fat = allocateFileSystem<SdFat>();

    if(_sd_fat == nullptr)
        return false;

    _sd_fat_started = _sd_fat->begin(SdioConfig(FIFO_SDIO));
    return _sd_fat_started;
}

bool HFileSystem::beginLittleFS() {
    if(_little_fs_started)
        return true;

    if(_little_fs == nullptr)
        _little_fs = allocateFileSystem<HLittleFSBackend>();

    if(_little_fs == nullptr)
        return false;

    _little_fs_started = _little_fs->begin();
    return _little_fs_started;
}

HFsFile HFileSystem::open(const char* path, int mode) {
    return open(_type, path, mode);
}

HFsFile HFileSystem::open(FileSystemType type, const char* path, int mode) {
    if(!beginBackend(type))
        return HFsFile();

    if(type == FileSystemType::LittleFS_QPINAND) {
        uint8_t little_fs_mode = FILE_READ;
        if(mode == FILE_WRITE || (mode & (O_AT_END | O_APPEND)) != 0)
            little_fs_mode = FILE_WRITE;
        else if(mode != FILE_READ)
            little_fs_mode = FILE_WRITE_BEGIN;

        HFsFile file = _little_fs->open(path, little_fs_mode);
        if(file && (mode & O_TRUNC) != 0)
            file.truncate(0);

        return file;
    }

    return openSdFat(path, mode);
}

HFsFile HFileSystem::openSdFat(const char* path, int mode) {
    if(!beginSdFat())
        return HFsFile();

    oflag_t flags = static_cast<oflag_t>(mode);
    if(mode == FILE_WRITE)
        flags = O_RDWR | O_CREAT | O_AT_END;
    else if(mode == FILE_WRITE_BEGIN)
        flags = O_RDWR | O_CREAT;

    FsFile file = _sd_fat->open(path, flags);
    if(!file)
        return HFsFile();

    return HFsFile(new SdFatFileImpl(file));
}

bool HFileSystem::isOpen(HFsFile& file) const {
    return static_cast<bool>(file);
}

void HFileSystem::getName(HFsFile& file, char* buffer, size_t buffer_size) const {
    strlcpy(buffer, file.name(), buffer_size);
}

bool HFileSystem::exists(const char* path) {
    return exists(_type, path);
}

bool HFileSystem::exists(FileSystemType type, const char* path) {
    if(!beginBackend(type))
        return false;

    return type == FileSystemType::LittleFS_QPINAND
        ? _little_fs->exists(path)
        : _sd_fat->exists(path);
}

bool HFileSystem::mkdir(const char* path) {
    return mkdir(_type, path);
}

bool HFileSystem::mkdir(FileSystemType type, const char* path) {
    if(!beginBackend(type))
        return false;

    return type == FileSystemType::LittleFS_QPINAND
        ? _little_fs->mkdir(path)
        : _sd_fat->mkdir(path);
}

bool HFileSystem::rename(const char* old_path, const char* new_path) {
    return rename(_type, old_path, new_path);
}

bool HFileSystem::rename(FileSystemType type, const char* old_path, const char* new_path) {
    if(!beginBackend(type))
        return false;

    return type == FileSystemType::LittleFS_QPINAND
        ? _little_fs->rename(old_path, new_path)
        : _sd_fat->rename(old_path, new_path);
}

bool HFileSystem::remove(const char* path) {
    return remove(_type, path);
}

bool HFileSystem::remove(FileSystemType type, const char* path) {
    if(!beginBackend(type))
        return false;

    return type == FileSystemType::LittleFS_QPINAND
        ? _little_fs->remove(path)
        : _sd_fat->remove(path);
}

bool HFileSystem::removeSdFat(const char* path) {
    return remove(FileSystemType::SdFat, path);
}

bool HFileSystem::rmdir(const char* path) {
    return rmdir(_type, path);
}

bool HFileSystem::rmdir(FileSystemType type, const char* path) {
    if(!beginBackend(type))
        return false;

    return type == FileSystemType::LittleFS_QPINAND
        ? _little_fs->rmdir(path)
        : _sd_fat->rmdir(path);
}

FLASHMEM bool HFileSystem::copy(
    FileSystemType source_type,
    FileSystemType destination_type,
    const char* path,
    HFileSystemCopyStats* stats
) {
    if(source_type == destination_type || path == nullptr || path[0] != '/')
        return false;

    if(!beginBackend(source_type) || !beginBackend(destination_type))
        return false;

    uint8_t* buffer = static_cast<uint8_t*>(malloc(kCopyBufferSize));
    char* copy_path = static_cast<char*>(calloc(kCopyPathSize, sizeof(char)));
    char* entry_name = static_cast<char*>(calloc(kCopyPathSize, sizeof(char)));
    if(buffer == nullptr || copy_path == nullptr || entry_name == nullptr) {
        free(buffer);
        free(copy_path);
        free(entry_name);
        return false;
    }

    HFileSystemCopyStats local_stats;
    HFileSystemCopyStats* result_stats = stats != nullptr ? stats : &local_stats;
    *result_stats = HFileSystemCopyStats();

    strlcpy(copy_path, path, kCopyPathSize);
    size_t path_length = strlen(copy_path);
    while(path_length > 1 && copy_path[path_length - 1] == '/')
        copy_path[--path_length] = '\0';

    CopyContext context = {
        this,
        source_type,
        destination_type,
        result_stats,
        buffer,
        copy_path,
        entry_name,
    };

    bool result = strlen(path) < kCopyPathSize && copyPath(context, 0);
    free(entry_name);
    free(copy_path);
    free(buffer);
    return result;
}

FLASHMEM bool HFileSystem::ls(FileSystemType type, const char* path, Print* output) {
    if(path == nullptr || path[0] != '/')
        return false;

    if(output == nullptr)
        output = &Serial;

    HFsFile directory = open(type, path, FILE_READ);
    if(!directory || !directory.isDirectory()) {
        directory.close();
        return false;
    }

    while(true) {
        HFsFile entry = directory.openNextFile(FILE_READ);
        if(!entry)
            break;

        bool is_directory = entry.isDirectory();
        output->print(is_directory ? F("[DIR] ") : F("[FILE] "));
        output->print(entry.name());

        if(is_directory) {
            output->println();
        }
        else {
            output->print(F(" ("));
            output->print(entry.size());
            output->println(F(" bytes)"));
        }

        entry.close();
    }

    directory.close();
    return true;
}

void HFileSystem::ls(const char* path, uint8_t flags, Print* output) {
    if(output == nullptr)
        output = &Serial;

    if(_type == FileSystemType::SdFat) {
        if(beginSdFat())
            _sd_fat->ls(output, path, flags);
        return;
    }

    ls(_type, path, output);
}

void HFileSystem::errorPrint(print_t* output, const char* message) {
    if(_type == FileSystemType::SdFat && _sd_fat != nullptr) {
        _sd_fat->errorPrint(output, message);
        return;
    }

    output->print(F("error: "));
    output->println(message);
}

void HFileSystem::errorPrint(print_t* output, const __FlashStringHelper* message) {
    if(_type == FileSystemType::SdFat && _sd_fat != nullptr) {
        _sd_fat->errorPrint(output, message);
        return;
    }

    output->print(F("error: "));
    output->println(message);
}

uint8_t HFileSystem::sdErrorCode() const {
    return _sd_fat != nullptr ? _sd_fat->sdErrorCode() : 0;
}

const char* HFileSystem::getTypeName(FileSystemType type) {
    return type == FileSystemType::LittleFS_QPINAND ? "LittleFS_QPINAND" : "SdFat";
}
