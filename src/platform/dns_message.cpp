/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "dns_message.h"

#include <arpa/inet.h>

#include <netinet/in.h>

namespace platform::dns {
namespace {

constexpr std::size_t kHeaderSize = 12;
constexpr std::size_t kMaxLabel = 63;
constexpr std::size_t kMaxPointers = 32;
// In wire form a name is at most 255 bytes, with the length bytes.
constexpr std::size_t kMaxNameWire = 255;

uint16_t read_u16(const uint8_t* data, std::size_t offset) {
    return static_cast<uint16_t>((data[offset] << 8) | data[offset + 1]);
}

uint32_t read_u32(const uint8_t* data, std::size_t offset) {
    return (static_cast<uint32_t>(data[offset]) << 24) | (static_cast<uint32_t>(data[offset + 1]) << 16) |
           (static_cast<uint32_t>(data[offset + 2]) << 8) | static_cast<uint32_t>(data[offset + 3]);
}

void put_u16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

bool label_character(char character) {
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9') || character == '-';
}

char lower(char character) {
    return character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character;
}

} // namespace

bool build_query(uint16_t id, std::string_view name, uint16_t type, std::vector<uint8_t>& out) {
    out.clear();
    if (!name.empty() && name.back() == '.') {
        name.remove_suffix(1);
    }
    if (name.empty() || name.size() > kMaxNameText) {
        return false;
    }

    put_u16(out, id);
    put_u16(out, 0x0100);  // a standard query, recursion desired
    put_u16(out, 1);       // one question
    put_u16(out, 0);
    put_u16(out, 0);
    put_u16(out, 0);

    std::size_t start = 0;
    while (start <= name.size()) {
        std::size_t end = name.find('.', start);
        if (end == std::string_view::npos) {
            end = name.size();
        }
        const std::size_t length = end - start;
        if (length == 0 || length > kMaxLabel) {
            out.clear();
            return false;
        }
        out.push_back(static_cast<uint8_t>(length));
        for (std::size_t index = start; index < end; ++index) {
            if (!label_character(name[index])) {
                out.clear();
                return false;
            }
            out.push_back(static_cast<uint8_t>(name[index]));
        }
        start = end + 1;
    }
    out.push_back(0);
    put_u16(out, type);
    put_u16(out, kClassIn);
    return true;
}

bool read_name(const uint8_t* data, std::size_t size, std::size_t& offset, std::string& out) {
    out.clear();
    std::size_t position = offset;
    std::size_t end_of_name = 0;   // where the name ends at `offset`, once a pointer was followed
    bool jumped = false;
    std::size_t pointers = 0;
    std::size_t wire_length = 1;   // the final zero byte
    // Where the label just read started: a pointer must point before it, so
    // every jump goes backwards and a loop is impossible.
    std::size_t limit = offset;

    while (true) {
        if (position >= size) {
            return false;
        }
        const uint8_t length = data[position];
        if ((length & 0xC0) == 0xC0) {
            if (position + 1 >= size || ++pointers > kMaxPointers) {
                return false;
            }
            const std::size_t target = static_cast<std::size_t>(((length & 0x3F) << 8) | data[position + 1]);
            if (target >= limit || target >= position) {
                return false;
            }
            if (!jumped) {
                end_of_name = position + 2;
                jumped = true;
            }
            limit = target;
            position = target;
            continue;
        }
        if ((length & 0xC0) != 0) {
            return false;  // the reserved label kinds
        }
        if (length == 0) {
            ++position;
            break;
        }
        wire_length += 1u + length;
        if (wire_length > kMaxNameWire || position + 1 + length > size) {
            return false;
        }
        if (!out.empty()) {
            out += '.';
        }
        for (std::size_t index = 0; index < length; ++index) {
            out += lower(static_cast<char>(data[position + 1 + index]));
        }
        position += 1u + length;
    }
    offset = jumped ? end_of_name : position;
    return true;
}

bool parse_message(const uint8_t* data, std::size_t size, Message& out) {
    out = Message{};
    if (!data || size < kHeaderSize) {
        return false;
    }
    out.id = read_u16(data, 0);
    const uint16_t flags = read_u16(data, 2);
    out.response = (flags & 0x8000) != 0;
    out.truncated = (flags & 0x0200) != 0;
    out.rcode = static_cast<uint8_t>(flags & 0x000F);
    const uint16_t questions = read_u16(data, 4);
    const uint16_t answers = read_u16(data, 6);

    std::size_t offset = kHeaderSize;
    for (uint16_t index = 0; index < questions; ++index) {
        std::string name;
        if (!read_name(data, size, offset, name) || offset + 4 > size) {
            return false;
        }
        if (index == 0) {
            out.question_name = name;
            out.question_type = read_u16(data, offset);
            out.question_class = read_u16(data, offset + 2);
        }
        offset += 4;
    }

    // Every record takes at least 11 bytes, so a count larger than what is
    // left is refused before anything is reserved.
    if (answers > (size - offset) / 11) {
        return false;
    }
    out.answers.reserve(answers);
    for (uint16_t index = 0; index < answers; ++index) {
        Record record;
        if (!read_name(data, size, offset, record.name) || offset + 10 > size) {
            return false;
        }
        record.type = read_u16(data, offset);
        record.klass = read_u16(data, offset + 2);
        record.ttl = read_u32(data, offset + 4);
        record.data_length = read_u16(data, offset + 8);
        offset += 10;
        if (record.data_length > size - offset) {
            return false;
        }
        record.data_offset = offset;
        offset += record.data_length;
        out.answers.push_back(std::move(record));
    }
    return true;
}

std::string address_text(const uint8_t* data, std::size_t size, const Record& record) {
    if (record.klass != kClassIn || record.data_offset > size || record.data_length > size - record.data_offset) {
        return {};
    }
    char text[INET6_ADDRSTRLEN] = {};
    if (record.type == kTypeA && record.data_length == 4) {
        if (inet_ntop(AF_INET, data + record.data_offset, text, sizeof(text))) {
            return text;
        }
    } else if (record.type == kTypeAaaa && record.data_length == 16) {
        if (inet_ntop(AF_INET6, data + record.data_offset, text, sizeof(text))) {
            return text;
        }
    }
    return {};
}

std::string pointer_name(const uint8_t* data, std::size_t size, const Record& record) {
    if (record.type != kTypePtr || record.klass != kClassIn || record.data_offset > size ||
        record.data_length > size - record.data_offset) {
        return {};
    }
    std::size_t offset = record.data_offset;
    std::string name;
    if (!read_name(data, size, offset, name) || offset > record.data_offset + record.data_length) {
        return {};
    }
    for (const char character : name) {
        if (character < '!' || character > '~') {
            return {};
        }
    }
    return name;
}

std::string reverse_name(const std::string& ipv4) {
    in_addr address{};
    if (inet_pton(AF_INET, ipv4.c_str(), &address) != 1) {
        return {};
    }
    const auto* bytes = reinterpret_cast<const uint8_t*>(&address.s_addr);
    std::string name;
    for (int index = 3; index >= 0; --index) {
        name += std::to_string(bytes[index]);
        name += '.';
    }
    return name + "in-addr.arpa";
}

} // namespace platform::dns
