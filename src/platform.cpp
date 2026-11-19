// SPDX-License-Identifier: GPL-3.0-or-later
#include "Platform.h"

#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <dlfcn.h>
#include <fcntl.h>
#include <mutex>
#include <thread>
#include <unistd.h>

namespace melonDS::Platform {
static FILE* stream(FileHandle* f) { return reinterpret_cast<FILE*>(f); }

FileHandle* OpenFile(const std::string& name, FileMode mode)
{
    const bool append = mode & Append;
    const bool write = append || (mode & Write);
    const bool read = mode & Read;
    if (!read && !write) return nullptr;
    int flags = write ? (read ? O_RDWR : O_WRONLY) : O_RDONLY;
    if (write && !(mode & NoCreate)) flags |= O_CREAT;
    if (append) flags |= O_APPEND;
    else if (write && !(mode & Preserve)) flags |= O_TRUNC;

    int fd = open(name.c_str(), flags | O_CLOEXEC, 0600);
    if (fd < 0) return nullptr;
    const char* access = append ? (read ? "a+b" : "ab") :
                          write ? (read ? "r+b" : "wb") : "rb";
    FILE* f = fdopen(fd, access);
    if (!f) close(fd);
    return reinterpret_cast<FileHandle*>(f);
}

std::string GetLocalFilePath(const std::string& name) { return name; }
FileHandle* OpenLocalFile(const std::string& name, FileMode mode) { return OpenFile(name, mode); }
bool CloseFile(FileHandle* f) { return fclose(stream(f)) == 0; }
bool IsEndOfFile(FileHandle* f) { return feof(stream(f)) != 0; }
bool FileReadLine(char* data, int n, FileHandle* f) { return fgets(data, n, stream(f)); }
bool FileExists(const std::string& name) { return access(name.c_str(), F_OK) == 0; }
bool LocalFileExists(const std::string& name) { return FileExists(name); }
bool CheckFileWritable(const std::string& name)
{
    auto* f = OpenFile(name, Append);
    return f && CloseFile(f);
}
bool CheckLocalFileWritable(const std::string& name) { return CheckFileWritable(name); }

bool FileSeek(FileHandle* f, s64 offset, FileSeekOrigin origin)
{
    int whence = origin == FileSeekOrigin::Start ? SEEK_SET :
                 origin == FileSeekOrigin::Current ? SEEK_CUR : SEEK_END;
    return fseeko(stream(f), offset, whence) == 0;
}

void FileRewind(FileHandle* f) { rewind(stream(f)); }
u64 FilePosition(FileHandle* f) { return ftello(stream(f)); }
u64 FileLength(FileHandle* f)
{
    auto pos = ftello(stream(f));
    if (pos < 0 || fseeko(stream(f), 0, SEEK_END) != 0) return 0;
    auto len = ftello(stream(f));
    if (fseeko(stream(f), pos, SEEK_SET) != 0 || len < 0) return 0;
    return len;
}

u64 FileRead(void* data, u64 size, u64 count, FileHandle* f) { return fread(data, size, count, stream(f)); }
u64 FileWrite(const void* data, u64 size, u64 count, FileHandle* f) { return fwrite(data, size, count, stream(f)); }
bool FileFlush(FileHandle* f) { return fflush(stream(f)) == 0; }

u64 FileWriteFormatted(FileHandle* f, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int result = vfprintf(stream(f), fmt, args);
    va_end(args);
    return result < 0 ? 0 : result;
}

void Log(LogLevel level, const char* fmt, ...)
{
    if (level < Warn) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

struct Thread { std::thread worker; };
Thread* Thread_Create(std::function<void()> task) { return new Thread{std::thread(std::move(task))}; }
void Thread_Wait(Thread* t) { if (t->worker.joinable()) t->worker.join(); }
void Thread_Free(Thread* t) { Thread_Wait(t); delete t; }

struct Semaphore {
    std::mutex mutex;
    std::condition_variable ready;
    int available = 0;
};

Semaphore* Semaphore_Create() { return new Semaphore; }
void Semaphore_Free(Semaphore* s) { delete s; }
void Semaphore_Reset(Semaphore* s)
{
    std::lock_guard<std::mutex> lock(s->mutex);
    s->available = 0;
}
void Semaphore_Wait(Semaphore* s)
{
    std::unique_lock<std::mutex> lock(s->mutex);
    s->ready.wait(lock, [&] { return s->available > 0; });
    --s->available;
}
bool Semaphore_TryWait(Semaphore* s, int timeout)
{
    std::unique_lock<std::mutex> lock(s->mutex);
    if (!s->ready.wait_for(lock, std::chrono::milliseconds(timeout), [&] { return s->available > 0; }))
        return false;
    --s->available;
    return true;
}
void Semaphore_Post(Semaphore* s, int count)
{
    {
        std::lock_guard<std::mutex> lock(s->mutex);
        s->available += count;
    }
    s->ready.notify_all();
}

struct Mutex { std::mutex mutex; };
Mutex* Mutex_Create() { return new Mutex; }
void Mutex_Free(Mutex* m) { delete m; }
void Mutex_Lock(Mutex* m) { m->mutex.lock(); }
void Mutex_Unlock(Mutex* m) { m->mutex.unlock(); }
bool Mutex_TryLock(Mutex* m) { return m->mutex.try_lock(); }
void Sleep(u64 usecs) { std::this_thread::sleep_for(std::chrono::microseconds(usecs)); }
u64 GetUSCount()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
u64 GetMSCount() { return GetUSCount() / 1000; }

DynamicLibrary* DynamicLibrary_Load(const char* name)
{
    return reinterpret_cast<DynamicLibrary*>(dlopen(name, RTLD_NOW | RTLD_LOCAL));
}
void DynamicLibrary_Unload(DynamicLibrary* lib) { if (lib) dlclose(lib); }
void* DynamicLibrary_LoadFunction(DynamicLibrary* lib, const char* name)
{
    return lib ? dlsym(lib, name) : nullptr;
}

// The portable frontend runs an offline Nintendo DS with no optional peripherals.
void WriteGBASave(const u8*, u32, u32, u32, void*) {}
void WriteFirmware(const Firmware&, u32, u32, void*) {}
void WriteDateTime(int, int, int, int, int, int, void*) {}
void MP_Begin(void*) {}
void MP_End(void*) {}
int MP_SendPacket(u8*, int, u64, void*) { return 0; }
int MP_RecvPacket(u8*, u64*, void*) { return 0; }
int MP_SendCmd(u8*, int, u64, void*) { return 0; }
int MP_SendReply(u8*, int, u64, u16, void*) { return 0; }
int MP_SendAck(u8*, int, u64, void*) { return 0; }
int MP_RecvHostPacket(u8*, u64*, void*) { return 0; }
u16 MP_RecvReplies(u8*, u64, u16, void*) { return 0; }
int Net_SendPacket(u8*, int, void*) { return 0; }
int Net_RecvPacket(u8*, void*) { return 0; }
void Camera_Start(int, void*) {}
void Camera_Stop(int, void*) {}
void Camera_CaptureFrame(int, u32*, int, int, bool, void*) {}
void Mic_Start(void*) {}
void Mic_Stop(void*) {}
int Mic_ReadInput(s16*, int, void*) { return 0; }
AACDecoder* AAC_Init() { return nullptr; }
void AAC_DeInit(AACDecoder*) {}
bool AAC_Configure(AACDecoder*, int, int) { return false; }
bool AAC_DecodeFrame(AACDecoder*, const void*, int, void*, int) { return false; }
bool Addon_KeyDown(KeyType, void*) { return false; }
void Addon_RumbleStart(u32, void*) {}
void Addon_RumbleStop(void*) {}
float Addon_MotionQuery(MotionQueryType, void*) { return 0; }
} // namespace melonDS::Platform
