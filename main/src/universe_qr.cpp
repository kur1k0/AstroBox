#include "orbit/universe_qr.h"

#include <algorithm>
#include <cstdio>
#include <limits>

namespace orbit {

namespace {

constexpr char kMultipartPrefix[] = "ORBITM1:";
constexpr std::size_t kMaximumArchiveLength = 8192;
constexpr std::size_t kMaximumAcceptedPayload = 1024;

std::uint32_t crc32(const char *data, std::size_t size)
{
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= static_cast<unsigned char>(data[i]);
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1U) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
    }
    return crc ^ 0xFFFFFFFFU;
}

std::uint32_t crc32(const std::string &text)
{
    return crc32(text.data(), text.size());
}

bool parse_decimal(const std::string &field, std::size_t maximum,
                   std::size_t &value)
{
    if (field.empty()) return false;
    std::size_t parsed = 0;
    for (char character : field) {
        if (character < '0' || character > '9') return false;
        const std::size_t digit = static_cast<std::size_t>(character - '0');
        if (parsed > (maximum - digit) / 10U) return false;
        parsed = parsed * 10U + digit;
    }
    value = parsed;
    return true;
}

bool parse_hex32(const std::string &field, std::uint32_t &value)
{
    if (field.size() != 8U) return false;
    std::uint32_t parsed = 0;
    for (char character : field) {
        unsigned digit = 0;
        if (character >= '0' && character <= '9')
            digit = static_cast<unsigned>(character - '0');
        else if (character >= 'A' && character <= 'F')
            digit = static_cast<unsigned>(character - 'A' + 10);
        else if (character >= 'a' && character <= 'f')
            digit = static_cast<unsigned>(character - 'a' + 10);
        else
            return false;
        parsed = (parsed << 4U) | digit;
    }
    value = parsed;
    return true;
}

bool read_field(const std::string &text, std::size_t &cursor,
                std::string &field)
{
    const std::size_t delimiter = text.find(':', cursor);
    if (delimiter == std::string::npos) return false;
    field = text.substr(cursor, delimiter - cursor);
    cursor = delimiter + 1U;
    return true;
}

std::size_t first_missing(const std::vector<std::string> &parts)
{
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (parts[i].empty()) return i + 1U;
    }
    return 0;
}

} 

UniverseQrPartsResult make_universe_qr_parts(const std::string &archive)
{
    if (archive.empty() || archive.size() > kMaximumArchiveLength)
        return {{}, "INVALID ARCHIVE SIZE"};
    if (archive.size() <= kUniverseQrChunkPayload)
        return {{archive}, {}};

    const std::size_t count =
        (archive.size() + kUniverseQrChunkPayload - 1U) /
        kUniverseQrChunkPayload;
    if (count > kMaximumUniverseQrParts)
        return {{}, "TOO MANY QR PARTS"};

    const std::uint32_t set_crc = crc32(archive);
    std::vector<std::string> parts;
    parts.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const std::size_t offset = index * kUniverseQrChunkPayload;
        const std::string payload = archive.substr(offset, kUniverseQrChunkPayload);
        char header[96];
        std::snprintf(header, sizeof(header), "ORBITM1:%08X:%zu:%zu:%zu:%08X:",
                      set_crc, archive.size(), index + 1U, count, crc32(payload));
        parts.emplace_back(std::string(header) + payload);
    }
    return {std::move(parts), {}};
}

bool is_multipart_universe_qr(const std::string &text)
{
    return text.rfind(kMultipartPrefix, 0) == 0;
}

void UniverseQrAssembler::reset()
{
    set_crc_ = 0;
    total_length_ = 0;
    parts_.clear();
    received_count_ = 0;
}

UniverseQrAcceptResult UniverseQrAssembler::accept(const std::string &text)
{
    UniverseQrAcceptResult result;
    if (!is_multipart_universe_qr(text)) return result;

    std::size_t cursor = sizeof(kMultipartPrefix) - 1U;
    std::string set_crc_field;
    std::string length_field;
    std::string index_field;
    std::string count_field;
    std::string part_crc_field;
    std::uint32_t set_crc = 0;
    std::uint32_t part_crc = 0;
    std::size_t total_length = 0;
    std::size_t index = 0;
    std::size_t count = 0;
    if (!read_field(text, cursor, set_crc_field) ||
        !read_field(text, cursor, length_field) ||
        !read_field(text, cursor, index_field) ||
        !read_field(text, cursor, count_field) ||
        !read_field(text, cursor, part_crc_field) ||
        !parse_hex32(set_crc_field, set_crc) ||
        !parse_hex32(part_crc_field, part_crc) ||
        !parse_decimal(length_field, kMaximumArchiveLength, total_length) ||
        !parse_decimal(index_field, kMaximumUniverseQrParts, index) ||
        !parse_decimal(count_field, kMaximumUniverseQrParts, count)) {
        result.state = UniverseQrAcceptState::Error;
        result.error = "INVALID QR PART";
        return result;
    }

    const std::string payload = text.substr(cursor);
    if (total_length == 0U || count < 2U || index == 0U || index > count ||
        payload.empty() || payload.size() > kMaximumAcceptedPayload ||
        crc32(payload) != part_crc) {
        result.state = UniverseQrAcceptState::Error;
        result.error = crc32(payload) != part_crc
                           ? "QR PART CHECKSUM FAILED" : "INVALID QR PART";
        return result;
    }

    if (parts_.empty()) {
        set_crc_ = set_crc;
        total_length_ = total_length;
        parts_.resize(count);
    }
    else if (set_crc != set_crc_ || total_length != total_length_ ||
             count != parts_.size()) {
        result.state = UniverseQrAcceptState::Error;
        result.error = "DIFFERENT QR SET";
        result.received_count = received_count_;
        result.total_count = parts_.size();
        result.next_missing = first_missing(parts_);
        return result;
    }

    result.part_index = index;
    std::string &slot = parts_[index - 1U];
    if (!slot.empty()) {
        if (slot != payload) {
            result.state = UniverseQrAcceptState::Error;
            result.error = "QR PART CONFLICT";
        }
        else {
            result.state = UniverseQrAcceptState::Duplicate;
        }
    }
    else {
        slot = payload;
        ++received_count_;
        result.state = UniverseQrAcceptState::Accepted;
    }

    result.received_count = received_count_;
    result.total_count = parts_.size();
    result.next_missing = first_missing(parts_);
    if (result.state == UniverseQrAcceptState::Error ||
        received_count_ != parts_.size()) return result;

    std::string archive;
    archive.reserve(total_length_);
    for (const std::string &part : parts_) archive += part;
    if (archive.size() != total_length_ || crc32(archive) != set_crc_) {
        result.state = UniverseQrAcceptState::Error;
        result.error = "QR SET CHECKSUM FAILED";
        return result;
    }
    result.state = UniverseQrAcceptState::Complete;
    result.archive = std::move(archive);
    result.next_missing = 0;
    return result;
}

} 
