/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace platform {

// The device's backlight file: a whole number from 0 (off) to 100. The
// `video` group may write it, so no root is needed.
constexpr const char* kBacklightFile = "/sys/class/backlight/backlight/brightness";

// The screen's brightness, as a number the backlight understands (0 is
// off). It only reads and writes; when and how far to dim is decided by
// app::PowerController. Used on the UI thread only.
class Backlight {
public:
    virtual ~Backlight() = default;

    // Reads the brightness now. Returns false when it cannot be read.
    virtual bool read(int32_t& level) = 0;
    // Sets the brightness. Returns false when it cannot be written.
    virtual bool write(int32_t level) = 0;
};

// The backlight behind a file such as kBacklightFile (or a plain file that
// stands in for it in tests). The file is opened for each read and write
// and never kept open.
std::unique_ptr<Backlight> make_file_backlight(std::string path);

// A backlight that is only a number: it starts at `level`, and every write
// stores the new number and hands it to `on_write` (which may be empty).
// The simulator uses it to darken its window, and snapshots to report the
// writes without touching the device.
std::unique_ptr<Backlight> make_stand_in_backlight(int32_t level,
                                                   std::function<void(int32_t)> on_write);

} // namespace platform
