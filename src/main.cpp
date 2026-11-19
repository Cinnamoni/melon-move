// SPDX-License-Identifier: GPL-3.0-or-later
#include "Args.h"
#include "NDS.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <poll.h>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
using melonDS::u8;
using melonDS::u32;

namespace {
constexpr int Width = 256;
constexpr int ScreenHeight = 192;
constexpr int DisplayHeight = 2 * ScreenHeight;
constexpr auto FramePeriod = 16715us;
constexpr auto PresentPeriod = 125ms;
constexpr unsigned AllButtons = 0xfff;

enum Message : u32 {
    Refresh = 1, Load = 2, Buttons = 3, Touch = 4, Stop = 5, Pause = 6,
    RomList = 100, Frame = 101, Status = 102, Playing = 103, Paused = 104,
    Terminate = 0xffffffff
};
struct Header { u32 type; u32 size; };
static_assert(sizeof(Header) == 8);
volatile std::sig_atomic_t exiting = 0;
void requestExit(int) { exiting = 1; }

struct FileDescriptor {
    int fd;
    ~FileDescriptor() { if (fd >= 0) close(fd); }
};

class Link {
public:
    explicit Link(const char* path) : socket_{socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0)}
    {
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        if (std::strlen(path) >= sizeof(addr.sun_path))
            throw std::runtime_error("AppLoad socket path is too long.");
        std::strcpy(addr.sun_path, path);
        if (socket_.fd < 0) throw std::runtime_error("Cannot create AppLoad socket.");
        for (int attempt = 0; attempt < 20 && !exiting; ++attempt) {
            if (connect(socket_.fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0)
                return;
            if (errno != ENOENT && errno != ECONNREFUSED && errno != EINTR) break;
            std::this_thread::sleep_for(50ms);
        }
        throw std::runtime_error("Cannot connect to AppLoad.");
    }

    void sendMessage(u32 type, const std::string& content)
    {
        Header h{type, static_cast<u32>(content.size())};
        sendRecord(&h, sizeof(h));
        if (h.size) sendRecord(content.data(), h.size);
    }

    template<class Handler>
    bool pollMessages(int timeout, Handler handle)
    {
        for (int i = 0; i < 64; ++i) {
            pollfd pfd{socket_.fd, POLLIN, 0};
            int ready = poll(&pfd, 1, i == 0 ? timeout : 0);
            if (ready < 0 && errno == EINTR) return !exiting;
            if (ready < 0) return false;
            if (ready == 0) return true;
            Header h{};
            if (recv(socket_.fd, &h, sizeof(h), MSG_TRUNC) != sizeof(h) || h.size > 4096)
                return false;
            std::string payload(h.size, '\0');
            if (h.size && recv(socket_.fd, payload.data(), h.size, MSG_TRUNC) != h.size)
                return false;
            if (!handle(h.type, payload)) return false;
        }
        return true;
    }

private:
    void sendRecord(const void* data, std::size_t size)
    {
        if (send(socket_.fd, data, size, MSG_NOSIGNAL) != static_cast<ssize_t>(size))
            throw std::runtime_error("AppLoad connection closed.");
    }
    FileDescriptor socket_;
};

class FrameFiles {
public:
    FrameFiles()
    {
        char path[] = "/tmp/melon-move-XXXXXX";
        if (!mkdtemp(path)) throw std::runtime_error("Cannot create temporary display directory.");
        directory_ = path;
    }
    ~FrameFiles() { std::error_code error; fs::remove_all(directory_, error); }

    std::string write(const melonDS::GPU& gpu)
    {
        static constexpr auto header = [] {
            std::array<u8, 54> bmp{};
            auto field = [&](int offset, u32 value) {
                for (int i = 0; i < 4; ++i) bmp[offset + i] = (value >> (8 * i)) & 255;
            };
            bmp[0] = 'B';
            bmp[1] = 'M';
            field(2, 54 + Width * DisplayHeight * 4);
            field(10, 54);
            field(14, 40);
            field(18, Width);
            field(22, static_cast<u32>(-DisplayHeight)); // Top-down BMP.
            bmp[26] = 1;
            bmp[28] = 32;
            return bmp;
        }();
        const auto& screens = gpu.Framebuffer[gpu.FrontBuffer];
        if (!screens[0] || !screens[1]) throw std::runtime_error("The emulator display is unavailable.");

        auto target = directory_ / (index_++ % 2 ? "b.bmp" : "a.bmp");
        auto temporary = directory_ / "frame.tmp";
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(header.data()), header.size());
        for (const auto& screen : screens)
            output.write(reinterpret_cast<const char*>(screen.get()), Width * ScreenHeight * sizeof(u32));
        output.close();
        if (!output) throw std::runtime_error("Cannot write DS frame.");
        fs::rename(temporary, target);
        return "file://" + target.string();
    }
private:
    fs::path directory_;
    unsigned index_ = 0;
};

std::unique_ptr<u8[]> readFile(const fs::path& path, std::uintmax_t min,
                               std::uintmax_t max, u32& length)
{
    auto size = fs::file_size(path);
    if (size < min || size > max) throw std::runtime_error("Unexpected file size for " + path.filename().string());
    auto data = std::make_unique<u8[]>(size);
    std::ifstream stream(path, std::ios::binary);
    if (!stream.read(reinterpret_cast<char*>(data.get()), size))
        throw std::runtime_error("Cannot read " + path.filename().string());
    length = static_cast<u32>(size);
    return data;
}

fs::path dataRoot()
{
    if (const char* path = std::getenv("MELON_MOVE_DATA_DIR"); path && *path) return fs::absolute(path);
    return "/home/root/melon-move";
}

class Emulator {
public:
    Emulator(Link& link, fs::path directory) : link_(link), directory_(std::move(directory))
    {
        fs::create_directories(directory_ / "roms");
        fs::create_directories(directory_ / "saves");
        lock_.fd = open((directory_ / "instance.lock").c_str(), O_CREAT | O_CLOEXEC | O_RDWR, 0600);
        if (lock_.fd < 0 || flock(lock_.fd, LOCK_EX | LOCK_NB) < 0)
            throw std::runtime_error("Cannot lock the melonDS data directory.");
    }
    ~Emulator() { if (!flushSave()) std::cerr << "Could not write " << savePath_ << '\n'; }

    void run()
    {
        refreshRoms();
        while (!exiting) {
            auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(nextFrame_ - Clock::now() + 999us);
            int timeout = playing() && !paused_ ? std::clamp<int>(wait.count(), 0, 17) : 100;
            if (!link_.pollMessages(timeout, [this](u32 type, const std::string& data) {
                return type != Terminate && command(type, data);
            })) break;

            auto now = Clock::now();
            if (!pendingSave_.empty() && now >= nextSave_ && !flushSave()) {
                nextSave_ = now + 3s;
                link_.sendMessage(Status, "Save write failed. Retrying.");
            }
            if (playing() && !paused_ && now >= nextFrame_) tick(now);
        }
    }

    void queueSave(const u8* data, u32 length)
    {
        if (pendingSave_.empty()) nextSave_ = Clock::now() + 1s;
        pendingSave_.assign(data, data + length);
    }

private:
    bool playing() const { return nds_ && nds_->IsRunning(); }

    void refreshRoms()
    {
        roms_.clear();
        for (const auto& entry : fs::directory_iterator(directory_ / "roms")) {
            auto name = entry.path().filename().string();
            auto ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if (!entry.is_regular_file() || ext != ".nds" ||
                std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32; }))
                continue;
            roms_.push_back(std::move(name));
        }
        std::sort(roms_.begin(), roms_.end());
        // AppLoad uses two sequence packets per message. Keep lists below the kernel's packet limit.
        if (roms_.size() > 256) roms_.resize(256);
        std::ostringstream json;
        json << '[';
        for (std::size_t i = 0; i < roms_.size(); ++i) {
            if (i) json << ',';
            json << std::quoted(roms_[i]);
        }
        json << ']';
        link_.sendMessage(RomList, json.str());
    }

    bool flushSave()
    {
        if (pendingSave_.empty()) return true;
        auto temporary = savePath_;
        temporary += ".tmp";
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(pendingSave_.data()), pendingSave_.size());
        output.close();
        if (!output) return false;
        std::error_code error;
        fs::rename(temporary, savePath_, error);
        if (error) return false;
        pendingSave_.clear();
        return true;
    }

    void load(const std::string& name)
    {
        if (std::find(roms_.begin(), roms_.end(), name) == roms_.end())
            throw std::runtime_error("Choose a ROM from the library.");
        if (!flushSave()) throw std::runtime_error("Could not save the current game.");
        u32 length = 0;
        auto rom = readFile(directory_ / "roms" / name, sizeof(melonDS::NDSHeader), 512ull * 1024 * 1024, length);
        melonDS::NDSHeader header{};
        std::memcpy(&header, rom.get(), sizeof(header));
        auto codeFits = [length](u32 offset, u32 size) {
            return size > 0 && offset >= 0x200 && ((offset | size) & 3) == 0 &&
                   offset < length && size <= length - offset;
        };
        const bool secureAreaFits = header.ARM9ROMOffset < 0x4000 ||
            header.ARM9ROMOffset >= 0x8000 || header.ARM9ROMOffset + 0x800 <= length;
        if (!codeFits(header.ARM9ROMOffset, header.ARM9Size) ||
            !codeFits(header.ARM7ROMOffset, header.ARM7Size) || !secureAreaFits)
            throw std::runtime_error("Invalid Nintendo DS ROM header.");
        if (header.IsDSiWare()) throw std::runtime_error("This app runs Nintendo DS cartridge games.");
        auto save = directory_ / "saves" / (name + ".sav");
        melonDS::NDSCart::NDSCartArgs cartArgs;
        if (fs::exists(save))
            cartArgs.SRAM = readFile(save, 1, 16 * 1024 * 1024, cartArgs.SRAMLength);
        auto cart = melonDS::NDSCart::ParseROM(std::move(rom), length, this, std::move(cartArgs));
        if (!cart) throw std::runtime_error("melonDS could not open this game.");

        auto next = std::make_unique<melonDS::NDS>(melonDS::NDSArgs{}, this);
        next->SetNDSCart(std::move(cart));
        next->Reset();
        next->SetupDirectBoot(name);
        next->Start();
        nds_ = std::move(next);
        savePath_ = std::move(save);
        buttons_ = latched_ = 0;
        touching_ = touchLatch_ = paused_ = false;
        nextFrame_ = nextPresent_ = Clock::now();
        link_.sendMessage(Playing, name);
    }

    bool command(u32 type, const std::string& data)
    {
        try {
            switch (type) {
            case Refresh:
                refreshRoms();
                break;
            case Load:
                load(data);
                break;
            case Buttons: {
                unsigned value;
                if (std::istringstream(data) >> value) {
                    latched_ |= (value & AllButtons) & ~buttons_;
                    buttons_ = value & AllButtons;
                }
                break;
            }
            case Touch: {
                int x, y;
                bool down;
                if (std::istringstream(data) >> down >> x >> y) {
                    touchLatch_ |= down && !touching_;
                    touching_ = down;
                    if (down) {
                        x_ = std::clamp(x, 0, Width - 1);
                        y_ = std::clamp(y, 0, ScreenHeight - 1);
                    }
                }
                break;
            }
            case Stop:
                if (!flushSave()) throw std::runtime_error("Could not save. Retry before closing.");
                nds_.reset();
                link_.sendMessage(Playing, "");
                break;
            case Pause:
                if (playing()) {
                    paused_ = !paused_;
                    buttons_ = latched_ = 0;
                    touching_ = touchLatch_ = false;
                    nextFrame_ = Clock::now();
                    link_.sendMessage(Paused, paused_ ? "1" : "0");
                }
                break;
            }
        } catch (const std::exception& error) {
            link_.sendMessage(Status, error.what());
        }
        return true;
    }

    void tick(Clock::time_point now)
    {
        nds_->SetKeyMask(AllButtons & ~(buttons_ | latched_));
        latched_ = 0;
        if (touching_ || touchLatch_) nds_->TouchScreen(x_, y_);
        else nds_->ReleaseScreen();
        touchLatch_ = false;
        nds_->RunFrame();
        nds_->SPU.DrainOutput();
        if (!playing()) {
            flushSave();
            nds_.reset();
            link_.sendMessage(Playing, "");
            link_.sendMessage(Status, "The emulated console stopped.");
            return;
        }
        nextFrame_ += FramePeriod;
        if (nextFrame_ + 2 * FramePeriod < now) nextFrame_ = now;
        if (now >= nextPresent_) {
            link_.sendMessage(Frame, frames_.write(nds_->GPU));
            nextPresent_ = now + PresentPeriod;
        }
    }

    Link& link_;
    fs::path directory_, savePath_;
    FileDescriptor lock_{-1};
    FrameFiles frames_;
    std::vector<std::string> roms_;
    std::vector<u8> pendingSave_;
    std::unique_ptr<melonDS::NDS> nds_;
    Clock::time_point nextFrame_{}, nextPresent_{}, nextSave_{};
    unsigned buttons_ = 0, latched_ = 0;
    int x_ = 0, y_ = 0;
    bool touching_ = false, touchLatch_ = false, paused_ = false;
};
} // namespace

namespace melonDS::Platform {
void SignalStop(StopReason, void*) {}
void WriteNDSSave(const u8* data, u32 length, u32, u32, void* user)
{
    if (user && length) static_cast<Emulator*>(user)->queueSave(data, length);
}
} // namespace melonDS::Platform

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "Usage: entry APPLOAD_SOCKET\n";
        return 2;
    }
    std::signal(SIGTERM, requestExit);
    std::signal(SIGINT, requestExit);
    try {
        Link link(argv[1]);
        try {
            Emulator emulator(link, dataRoot());
            emulator.run();
        } catch (const std::exception& error) {
            link.sendMessage(Status, error.what());
            throw;
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
