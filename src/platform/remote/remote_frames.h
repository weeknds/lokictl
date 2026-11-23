/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace platform::remote {

// Where one colour sits in a pixel: `length` bits from bit `offset` (as in
// the kernel's fb_bitfield).
struct PixelChannel {
    uint32_t offset{0};
    uint32_t length{0};
};

// How the pixels of a picture are stored: lines of `stride` bytes, each
// pixel a native-endian 16- or 32-bit word.
struct PixelLayout {
    int32_t width{0};
    int32_t height{0};
    int32_t stride{0};
    int32_t bits_per_pixel{0};
    PixelChannel red;
    PixelChannel green;
    PixelChannel blue;
};

// True when `layout` is one this module reads and `size` bytes hold all of
// its lines.
bool layout_valid(const PixelLayout& layout, std::size_t size);

// Encodes the picture as an 8-bit RGB PNG in memory, with fast compression.
// Returns false (with `png` empty) when the layout is not valid or libpng
// failed.
bool encode_png(const uint8_t* pixels, const PixelLayout& layout, std::vector<uint8_t>& png);

// Takes pictures of the screen for the server. grab() runs on the server's
// thread only; cancel() may come from any thread and ends a grab that waits.
class FrameSource {
public:
    virtual ~FrameSource() = default;
    virtual bool grab(std::vector<uint8_t>& bytes, PixelLayout& layout) = 0;
    virtual void cancel() {}
};

#if defined(__linux__)
// The device: reads the framebuffer at `path` (opened on the first grab,
// never written). Size, panning offset and colour layout come from
// FBIOGET_VSCREENINFO, the line length from FBIOGET_FSCREENINFO, so the
// overlays drawn on LVGL's top layer are in the picture too.
std::unique_ptr<FrameSource> make_framebuffer_source(std::string path);
#endif

// The desktop simulator: the UI thread hands pictures over. grab() asks for
// one and waits up to kWaitMs for it; when none comes it returns the last
// one handed over (false before the first).
class HandOffSource : public FrameSource {
public:
    static constexpr int kWaitMs = 300;

    bool grab(std::vector<uint8_t>& bytes, PixelLayout& layout) override;
    void cancel() override;

    // For the UI thread: true while the server waits for a picture.
    bool wanted();
    // For the UI thread: the picture asked for. It is copied.
    void deliver(const uint8_t* pixels, const PixelLayout& layout);

private:
    std::mutex mutex_;
    std::condition_variable delivered_cv_;
    bool wanted_{false};
    bool cancelled_{false};
    uint64_t deliveries_{0};
    std::vector<uint8_t> bytes_;
    PixelLayout layout_;
};

// The latest picture and its PNG, kept by the server's thread. A picture is
// taken only when a browser asks and the last one is at least kMinGapMs
// old; it gets the next frame number only when its pixels changed, and is
// encoded only when a browser needs the PNG.
class FrameCache {
public:
    static constexpr int64_t kMinGapMs = 200;

    explicit FrameCache(FrameSource& source) : source_(source) {}

    // Takes a new picture when the last one is old enough. Returns false
    // while there is no picture at all.
    bool refresh(int64_t now_ms);
    // The current frame number; 0 before the first picture.
    uint64_t frame() const { return frame_; }
    // The PNG of the current frame, or nullptr when it could not be made.
    const std::vector<uint8_t>* png();

private:
    FrameSource& source_;
    std::vector<uint8_t> current_;
    std::vector<uint8_t> incoming_;
    std::vector<uint8_t> png_;
    PixelLayout layout_;
    uint64_t frame_{0};
    int64_t grabbed_ms_{0};
    bool png_ready_{false};
};

} // namespace platform::remote
