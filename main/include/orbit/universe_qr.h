#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace orbit {

constexpr std::size_t kUniverseQrChunkPayload = 540;
constexpr std::size_t kMaximumUniverseQrParts = 32;

struct UniverseQrPartsResult {
    std::vector<std::string> parts;
    std::string error;

    explicit operator bool() const { return error.empty(); }
};

UniverseQrPartsResult make_universe_qr_parts(const std::string &archive);
bool is_multipart_universe_qr(const std::string &text);

enum class UniverseQrAcceptState {
    NotMultipart,
    Accepted,
    Duplicate,
    Complete,
    Error,
};

struct UniverseQrAcceptResult {
    UniverseQrAcceptState state = UniverseQrAcceptState::NotMultipart;
    std::string archive;
    std::string error;
    std::size_t part_index = 0;
    std::size_t received_count = 0;
    std::size_t total_count = 0;
    std::size_t next_missing = 0;
};

class UniverseQrAssembler {
public:
    UniverseQrAcceptResult accept(const std::string &text);
    void reset();

    std::size_t received_count() const { return received_count_; }
    std::size_t total_count() const { return parts_.size(); }

private:
    std::uint32_t set_crc_ = 0;
    std::size_t total_length_ = 0;
    std::vector<std::string> parts_;
    std::size_t received_count_ = 0;
};

} 
