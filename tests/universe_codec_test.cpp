#include "orbit/body_colors.h"
#include "orbit/body_manager.h"
#include "orbit/universe_codec.h"
#include "orbit/universe_qr.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr char kPrefix[] = "ORBIT1:";
constexpr char kAlphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ$%*+-./:";

int base44_digit(char character)
{
    const char *found = std::strchr(kAlphabet, character);
    return found ? static_cast<int>(found - kAlphabet) : -1;
}

std::vector<std::uint8_t> decode_payload(const std::string &text)
{
    assert(text.rfind(kPrefix, 0) == 0);
    const std::string payload = text.substr(sizeof(kPrefix) - 1U);
    assert(payload.size() % 3U != 1U);
    std::vector<std::uint8_t> bytes;
    std::size_t index = 0;
    while (index + 2U < payload.size()) {
        const int a = base44_digit(payload[index]);
        const int b = base44_digit(payload[index + 1U]);
        const int c = base44_digit(payload[index + 2U]);
        assert(a >= 0 && b >= 0 && c >= 0);
        const unsigned value = static_cast<unsigned>((a * 44 + b) * 44 + c);
        assert(value <= 65535U);
        bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
        bytes.push_back(static_cast<std::uint8_t>(value));
        index += 3U;
    }
    if (index < payload.size()) {
        const int a = base44_digit(payload[index]);
        const int b = base44_digit(payload[index + 1U]);
        assert(a >= 0 && b >= 0);
        const unsigned value = static_cast<unsigned>(a * 44 + b);
        assert(value <= 255U);
        bytes.push_back(static_cast<std::uint8_t>(value));
    }
    return bytes;
}

std::uint32_t read_u32(const std::uint8_t *bytes)
{
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
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

orbit::PhysicsConfig test_config()
{
    orbit::PhysicsConfig config;
    config.gravity = 1.0;
    config.softening = 0.001;
    config.fixed_dt = 1.0 / 240.0;
    config.collisions_enabled = true;
    return config;
}

void test_small_universe()
{
    orbit::BodyManager bodies;
    assert(bodies.create({"Sol", {0.0, 0.0}, {0.0, 0.0}, 1000.0, 0.06,
                          orbit::kBodyColors[orbit::kStarDefaultColor].rgb,
                          orbit::BodyType::Star}));
    assert(bodies.create({"Terra", {1.0, 0.0}, {0.0, 31.0}, 1.0, 0.026,
                          orbit::kBodyColors[orbit::kPlanetDefaultColor].rgb,
                          orbit::BodyType::Planet}));

    const orbit::UniverseEncodeResult first = orbit::encode_universe(bodies, test_config());
    const orbit::UniverseEncodeResult second = orbit::encode_universe(bodies, test_config());
    assert(first);
    assert(first.text == second.text);
    assert(first.text.rfind(kPrefix, 0) == 0);
    for (char character : first.text.substr(sizeof(kPrefix) - 1U))
        assert(base44_digit(character) >= 0);

    const orbit::UniverseDecodeResult decoded = orbit::decode_universe(first.text);
    assert(decoded);
    assert(decoded.universe.body_count == 2U);
    assert(decoded.universe.config.collisions_enabled);
    assert(std::fabs(decoded.universe.config.gravity - 1.0) < 1.0e-6);
    assert(std::strcmp(decoded.universe.bodies[0].name.data(), "Sol") == 0);
    assert(decoded.universe.bodies[0].type == orbit::BodyType::Star);
    assert(decoded.universe.bodies[0].color ==
           orbit::kBodyColors[orbit::kStarDefaultColor].rgb);
    assert(std::strcmp(decoded.universe.bodies[1].name.data(), "Terra") == 0);
    assert(std::fabs(decoded.universe.bodies[1].pos.x - 1.0) < 1.0e-5);
    assert(std::fabs(decoded.universe.bodies[1].vel.y - 31.0) < 1.0e-4);
}

void test_maximum_universe_fits_qr()
{
    orbit::BodyManager bodies;
    for (std::size_t i = 0; i < orbit::BodyManager::kMaxBodies; ++i) {
        char name[16];
        std::snprintf(name, sizeof(name), "BODY%011zu", i);
        assert(std::strlen(name) == 15U);
        const auto color = orbit::kBodyColors[i % orbit::kBodyColors.size()].rgb;
        assert(bodies.create({name,
                              {static_cast<double>(i) * 100000.0,
                               -static_cast<double>(i) * 50000.0},
                              {static_cast<double>(i) * 10.0,
                               -static_cast<double>(i) * 20.0},
                              static_cast<double>(i + 1U), 0.01 + i,
                              color, static_cast<orbit::BodyType>(i % 4U)}));
    }

    const orbit::UniverseEncodeResult encoded = orbit::encode_universe(bodies, test_config());
    assert(encoded);
    assert(encoded.text.size() <= 3412U);
    const orbit::UniverseQrPartsResult split =
        orbit::make_universe_qr_parts(encoded.text);
    assert(split);
    assert(split.parts.size() >= 2U && split.parts.size() <= 7U);
    for (const std::string &part : split.parts)
        assert(part.size() <= 600U);

    const std::vector<std::uint8_t> bytes = decode_payload(encoded.text);
    assert(bytes.size() == 2270U);
    assert(bytes[1] == 64U);
    assert(read_u32(bytes.data() + bytes.size() - 4U) ==
           crc32(bytes.data(), bytes.size() - 4U));

    const orbit::UniverseDecodeResult decoded = orbit::decode_universe(encoded.text);
    assert(decoded);
    assert(decoded.universe.body_count == orbit::BodyManager::kMaxBodies);
}

void test_invalid_codes_are_rejected()
{
    orbit::BodyManager bodies;
    assert(bodies.create({"Test", {1.0, 2.0}, {3.0, 4.0}, 5.0, 0.02,
                          orbit::kBodyColors[0].rgb, orbit::BodyType::Planet}));
    const orbit::UniverseEncodeResult encoded = orbit::encode_universe(bodies, test_config());
    assert(encoded);

    std::string corrupt = encoded.text;
    corrupt.back() = corrupt.back() == '0' ? '1' : '0';
    const orbit::UniverseDecodeResult bad_crc = orbit::decode_universe(corrupt);
    assert(!bad_crc);
    assert(bad_crc.error == "CHECKSUM FAILED");

    std::string invalid_character = encoded.text;
    invalid_character[sizeof(kPrefix)] = '?';
    assert(!orbit::decode_universe(invalid_character));
    assert(!orbit::decode_universe("ORBIT1:0"));
    assert(!orbit::decode_universe("WRONG:000"));
}

void test_multipart_qr_round_trip()
{
    std::string archive = "ORBIT1:";
    while (archive.size() < 1800U) archive += kAlphabet;
    const orbit::UniverseQrPartsResult split =
        orbit::make_universe_qr_parts(archive);
    assert(split);
    assert(split.parts.size() == 4U);
    for (const std::string &part : split.parts)
        assert(orbit::is_multipart_universe_qr(part));

    orbit::UniverseQrAssembler assembler;
    orbit::UniverseQrAcceptResult accepted = assembler.accept(split.parts[2]);
    assert(accepted.state == orbit::UniverseQrAcceptState::Accepted);
    assert(accepted.part_index == 3U);
    assert(accepted.next_missing == 1U);

    const orbit::UniverseQrAcceptResult duplicate = assembler.accept(split.parts[2]);
    assert(duplicate.state == orbit::UniverseQrAcceptState::Duplicate);
    assert(duplicate.received_count == 1U);

    assert(assembler.accept(split.parts[0]).state ==
           orbit::UniverseQrAcceptState::Accepted);
    assert(assembler.accept(split.parts[3]).state ==
           orbit::UniverseQrAcceptState::Accepted);
    const orbit::UniverseQrAcceptResult complete = assembler.accept(split.parts[1]);
    assert(complete.state == orbit::UniverseQrAcceptState::Complete);
    assert(complete.archive == archive);
    assert(complete.received_count == split.parts.size());
}

void test_multipart_qr_rejects_corruption_and_mixed_sets()
{
    const std::string first_archive(1200U, 'A');
    const std::string second_archive(1200U, 'B');
    const orbit::UniverseQrPartsResult first =
        orbit::make_universe_qr_parts(first_archive);
    const orbit::UniverseQrPartsResult second =
        orbit::make_universe_qr_parts(second_archive);
    assert(first && second);

    std::string corrupt = first.parts[0];
    corrupt.back() = corrupt.back() == 'A' ? 'B' : 'A';
    orbit::UniverseQrAssembler assembler;
    const orbit::UniverseQrAcceptResult bad = assembler.accept(corrupt);
    assert(bad.state == orbit::UniverseQrAcceptState::Error);
    assert(bad.error == "QR PART CHECKSUM FAILED");

    assert(assembler.accept(first.parts[0]).state ==
           orbit::UniverseQrAcceptState::Accepted);
    const orbit::UniverseQrAcceptResult mixed = assembler.accept(second.parts[1]);
    assert(mixed.state == orbit::UniverseQrAcceptState::Error);
    assert(mixed.error == "DIFFERENT QR SET");
}

} 

int main()
{
    test_small_universe();
    test_maximum_universe_fits_qr();
    test_invalid_codes_are_rejected();
    test_multipart_qr_round_trip();
    test_multipart_qr_rejects_corruption_and_mixed_sets();
    return 0;
}
