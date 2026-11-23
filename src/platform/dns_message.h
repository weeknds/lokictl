/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// DNS messages written and read by hand (RFC 1035), with no sockets: the
// tools build questions with build_query() and read the answers with
// parse_message(). Everything here checks every length against the end of
// the message, so a broken or hostile reply can never make it read past the
// buffer; names follow compression pointers only backwards, with a limit.
namespace platform::dns {

constexpr uint16_t kTypeA     = 1;
constexpr uint16_t kTypeCname = 5;
constexpr uint16_t kTypePtr   = 12;
constexpr uint16_t kTypeAaaa  = 28;
constexpr uint16_t kClassIn   = 1;

constexpr uint8_t kRcodeNoError  = 0;
constexpr uint8_t kRcodeServFail = 2;
constexpr uint8_t kRcodeNxDomain = 3;

// The longest name in text form, without the final dot.
constexpr std::size_t kMaxNameText = 253;
// The longest UDP message read (without EDNS a server sends at most 512).
constexpr std::size_t kMaxMessage = 1232;

// One record of the answer section.
struct Record {
    std::string name;          // owner name, lowercase, without the final dot
    uint16_t type{0};
    uint16_t klass{0};
    uint32_t ttl{0};
    std::size_t data_offset{0}; // where the record's data starts in the message
    uint16_t data_length{0};
};

struct Message {
    uint16_t id{0};
    bool response{false};       // QR bit
    bool truncated{false};      // TC bit
    uint8_t rcode{0};
    std::string question_name;  // the first question, lowercase, without the final dot
    uint16_t question_type{0};
    uint16_t question_class{0};
    std::vector<Record> answers;
};

// Writes a standard query for `name` (text form, labels of 1-63 characters
// of letters, digits and '-', at most kMaxNameText in all; one final dot is
// allowed) and `type`, with recursion desired, into `out`. Returns false,
// with `out` emptied, for a name it refuses.
bool build_query(uint16_t id, std::string_view name, uint16_t type, std::vector<uint8_t>& out);

// Reads a whole message: the header, the first question (others are
// skipped) and the answer section; the authority and additional sections
// are not read. Returns false for anything that does not fit its own
// lengths.
bool parse_message(const uint8_t* data, std::size_t size, Message& out);

// Reads a name at `offset` of the message, following compression pointers
// (each must point before the place it was read from, at most 32 of them),
// in lowercase without the final dot ("" for the root). On success `offset`
// is moved past the name as it stands at `offset`.
bool read_name(const uint8_t* data, std::size_t size, std::size_t& offset, std::string& out);

// The address in an A (4 bytes) or AAAA (16 bytes) record of `message`, in
// text form; empty for any other record or length.
std::string address_text(const uint8_t* data, std::size_t size, const Record& record);

// The name in a PTR record of `message` (lowercase, without the final
// dot). Empty for any other record, for a name that does not end inside the
// record's data, and for a name with anything but printable ASCII (a
// reverse name is shown as it is, so nothing else gets through).
std::string pointer_name(const uint8_t* data, std::size_t size, const Record& record);

// The name to ask a PTR question for to find the name of the IPv4 address
// `ipv4` ("192.0.2.1" gives "1.2.0.192.in-addr.arpa"); empty when `ipv4`
// is not an IPv4 address.
std::string reverse_name(const std::string& ipv4);

} // namespace platform::dns
