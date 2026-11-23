/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "remote_frames.h"

#include <png.h>

#include <array>
#include <chrono>
#include <cstring>
#include <utility>

#if defined(__linux__)
#include <cerrno>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace platform::remote {
namespace {

constexpr int32_t kMaxSide = 4096;

// The 8-bit value of every value a channel of `length` bits can hold,
// spreading the bits the way scripts/device-agent.py does (5 bits abcde
// become abcdeabc), so both make the same picture.
std::array<uint8_t, 256> channel_table(uint32_t length) {
    std::array<uint8_t, 256> table{};
    if (length == 0 || length > 8) {
        return table;
    }
    for (uint32_t value = 0; value < (1U << length); ++value) {
        uint32_t out = 0;
        uint32_t bits = 0;
        while (bits < 8) {
            out = (out << length) | value;
            bits += length;
        }
        table[value] = static_cast<uint8_t>((out >> (bits - 8)) & 0xFF);
    }
    return table;
}

struct ChannelReader {
    uint32_t offset;
    uint32_t length;
    uint32_t mask;
    std::array<uint8_t, 256> table;

    explicit ChannelReader(const PixelChannel& channel)
        : offset(channel.offset),
          length(channel.length),
          mask(channel.length >= 32 ? 0xFFFFFFFFU : (1U << channel.length) - 1U),
          table(channel_table(channel.length)) {}

    uint8_t read(uint32_t pixel) const {
        const uint32_t value = (pixel >> offset) & mask;
        if (length > 8) {
            return static_cast<uint8_t>(value >> (length - 8));
        }
        return table[value];
    }
};

bool valid_channel(const PixelChannel& channel, int32_t bits_per_pixel) {
    return channel.length > 0 && channel.length <= 16 &&
           channel.offset + channel.length <= static_cast<uint32_t>(bits_per_pixel);
}

bool same_layout(const PixelLayout& a, const PixelLayout& b) {
    auto same_channel = [](const PixelChannel& x, const PixelChannel& y) {
        return x.offset == y.offset && x.length == y.length;
    };
    return a.width == b.width && a.height == b.height && a.stride == b.stride &&
           a.bits_per_pixel == b.bits_per_pixel && same_channel(a.red, b.red) &&
           same_channel(a.green, b.green) && same_channel(a.blue, b.blue);
}

void append_png_data(png_structp png, png_bytep data, png_size_t length) {
    auto* out = static_cast<std::vector<uint8_t>*>(png_get_io_ptr(png));
    out->insert(out->end(), data, data + length);
}

void flush_png_data(png_structp) {}

#if defined(__linux__)
class FramebufferSource final : public FrameSource {
public:
    explicit FramebufferSource(std::string path) : path_(std::move(path)) {}
    ~FramebufferSource() override {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    bool grab(std::vector<uint8_t>& bytes, PixelLayout& layout) override {
        if (fd_ < 0) {
            fd_ = ::open(path_.c_str(), O_RDONLY | O_CLOEXEC);
            if (fd_ < 0) {
                return false;
            }
        }
        fb_var_screeninfo var{};
        fb_fix_screeninfo fix{};
        if (::ioctl(fd_, FBIOGET_VSCREENINFO, &var) != 0 || ::ioctl(fd_, FBIOGET_FSCREENINFO, &fix) != 0) {
            return false;
        }
        if (var.red.msb_right || var.green.msb_right || var.blue.msb_right) {
            return false;
        }
        layout.width = static_cast<int32_t>(var.xres);
        layout.height = static_cast<int32_t>(var.yres);
        layout.bits_per_pixel = static_cast<int32_t>(var.bits_per_pixel);
        layout.stride = fix.line_length > 0 ? static_cast<int32_t>(fix.line_length)
                                            : layout.width * layout.bits_per_pixel / 8;
        layout.red = {var.red.offset, var.red.length};
        layout.green = {var.green.offset, var.green.length};
        layout.blue = {var.blue.offset, var.blue.length};
        if (var.xres > static_cast<uint32_t>(kMaxSide) || var.yres > static_cast<uint32_t>(kMaxSide) ||
            var.xoffset > static_cast<uint32_t>(kMaxSide) || var.yoffset > static_cast<uint32_t>(kMaxSide)) {
            return false;
        }

        const std::size_t size = static_cast<std::size_t>(layout.stride) * static_cast<std::size_t>(layout.height);
        if (!layout_valid(layout, size)) {
            return false;
        }
        const auto offset = static_cast<off_t>(static_cast<std::size_t>(var.yoffset) * static_cast<std::size_t>(layout.stride) +
                                               static_cast<std::size_t>(var.xoffset) *
                                                   static_cast<std::size_t>(layout.bits_per_pixel / 8));
        bytes.resize(size);
        std::size_t done = 0;
        while (done < size) {
            const ssize_t got = ::pread(fd_, bytes.data() + done, size - done, offset + static_cast<off_t>(done));
            if (got < 0 && errno == EINTR) {
                continue;
            }
            if (got <= 0) {
                return false;
            }
            done += static_cast<std::size_t>(got);
        }
        return true;
    }

private:
    std::string path_;
    int fd_{-1};
};
#endif

} // namespace

bool layout_valid(const PixelLayout& layout, std::size_t size) {
    if (layout.width <= 0 || layout.height <= 0 || layout.width > kMaxSide || layout.height > kMaxSide) {
        return false;
    }
    if (layout.bits_per_pixel != 16 && layout.bits_per_pixel != 32) {
        return false;
    }
    if (layout.stride < layout.width * (layout.bits_per_pixel / 8)) {
        return false;
    }
    if (!valid_channel(layout.red, layout.bits_per_pixel) || !valid_channel(layout.green, layout.bits_per_pixel) ||
        !valid_channel(layout.blue, layout.bits_per_pixel)) {
        return false;
    }
    return size >= static_cast<std::size_t>(layout.stride) * static_cast<std::size_t>(layout.height);
}

bool encode_png(const uint8_t* pixels, const PixelLayout& layout, std::vector<uint8_t>& png_out) {
    png_out.clear();
    if (!pixels || !layout_valid(layout, static_cast<std::size_t>(layout.stride) * static_cast<std::size_t>(layout.height))) {
        return false;
    }

    // Everything with a destructor is made before setjmp(): libpng jumps
    // back here on an error.
    const ChannelReader red(layout.red);
    const ChannelReader green(layout.green);
    const ChannelReader blue(layout.blue);
    std::vector<uint8_t> row(static_cast<std::size_t>(layout.width) * 3U);
    png_out.reserve(32 * 1024);

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png ? png_create_info_struct(png) : nullptr;
    if (!png || !info) {
        if (png) {
            png_destroy_write_struct(&png, nullptr);
        }
        return false;
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        png_out.clear();
        return false;
    }

    png_set_write_fn(png, &png_out, append_png_data, flush_png_data);
    // Fast: the lowest compression level and one cheap filter, which suits
    // the flat colours of the interface.
    png_set_compression_level(png, 1);
    png_set_filter(png, PNG_FILTER_TYPE_BASE, PNG_FILTER_SUB);
    png_set_IHDR(png,
                 info,
                 static_cast<png_uint_32>(layout.width),
                 static_cast<png_uint_32>(layout.height),
                 8,
                 PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    const std::size_t pixel_bytes = static_cast<std::size_t>(layout.bits_per_pixel / 8);
    for (int32_t y = 0; y < layout.height; ++y) {
        const uint8_t* line = pixels + static_cast<std::size_t>(y) * static_cast<std::size_t>(layout.stride);
        for (int32_t x = 0; x < layout.width; ++x) {
            uint32_t pixel = 0;
            if (pixel_bytes == 2) {
                uint16_t word = 0;
                std::memcpy(&word, line + static_cast<std::size_t>(x) * 2U, sizeof(word));
                pixel = word;
            } else {
                std::memcpy(&pixel, line + static_cast<std::size_t>(x) * 4U, sizeof(pixel));
            }
            uint8_t* out = row.data() + static_cast<std::size_t>(x) * 3U;
            out[0] = red.read(pixel);
            out[1] = green.read(pixel);
            out[2] = blue.read(pixel);
        }
        png_write_row(png, row.data());
    }
    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    return true;
}

#if defined(__linux__)
std::unique_ptr<FrameSource> make_framebuffer_source(std::string path) {
    return std::make_unique<FramebufferSource>(std::move(path));
}
#endif

bool HandOffSource::grab(std::vector<uint8_t>& bytes, PixelLayout& layout) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (cancelled_) {
        return false;
    }
    const uint64_t before = deliveries_;
    wanted_ = true;
    delivered_cv_.wait_for(lock, std::chrono::milliseconds(kWaitMs), [&] {
        return cancelled_ || deliveries_ != before;
    });
    wanted_ = false;
    if (cancelled_ || deliveries_ == 0) {
        return false;
    }
    bytes = bytes_;
    layout = layout_;
    return true;
}

void HandOffSource::cancel() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cancelled_ = true;
    }
    delivered_cv_.notify_all();
}

bool HandOffSource::wanted() {
    std::lock_guard<std::mutex> lock(mutex_);
    return wanted_ && !cancelled_;
}

void HandOffSource::deliver(const uint8_t* pixels, const PixelLayout& layout) {
    const std::size_t size = static_cast<std::size_t>(layout.stride) * static_cast<std::size_t>(layout.height);
    if (!pixels || !layout_valid(layout, size)) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        bytes_.assign(pixels, pixels + size);
        layout_ = layout;
        ++deliveries_;
        wanted_ = false;
    }
    delivered_cv_.notify_all();
}

bool FrameCache::refresh(int64_t now_ms) {
    if (frame_ != 0 && now_ms - grabbed_ms_ < kMinGapMs) {
        return true;
    }
    PixelLayout layout;
    if (!source_.grab(incoming_, layout) || !layout_valid(layout, incoming_.size())) {
        // The last picture, when there is one, stays the current one.
        return frame_ != 0;
    }
    grabbed_ms_ = now_ms;
    const std::size_t size = static_cast<std::size_t>(layout.stride) * static_cast<std::size_t>(layout.height);
    const bool same = frame_ != 0 && same_layout(layout, layout_) && current_.size() >= size &&
                      std::memcmp(current_.data(), incoming_.data(), size) == 0;
    if (!same) {
        current_.swap(incoming_);
        layout_ = layout;
        ++frame_;
        png_ready_ = false;
    }
    return true;
}

const std::vector<uint8_t>* FrameCache::png() {
    if (frame_ == 0) {
        return nullptr;
    }
    if (!png_ready_) {
        if (!encode_png(current_.data(), layout_, png_)) {
            return nullptr;
        }
        png_ready_ = true;
    }
    return &png_;
}

} // namespace platform::remote
