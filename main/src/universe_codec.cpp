#include "orbit/universe_codec.h"

#include "orbit/body_colors.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace orbit {

namespace {

constexpr char kPrefix[] = "ORBIT1:";
constexpr char kBase44Alphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ$%*+-./:";
constexpr std::int32_t kSigned24Maximum = 8388607;
constexpr std::size_t kMaximumTextLength = 3412;
constexpr std::size_t kHeaderSize = 26;
constexpr std::size_t kChecksumSize = 4;

void append_u16(std::vector<std::uint8_t> &bytes, std::uint16_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void append_u32(std::vector<std::uint8_t> &bytes, std::uint32_t value)
{
    for (int shift = 0; shift < 32; shift += 8)
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}

bool append_f32(std::vector<std::uint8_t> &bytes, double value)
{
    const float narrowed = static_cast<float>(value);
    if (!std::isfinite(value) || !std::isfinite(narrowed)) return false;
    std::uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(narrowed));
    std::memcpy(&bits, &narrowed, sizeof(bits));
    append_u32(bytes, bits);
    return true;
}

void append_s24(std::vector<std::uint8_t> &bytes, double value, float scale)
{
    const long long rounded = std::llround(value / static_cast<double>(scale));
    const std::int32_t quantized = static_cast<std::int32_t>(
        std::clamp(rounded, -static_cast<long long>(kSigned24Maximum),
                   static_cast<long long>(kSigned24Maximum)));
    const std::uint32_t raw = static_cast<std::uint32_t>(quantized) & 0xFFFFFFU;
    bytes.push_back(static_cast<std::uint8_t>(raw));
    bytes.push_back(static_cast<std::uint8_t>(raw >> 8U));
    bytes.push_back(static_cast<std::uint8_t>(raw >> 16U));
}

float scale_for(double maximum, double units, float minimum)
{
    if (!std::isfinite(maximum)) return std::numeric_limits<float>::quiet_NaN();
    const double exact = maximum > 0.0 ? maximum / units : 0.0;
    float scale = std::max(static_cast<float>(exact), minimum);
    if (static_cast<double>(scale) * units < maximum)
        scale = std::nextafter(scale, std::numeric_limits<float>::infinity());
    return scale;
}

std::uint32_t crc32(const std::vector<std::uint8_t> &bytes)
{
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::uint8_t byte : bytes) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1U) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
    }
    return crc ^ 0xFFFFFFFFU;
}

std::uint32_t crc32(const std::uint8_t *bytes, std::size_t size)
{
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1U) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
    }
    return crc ^ 0xFFFFFFFFU;
}

std::size_t bounded_name_length(const char *name, std::size_t capacity)
{
    std::size_t length = 0;
    while (length < capacity && name[length] != '\0') ++length;
    return length;
}

std::string encode_base44(const std::vector<std::uint8_t> &bytes)
{
    std::string result = kPrefix;
    result.reserve(result.size() + (bytes.size() / 2U) * 3U +
                   (bytes.size() % 2U ? 2U : 0U));
    std::size_t i = 0;
    for (; i + 1 < bytes.size(); i += 2) {
        const unsigned value = static_cast<unsigned>(bytes[i]) * 256U + bytes[i + 1];
        result.push_back(kBase44Alphabet[value / (44U * 44U)]);
        result.push_back(kBase44Alphabet[(value / 44U) % 44U]);
        result.push_back(kBase44Alphabet[value % 44U]);
    }
    if (i < bytes.size()) {
        const unsigned value = bytes[i];
        result.push_back(kBase44Alphabet[value / 44U]);
        result.push_back(kBase44Alphabet[value % 44U]);
    }
    return result;
}

int base44_digit(char character)
{
    const char *found = std::strchr(kBase44Alphabet, character);
    return found ? static_cast<int>(found - kBase44Alphabet) : -1;
}

bool decode_base44(const std::string &text, std::vector<std::uint8_t> &bytes)
{
    if (text.size() > kMaximumTextLength || text.rfind(kPrefix, 0) != 0) return false;
    const std::size_t prefix_length = sizeof(kPrefix) - 1U;
    const std::size_t payload_length = text.size() - prefix_length;
    if (payload_length == 0 || payload_length % 3U == 1U) return false;

    bytes.clear();
    bytes.reserve((payload_length / 3U) * 2U + (payload_length % 3U ? 1U : 0U));
    std::size_t index = prefix_length;
    while (index + 2U < text.size()) {
        const int a = base44_digit(text[index]);
        const int b = base44_digit(text[index + 1U]);
        const int c = base44_digit(text[index + 2U]);
        if (a < 0 || b < 0 || c < 0) return false;
        const unsigned value = static_cast<unsigned>((a * 44 + b) * 44 + c);
        if (value > 65535U) return false;
        bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
        bytes.push_back(static_cast<std::uint8_t>(value));
        index += 3U;
    }
    if (index < text.size()) {
        const int a = base44_digit(text[index]);
        const int b = base44_digit(text[index + 1U]);
        if (a < 0 || b < 0) return false;
        const unsigned value = static_cast<unsigned>(a * 44 + b);
        if (value > 255U) return false;
        bytes.push_back(static_cast<std::uint8_t>(value));
    }
    return true;
}

class ByteReader {
public:
    ByteReader(const std::uint8_t *bytes, std::size_t size)
        : bytes_(bytes), size_(size) {}

    bool read_u8(std::uint8_t &value)
    {
        if (remaining() < 1U) return false;
        value = bytes_[position_++];
        return true;
    }

    bool read_u16(std::uint16_t &value)
    {
        if (remaining() < 2U) return false;
        value = static_cast<std::uint16_t>(bytes_[position_]) |
                static_cast<std::uint16_t>(bytes_[position_ + 1U] << 8U);
        position_ += 2U;
        return true;
    }

    bool read_u32(std::uint32_t &value)
    {
        if (remaining() < 4U) return false;
        value = static_cast<std::uint32_t>(bytes_[position_]) |
                (static_cast<std::uint32_t>(bytes_[position_ + 1U]) << 8U) |
                (static_cast<std::uint32_t>(bytes_[position_ + 2U]) << 16U) |
                (static_cast<std::uint32_t>(bytes_[position_ + 3U]) << 24U);
        position_ += 4U;
        return true;
    }

    bool read_s24(std::int32_t &value)
    {
        if (remaining() < 3U) return false;
        std::uint32_t raw = static_cast<std::uint32_t>(bytes_[position_]) |
                            (static_cast<std::uint32_t>(bytes_[position_ + 1U]) << 8U) |
                            (static_cast<std::uint32_t>(bytes_[position_ + 2U]) << 16U);
        position_ += 3U;
        if (raw & 0x800000U) raw |= 0xFF000000U;
        value = static_cast<std::int32_t>(raw);
        return true;
    }

    bool read_f32(float &value)
    {
        std::uint32_t bits = 0;
        if (!read_u32(bits)) return false;
        static_assert(sizeof(bits) == sizeof(value));
        std::memcpy(&value, &bits, sizeof(value));
        return true;
    }

    bool read_bytes(char *output, std::size_t size)
    {
        if (remaining() < size) return false;
        std::memcpy(output, bytes_ + position_, size);
        position_ += size;
        return true;
    }

    std::size_t remaining() const { return size_ - position_; }

private:
    const std::uint8_t *bytes_;
    std::size_t size_;
    std::size_t position_ = 0;
};

} 

UniverseEncodeResult encode_universe(const BodyManager &bodies,
                                     const PhysicsConfig &config)
{
    if (!std::isfinite(config.gravity) || config.gravity < 0.0 ||
        !std::isfinite(config.softening) || config.softening < 0.0 ||
        !std::isfinite(config.fixed_dt) || config.fixed_dt <= 0.0) {
        return {{}, "INVALID PHYSICS"};
    }

    double maximum_position = 0.0;
    double maximum_velocity = 0.0;
    double maximum_radius = 0.0;
    for (const Body &body : bodies.bodies()) {
        if (!body.active) continue;
        if (!std::isfinite(body.pos.x) || !std::isfinite(body.pos.y) ||
            !std::isfinite(body.vel.x) || !std::isfinite(body.vel.y) ||
            !std::isfinite(body.mass) || body.mass <= 0.0 ||
            !std::isfinite(body.radius) || body.radius <= 0.0) {
            return {{}, "INVALID BODY"};
        }
        maximum_position = std::max({maximum_position, std::fabs(body.pos.x),
                                     std::fabs(body.pos.y)});
        maximum_velocity = std::max({maximum_velocity, std::fabs(body.vel.x),
                                     std::fabs(body.vel.y)});
        maximum_radius = std::max(maximum_radius, body.radius);
    }

    const float position_scale = scale_for(maximum_position, kSigned24Maximum, 1.0e-6F);
    const float velocity_scale = scale_for(maximum_velocity, kSigned24Maximum, 1.0e-6F);
    const float radius_scale = scale_for(maximum_radius, 65535.0, 1.0e-8F);
    if (!std::isfinite(position_scale) || !std::isfinite(velocity_scale) ||
        !std::isfinite(radius_scale)) {
        return {{}, "VALUE OUT OF RANGE"};
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve(2270);
    bytes.push_back(config.collisions_enabled ? 1U : 0U);
    bytes.push_back(static_cast<std::uint8_t>(bodies.active_count()));
    if (!append_f32(bytes, config.gravity) || !append_f32(bytes, config.softening) ||
        !append_f32(bytes, config.fixed_dt) || !append_f32(bytes, position_scale) ||
        !append_f32(bytes, velocity_scale) || !append_f32(bytes, radius_scale)) {
        return {{}, "VALUE OUT OF RANGE"};
    }

    for (const Body &body : bodies.bodies()) {
        if (!body.active) continue;
        const std::size_t name_length = bounded_name_length(body.name, sizeof(body.name));
        const std::size_t color_index = nearest_body_color(body.color);
        const auto type = static_cast<std::uint8_t>(body.type);
        if (name_length > 15 || type > 3) return {{}, "INVALID BODY"};

        bytes.push_back(static_cast<std::uint8_t>((type << 6U) | (color_index << 3U)));
        bytes.push_back(static_cast<std::uint8_t>(name_length));
        append_s24(bytes, body.pos.x, position_scale);
        append_s24(bytes, body.pos.y, position_scale);
        append_s24(bytes, body.vel.x, velocity_scale);
        append_s24(bytes, body.vel.y, velocity_scale);
        if (!append_f32(bytes, body.mass)) return {{}, "VALUE OUT OF RANGE"};
        const long long radius_value = std::llround(body.radius / radius_scale);
        append_u16(bytes, static_cast<std::uint16_t>(
            std::clamp(radius_value, 1LL, 65535LL)));
        bytes.insert(bytes.end(), body.name, body.name + name_length);
    }

    append_u32(bytes, crc32(bytes));
    std::string text = encode_base44(bytes);
    if (text.size() > kMaximumTextLength) return {{}, "UNIVERSE TOO LARGE"};
    return {std::move(text), {}};
}

UniverseDecodeResult decode_universe(const std::string &text)
{
    std::vector<std::uint8_t> bytes;
    if (!decode_base44(text, bytes)) return {{}, "INVALID ORBIT CODE"};
    if (bytes.size() < kHeaderSize + kChecksumSize)
        return {{}, "TRUNCATED ORBIT CODE"};

    const std::size_t payload_size = bytes.size() - kChecksumSize;
    const std::uint32_t expected_crc =
        static_cast<std::uint32_t>(bytes[payload_size]) |
        (static_cast<std::uint32_t>(bytes[payload_size + 1U]) << 8U) |
        (static_cast<std::uint32_t>(bytes[payload_size + 2U]) << 16U) |
        (static_cast<std::uint32_t>(bytes[payload_size + 3U]) << 24U);
    if (crc32(bytes.data(), payload_size) != expected_crc)
        return {{}, "CHECKSUM FAILED"};

    ByteReader reader(bytes.data(), payload_size);
    DecodedUniverse universe;
    std::uint8_t flags = 0;
    std::uint8_t body_count = 0;
    float gravity = 0.0F;
    float softening = 0.0F;
    float fixed_dt = 0.0F;
    float position_scale = 0.0F;
    float velocity_scale = 0.0F;
    float radius_scale = 0.0F;
    if (!reader.read_u8(flags) || !reader.read_u8(body_count) ||
        !reader.read_f32(gravity) || !reader.read_f32(softening) ||
        !reader.read_f32(fixed_dt) || !reader.read_f32(position_scale) ||
        !reader.read_f32(velocity_scale) || !reader.read_f32(radius_scale)) {
        return {{}, "TRUNCATED ORBIT CODE"};
    }
    if ((flags & ~1U) != 0U || body_count > BodyManager::kMaxBodies ||
        !std::isfinite(gravity) || gravity < 0.0F ||
        !std::isfinite(softening) || softening < 0.0F ||
        !std::isfinite(fixed_dt) || fixed_dt <= 0.0F ||
        !std::isfinite(position_scale) || position_scale <= 0.0F ||
        !std::isfinite(velocity_scale) || velocity_scale <= 0.0F ||
        !std::isfinite(radius_scale) || radius_scale <= 0.0F) {
        return {{}, "INVALID UNIVERSE DATA"};
    }

    universe.config.gravity = gravity;
    universe.config.softening = softening;
    universe.config.fixed_dt = fixed_dt;
    universe.config.collisions_enabled = (flags & 1U) != 0U;
    universe.body_count = body_count;

    for (std::size_t i = 0; i < universe.body_count; ++i) {
        std::uint8_t metadata = 0;
        std::uint8_t name_length = 0;
        std::int32_t pos_x = 0;
        std::int32_t pos_y = 0;
        std::int32_t vel_x = 0;
        std::int32_t vel_y = 0;
        float mass = 0.0F;
        std::uint16_t radius = 0;
        if (!reader.read_u8(metadata) || !reader.read_u8(name_length) ||
            !reader.read_s24(pos_x) || !reader.read_s24(pos_y) ||
            !reader.read_s24(vel_x) || !reader.read_s24(vel_y) ||
            !reader.read_f32(mass) || !reader.read_u16(radius)) {
            return {{}, "TRUNCATED ORBIT CODE"};
        }
        const std::size_t color_index = (metadata >> 3U) & 0x07U;
        const std::uint8_t type = metadata >> 6U;
        if ((metadata & 0x07U) != 0U || name_length > 15U ||
            color_index >= kBodyColors.size() || type > 3U || radius == 0U ||
            !std::isfinite(mass) || mass <= 0.0F) {
            return {{}, "INVALID BODY DATA"};
        }

        DecodedBody &body = universe.bodies[i];
        if (!reader.read_bytes(body.name.data(), name_length))
            return {{}, "TRUNCATED ORBIT CODE"};
        if (std::find(body.name.begin(), body.name.begin() + name_length, '\0') !=
            body.name.begin() + name_length) {
            return {{}, "INVALID BODY DATA"};
        }
        body.name[name_length] = '\0';
        body.pos = {static_cast<double>(pos_x) * position_scale,
                    static_cast<double>(pos_y) * position_scale};
        body.vel = {static_cast<double>(vel_x) * velocity_scale,
                    static_cast<double>(vel_y) * velocity_scale};
        body.mass = mass;
        body.radius = static_cast<double>(radius) * radius_scale;
        body.color = kBodyColors[color_index].rgb;
        body.type = static_cast<BodyType>(type);
        if (!std::isfinite(body.pos.x) || !std::isfinite(body.pos.y) ||
            !std::isfinite(body.vel.x) || !std::isfinite(body.vel.y) ||
            !std::isfinite(body.radius) || body.radius <= 0.0) {
            return {{}, "INVALID BODY DATA"};
        }
    }
    if (reader.remaining() != 0U) return {{}, "INVALID UNIVERSE DATA"};
    return {std::move(universe), {}};
}

} 
