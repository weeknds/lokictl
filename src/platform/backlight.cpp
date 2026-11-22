/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "backlight.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace platform {
namespace {

class FileBacklight final : public Backlight {
public:
    explicit FileBacklight(std::string path)
        : path_(std::move(path)) {}

    bool read(int32_t& level) override {
        std::FILE* file = std::fopen(path_.c_str(), "r");
        if (!file) {
            return false;
        }
        char text[32]{};
        const bool got_text = std::fgets(text, sizeof(text), file) != nullptr;
        std::fclose(file);
        if (!got_text) {
            return false;
        }
        char* end = nullptr;
        errno = 0;
        const long value = std::strtol(text, &end, 10);
        if (end == text || errno != 0 || value < 0 || value > 100000) {
            return false;
        }
        level = static_cast<int32_t>(value);
        return true;
    }

    bool write(int32_t level) override {
        std::FILE* file = std::fopen(path_.c_str(), "w");
        if (!file) {
            return false;
        }
        const bool written = std::fprintf(file, "%d\n", static_cast<int>(level)) > 0;
        // A sysfs file reports a refused value when it is closed.
        const bool closed = std::fclose(file) == 0;
        return written && closed;
    }

private:
    std::string path_;
};

class StandInBacklight final : public Backlight {
public:
    StandInBacklight(int32_t level, std::function<void(int32_t)> on_write)
        : level_(level),
          on_write_(std::move(on_write)) {}

    bool read(int32_t& level) override {
        level = level_;
        return true;
    }

    bool write(int32_t level) override {
        level_ = level;
        if (on_write_) {
            on_write_(level);
        }
        return true;
    }

private:
    int32_t level_;
    std::function<void(int32_t)> on_write_;
};

} // namespace

std::unique_ptr<Backlight> make_file_backlight(std::string path) {
    return std::make_unique<FileBacklight>(std::move(path));
}

std::unique_ptr<Backlight> make_stand_in_backlight(int32_t level,
                                                   std::function<void(int32_t)> on_write) {
    return std::make_unique<StandInBacklight>(level, std::move(on_write));
}

} // namespace platform
