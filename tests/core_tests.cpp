#include "core.h"
#include "mavlink_enums.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

/**
 * @brief Fail independently of NDEBUG when a test expectation is false.
 * @param condition Expected boolean condition.
 * @param description Informative error description on assertion failure.
 */
static void check(bool condition, const char *description)
{
    if (!condition)
    {
        throw std::runtime_error(description);
    }
}

/**
 * @brief Create a zero-payload TX fixture from official message metadata.
 * @param id MAVLink message ID.
 * @return Zero-initialized TxEntry fixture.
 */
static Mav::TxEntry fixture(uint32_t id = 0)
{
    Mav::TxEntry entry;
    entry.key = 1;
    entry.id = id;
    const auto *meta = mavlink_get_msg_entry(id);
    if (meta)
    {
        entry.payload.resize(meta->max_msg_len, 0);
    }
    return entry;
}

/**
 * @brief Test primitive type metadata lookup and byte width calculations.
 */
static void testTypeMetadata()
{
    // Validate standard primitive type names
    check(Mav::typeName(MAVLINK_TYPE_CHAR) == "char", "Type name char");
    check(Mav::typeName(MAVLINK_TYPE_UINT8_T) == "uint8_t", "Type name uint8_t");
    check(Mav::typeName(MAVLINK_TYPE_INT8_T) == "int8_t", "Type name int8_t");
    check(Mav::typeName(MAVLINK_TYPE_UINT16_T) == "uint16_t", "Type name uint16_t");
    check(Mav::typeName(MAVLINK_TYPE_INT16_T) == "int16_t", "Type name int16_t");
    check(Mav::typeName(MAVLINK_TYPE_UINT32_T) == "uint32_t", "Type name uint32_t");
    check(Mav::typeName(MAVLINK_TYPE_INT32_T) == "int32_t", "Type name int32_t");
    check(Mav::typeName(MAVLINK_TYPE_UINT64_T) == "uint64_t", "Type name uint64_t");
    check(Mav::typeName(MAVLINK_TYPE_INT64_T) == "int64_t", "Type name int64_t");
    check(Mav::typeName(MAVLINK_TYPE_FLOAT) == "float", "Type name float");
    check(Mav::typeName(MAVLINK_TYPE_DOUBLE) == "double", "Type name double");

    // Invalid / out-of-range types
    check(Mav::typeName(static_cast<mavlink_message_type_t>(11)) == "unknown", "Out of range type 11 name");
    check(Mav::typeName(static_cast<mavlink_message_type_t>(255)) == "unknown", "Out of range type 255 name");

    // Validate byte widths
    check(Mav::typeSize(MAVLINK_TYPE_CHAR) == 1, "Size char == 1");
    check(Mav::typeSize(MAVLINK_TYPE_UINT8_T) == 1, "Size uint8_t == 1");
    check(Mav::typeSize(MAVLINK_TYPE_INT8_T) == 1, "Size int8_t == 1");
    check(Mav::typeSize(MAVLINK_TYPE_UINT16_T) == 2, "Size uint16_t == 2");
    check(Mav::typeSize(MAVLINK_TYPE_INT16_T) == 2, "Size int16_t == 2");
    check(Mav::typeSize(MAVLINK_TYPE_UINT32_T) == 4, "Size uint32_t == 4");
    check(Mav::typeSize(MAVLINK_TYPE_INT32_T) == 4, "Size int32_t == 4");
    check(Mav::typeSize(MAVLINK_TYPE_UINT64_T) == 8, "Size uint64_t == 8");
    check(Mav::typeSize(MAVLINK_TYPE_INT64_T) == 8, "Size int64_t == 8");
    check(Mav::typeSize(MAVLINK_TYPE_FLOAT) == 4, "Size float == 4");
    check(Mav::typeSize(MAVLINK_TYPE_DOUBLE) == 8, "Size double == 8");
    check(Mav::typeSize(static_cast<mavlink_message_type_t>(11)) == 0, "Out of range type 11 size == 0");
    check(Mav::typeSize(static_cast<mavlink_message_type_t>(99)) == 0, "Out of range type 99 size == 0");

    std::cout << "PASS: [01/12] Type Metadata and Size Consistency\n";
}

/**
 * @brief Test parser resilience against line noise, fragmentation, and stream resync.
 */
static void testParserNoiseAndRecovery()
{
    Mav::Parser parser;

    // Line noise and random garbage bytes must produce zero messages
    const Mav::Bytes noise = {0x00, 0x55, 0xAA, 0xFF, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0};
    check(parser.feed(noise).empty(), "Noise bytes must not decode");
    check(parser.crcErrors == 0, "Pure noise before sync byte produces no CRC error");

    // Single-byte incremental feed for fragmented frame
    auto entry = fixture(0); // HEARTBEAT
    mavlink_status_t status{};
    const auto validBytes = Mav::encode(0, 1, 2, false, entry.payload, status);
    check(!validBytes.empty() && validBytes[0] == MAVLINK_STX, "Encoded valid MAVLink 2 frame");

    std::vector<mavlink_message_t> decoded;
    for (size_t i = 0; i < validBytes.size(); ++i)
    {
        auto result = parser.feed({validBytes[i]});
        decoded.insert(decoded.end(), result.begin(), result.end());
        check(i == validBytes.size() - 1 || result.empty(), "No premature output during fragmented feed");
    }
    check(decoded.size() == 1, "Exactly one frame decoded from fragmented feed");
    check(decoded[0].sysid == 1 && decoded[0].compid == 2 && decoded[0].msgid == 0, "Decoded identity matches");

    // Recovery after leading noise bytes
    Mav::Bytes noiseThenValid = noise;
    noiseThenValid.insert(noiseThenValid.end(), validBytes.begin(), validBytes.end());
    auto recoveredFromNoise = parser.feed(noiseThenValid);
    check(recoveredFromNoise.size() == 1, "Parser decoded valid frame preceded by noise");
    check(recoveredFromNoise[0].sysid == 1 && recoveredFromNoise[0].compid == 2, "Frame identity intact");

    std::cout << "PASS: [02/12] Parser Noise Rejection and Stream Resynchronization\n";
}

/**
 * @brief Test framing verification, checksum failure accounting, and mixed-version streams.
 */
static void testParserFramingAndCrc()
{
    Mav::Parser parser;
    auto entry = fixture(0);
    mavlink_status_t status{};

    // MAVLink 2 and MAVLink 1 mixed in a single buffer
    const auto v2Bytes = Mav::encode(0, 10, 20, false, entry.payload, status);
    const auto v1Bytes = Mav::encode(0, 30, 40, true, entry.payload, status);
    check(v2Bytes[0] == MAVLINK_STX, "MAVLink 2 STX byte == 0xFD");
    check(v1Bytes[0] == MAVLINK_STX_MAVLINK1, "MAVLink 1 STX byte == 0xFE");

    Mav::Bytes combined = v2Bytes;
    combined.insert(combined.end(), v1Bytes.begin(), v1Bytes.end());
    combined.insert(combined.end(), v2Bytes.begin(), v2Bytes.end());

    auto decoded = parser.feed(combined);
    check(decoded.size() == 3, "Parsed 3 consecutive mixed frames in one buffer");
    check(decoded[0].magic == MAVLINK_STX && decoded[0].sysid == 10, "Frame 1: MAVLink 2 sysid 10");
    check(decoded[1].magic == MAVLINK_STX_MAVLINK1 && decoded[1].sysid == 30, "Frame 2: MAVLink 1 sysid 30");
    check(decoded[2].magic == MAVLINK_STX && decoded[2].sysid == 10, "Frame 3: MAVLink 2 sysid 10");

    // Corrupted payload byte -> Bad CRC accounting
    auto corruptPayload = v2Bytes;
    corruptPayload[10] ^= 0x55;
    const uint64_t crcBefore = parser.crcErrors;
    auto failResult = parser.feed(corruptPayload);
    check(failResult.empty(), "Corrupt payload dropped");
    check(parser.crcErrors == crcBefore + 1, "CRC error counter incremented");

    // Corrupted checksum bytes
    auto corruptCrc = v2Bytes;
    corruptCrc[corruptCrc.size() - 1] ^= 0xFF;
    failResult = parser.feed(corruptCrc);
    check(failResult.empty(), "Corrupt checksum byte dropped");
    check(parser.crcErrors == crcBefore + 2, "CRC error counter incremented again");

    std::cout << "PASS: [03/12] Parser Framing, CRC Errors and Signature Handling\n";
}

/**
 * @brief Test field reflection for all primitive integer types with strict boundaries.
 */
static void testFieldReflectionPrimitives()
{
    std::string error;
    mavlink_field_info_t field{};
    field.name = "test_int";
    field.array_length = 0;
    field.wire_offset = 0;

    // UINT8_T [0 .. 255]
    {
        field.type = MAVLINK_TYPE_UINT8_T;
        Mav::Bytes payload(1, 0);
        check(Mav::setField(payload, field, "0", error), "uint8 min 0");
        check(payload[0] == 0, "uint8 byte 0");
        check(Mav::setField(payload, field, "255", error), "uint8 max 255");
        check(payload[0] == 255, "uint8 byte 255");
        check(!Mav::setField(payload, field, "-1", error), "uint8 reject negative");
        check(!Mav::setField(payload, field, "256", error), "uint8 reject overflow 256");
        check(!Mav::setField(payload, field, "123junk", error), "uint8 reject trailing chars");
        check(!Mav::setField(payload, field, "", error), "uint8 reject empty string");
        check(!Mav::setField(payload, field, "   ", error), "uint8 reject whitespace only");
    }

    // INT8_T [-128 .. 127]
    {
        field.type = MAVLINK_TYPE_INT8_T;
        Mav::Bytes payload(1, 0);
        check(Mav::setField(payload, field, "0", error), "int8 zero");
        check(Mav::setField(payload, field, "-128", error), "int8 min -128");
        check(static_cast<int8_t>(payload[0]) == -128, "int8 byte -128");
        check(Mav::setField(payload, field, "127", error), "int8 max 127");
        check(static_cast<int8_t>(payload[0]) == 127, "int8 byte 127");
        check(!Mav::setField(payload, field, "-129", error), "int8 reject underflow -129");
        check(!Mav::setField(payload, field, "128", error), "int8 reject overflow 128");
    }

    // UINT16_T [0 .. 65535]
    {
        field.type = MAVLINK_TYPE_UINT16_T;
        Mav::Bytes payload(2, 0);
        check(Mav::setField(payload, field, "0", error), "uint16 min 0");
        check(Mav::setField(payload, field, "65535", error), "uint16 max 65535");
        uint16_t val = 0;
        std::memcpy(&val, payload.data(), sizeof(val));
        check(val == 65535, "uint16 wire value 65535");
        check(!Mav::setField(payload, field, "-1", error), "uint16 reject negative");
        check(!Mav::setField(payload, field, "65536", error), "uint16 reject overflow 65536");
    }

    // INT16_T [-32768 .. 32767]
    {
        field.type = MAVLINK_TYPE_INT16_T;
        Mav::Bytes payload(2, 0);
        check(Mav::setField(payload, field, "-32768", error), "int16 min -32768");
        int16_t val = 0;
        std::memcpy(&val, payload.data(), sizeof(val));
        check(val == -32768, "int16 wire value -32768");
        check(Mav::setField(payload, field, "32767", error), "int16 max 32767");
        std::memcpy(&val, payload.data(), sizeof(val));
        check(val == 32767, "int16 wire value 32767");
        check(!Mav::setField(payload, field, "-32769", error), "int16 reject underflow -32769");
        check(!Mav::setField(payload, field, "32768", error), "int16 reject overflow 32768");
    }

    // UINT32_T [0 .. 4294967295]
    {
        field.type = MAVLINK_TYPE_UINT32_T;
        Mav::Bytes payload(4, 0);
        check(Mav::setField(payload, field, "0", error), "uint32 min 0");
        check(Mav::setField(payload, field, "4294967295", error), "uint32 max 4294967295");
        uint32_t val = 0;
        std::memcpy(&val, payload.data(), sizeof(val));
        check(val == 4294967295U, "uint32 wire value 4294967295");
        check(!Mav::setField(payload, field, "-1", error), "uint32 reject negative");
        check(!Mav::setField(payload, field, "4294967296", error), "uint32 reject overflow");
    }

    // INT32_T [-2147483648 .. 2147483647]
    {
        field.type = MAVLINK_TYPE_INT32_T;
        Mav::Bytes payload(4, 0);
        check(Mav::setField(payload, field, "-2147483648", error), "int32 min -2147483648");
        int32_t val = 0;
        std::memcpy(&val, payload.data(), sizeof(val));
        check(val == -2147483648LL, "int32 wire value -2147483648");
        check(Mav::setField(payload, field, "2147483647", error), "int32 max 2147483647");
        std::memcpy(&val, payload.data(), sizeof(val));
        check(val == 2147483647, "int32 wire value 2147483647");
        check(!Mav::setField(payload, field, "-2147483649", error), "int32 reject underflow");
        check(!Mav::setField(payload, field, "2147483648", error), "int32 reject overflow");
    }

    // UINT64_T [0 .. 18446744073709551615] and roundtrip readField
    {
        field.type = MAVLINK_TYPE_UINT64_T;
        Mav::Bytes payload(8, 0);
        check(Mav::setField(payload, field, "18446744073709551615", error), "uint64 exact max");
        mavlink_message_t msg{};
        msg.len = 8;
        std::memcpy(_MAV_PAYLOAD_NON_CONST(&msg), payload.data(), 8);
        check(Mav::readField(msg, field) == "18446744073709551615", "uint64 roundtrip preserve all 64 bits");
        check(!Mav::setField(payload, field, "-1", error), "uint64 reject negative");
        check(!Mav::setField(payload, field, "18446744073709551616", error), "uint64 reject overflow");
    }

    // INT64_T [-9223372036854775808 .. 9223372036854775807]
    {
        field.type = MAVLINK_TYPE_INT64_T;
        Mav::Bytes payload(8, 0);
        check(Mav::setField(payload, field, "-9223372036854775808", error), "int64 min");
        mavlink_message_t msg{};
        msg.len = 8;
        std::memcpy(_MAV_PAYLOAD_NON_CONST(&msg), payload.data(), 8);
        check(Mav::readField(msg, field) == "-9223372036854775808", "int64 min roundtrip");
        check(Mav::setField(payload, field, "9223372036854775807", error), "int64 max");
        std::memcpy(_MAV_PAYLOAD_NON_CONST(&msg), payload.data(), 8);
        check(Mav::readField(msg, field) == "9223372036854775807", "int64 max roundtrip");
    }

    std::cout << "PASS: [04/12] Field Reflection Primitive Types and Range Boundaries\n";
}

/**
 * @brief Test floating-point reflection (IEEE-754), scientific notation, and non-finite rejection.
 */
static void testFieldReflectionFloats()
{
    std::string error;
    mavlink_field_info_t field{};
    field.name = "test_float";
    field.array_length = 0;
    field.wire_offset = 0;

    // FLOAT (IEEE-754 32-bit)
    {
        field.type = MAVLINK_TYPE_FLOAT;
        Mav::Bytes payload(4, 0);
        mavlink_message_t msg{};
        msg.len = 4;

        // Standard decimal and signs
        check(Mav::setField(payload, field, "0.0", error), "Float 0.0");
        check(Mav::setField(payload, field, "0.2", error), "Float 0.2");
        std::memcpy(_MAV_PAYLOAD_NON_CONST(&msg), payload.data(), 4);
        check(Mav::readField(msg, field) == "0.2", "Float 0.2 clean format");

        check(Mav::setField(payload, field, "+123.456", error), "Float with leading +");
        check(Mav::setField(payload, field, "-987.654", error), "Float with leading -");

        // Scientific notation
        check(Mav::setField(payload, field, "1.5e-3", error), "Float scientific 1.5e-3");
        check(Mav::setField(payload, field, "2.25E4", error), "Float scientific 2.25E4");

        // Reject non-finite values (NaN, Inf)
        check(!Mav::setField(payload, field, "nan", error), "Reject nan");
        check(!Mav::setField(payload, field, "NaN", error), "Reject NaN");
        check(!Mav::setField(payload, field, "inf", error), "Reject inf");
        check(!Mav::setField(payload, field, "-inf", error), "Reject -inf");
        check(!Mav::setField(payload, field, "+inf", error), "Reject +inf");

        // Reject malformed floats and overflows
        check(!Mav::setField(payload, field, "1e50", error), "Reject float overflow 1e50");
        check(!Mav::setField(payload, field, "0.2junk", error), "Reject trailing characters");
        check(!Mav::setField(payload, field, "1.2.3", error), "Reject multiple decimals");
        check(!Mav::setField(payload, field, "+", error), "Reject isolated sign");
        check(!Mav::setField(payload, field, "", error), "Reject empty float");
    }

    // DOUBLE (IEEE-754 64-bit)
    {
        field.type = MAVLINK_TYPE_DOUBLE;
        Mav::Bytes payload(8, 0);
        mavlink_message_t msg{};
        msg.len = 8;

        check(Mav::setField(payload, field, "3.141592653589793", error), "Double high precision");
        std::memcpy(_MAV_PAYLOAD_NON_CONST(&msg), payload.data(), 8);
        check(Mav::readField(msg, field).find("3.141592653589793") != std::string::npos, "Double roundtrip");

        check(Mav::setField(payload, field, "-1.23456789e-20", error), "Double small scientific");
        check(!Mav::setField(payload, field, "nan", error), "Double reject nan");
        check(!Mav::setField(payload, field, "inf", error), "Double reject inf");
        check(!Mav::setField(payload, field, "1e400", error), "Double reject overflow");
        check(!Mav::setField(payload, field, "3.14garbage", error), "Double reject trailing characters");
    }

    std::cout << "PASS: [05/12] Field Reflection IEEE-754 Floats, Doubles and Scientific Notation\n";
}

/**
 * @brief Test string (char array), fixed-size numeric arrays, and enum symbol mapping.
 */
static void testFieldReflectionStringsAndArrays()
{
    std::string error;

    // String / Char Array (MAVLINK_TYPE_CHAR)
    {
        mavlink_field_info_t charField{"text", nullptr, MAVLINK_TYPE_CHAR, 10, 0, 0};
        Mav::Bytes payload(10, 0);
        mavlink_message_t msg{};
        msg.len = 10;

        // Empty string -> zeroed payload
        check(Mav::setField(payload, charField, "", error), "Empty string accepted");
        check(payload == Mav::Bytes(10, 0), "Payload zero-initialized on empty string");

        // Normal string shorter than array
        check(Mav::setField(payload, charField, "HELLO", error), "Set string HELLO");
        std::memcpy(_MAV_PAYLOAD_NON_CONST(&msg), payload.data(), 10);
        check(Mav::readField(msg, charField) == "HELLO", "Read back HELLO");

        // String exactly matching array length
        check(Mav::setField(payload, charField, "0123456789", error), "String exact 10 chars");
        std::memcpy(_MAV_PAYLOAD_NON_CONST(&msg), payload.data(), 10);
        check(Mav::readField(msg, charField) == "0123456789", "Read back full 10 chars");

        // String exceeding array length must fail
        check(!Mav::setField(payload, charField, "0123456789X", error), "Reject string length 11 > 10");

        // Null-termination truncation on read
        payload[4] = 0; // "0123\056789"
        std::memcpy(_MAV_PAYLOAD_NON_CONST(&msg), payload.data(), 10);
        check(Mav::readField(msg, charField) == "0123", "ReadField stops at null terminator");
    }

    // Numeric Array (e.g. uint16_t array of length 4)
    {
        mavlink_field_info_t arrField{"values", nullptr, MAVLINK_TYPE_UINT16_T, 4, 0, 0};
        Mav::Bytes payload(8, 0);
        mavlink_message_t msg{};
        msg.len = 8;

        // Exact match
        check(Mav::setField(payload, arrField, "10, 20, 30, 40", error), "Array exact 4 elements");
        std::memcpy(_MAV_PAYLOAD_NON_CONST(&msg), payload.data(), 8);
        check(Mav::readField(msg, arrField) == "10, 20, 30, 40", "Array roundtrip formatted");

        // Boundary element values
        check(Mav::setField(payload, arrField, "0, 65535, 1, 32768", error), "Array boundary uint16 values");

        // Reject count mismatches
        check(!Mav::setField(payload, arrField, "10, 20, 30", error), "Reject underflow (3 elements)");
        check(!Mav::setField(payload, arrField, "10, 20, 30, 40, 50", error), "Reject overflow (5 elements)");

        // Reject syntax errors
        check(!Mav::setField(payload, arrField, "10, 20, 30, 40,", error), "Reject trailing comma");
        check(!Mav::setField(payload, arrField, ",10, 20, 30, 40", error), "Reject leading comma");
        check(!Mav::setField(payload, arrField, "10,,30, 40", error), "Reject missing middle item");
        check(!Mav::setField(payload, arrField, "10, -5, 30, 40", error), "Reject negative value in unsigned array");
    }

    // Enum Symbolic Names and Reverse Lookup
    {
        mavlink_field_info_t enumField{"type", nullptr, MAVLINK_TYPE_UINT8_T, 0, 0, 0};
        Mav::Bytes payload(1, 0);

        check(Mav::setField(payload, enumField, "MAV_AUTOPILOT_ARDUPILOTMEGA", error), "Symbolic enum resolution");
        check(payload[0] == 3, "MAV_AUTOPILOT_ARDUPILOTMEGA resolves to 3");

        // Reverse lookup
        check(Mav::lookupEnumName(0, "autopilot", 3) == "MAV_AUTOPILOT_ARDUPILOTMEGA", "Reverse enum lookup");
    }

    std::cout << "PASS: [06/12] Field Reflection Strings, Char Arrays and Numeric Arrays\n";
}

/**
 * @brief Test wire boundary bounds checks and defensive handling against corrupt metadata.
 */
static void testMetadataBoundsAndWireOffsets()
{
    std::string error;

    // Field wire_offset exceeds payload buffer size
    mavlink_field_info_t overflowField{};
    overflowField.name = "overflow";
    overflowField.type = MAVLINK_TYPE_UINT32_T;
    overflowField.array_length = 0;
    overflowField.wire_offset = 10;

    Mav::Bytes smallPayload(8, 0); // Need offset 10 + 4 = 14 > 8
    check(!Mav::setField(smallPayload, overflowField, "123", error), "Reject wire_offset + typeSize > payload");
    check(error == "Invalid field bounds", "Expected invalid field bounds error");

    // Invalid / unsupported type size
    mavlink_field_info_t invalidTypeField{};
    invalidTypeField.name = "invalid";
    invalidTypeField.type = static_cast<mavlink_message_type_t>(15);
    invalidTypeField.array_length = 0;
    invalidTypeField.wire_offset = 0;
    check(!Mav::setField(smallPayload, invalidTypeField, "123", error), "Reject unsupported type in setField");

    // readField bounds defense
    mavlink_message_t msg{};
    msg.len = 8;
    mavlink_field_info_t badOffsetField{};
    badOffsetField.name = "bad";
    badOffsetField.type = MAVLINK_TYPE_UINT32_T;
    badOffsetField.array_length = 0;
    badOffsetField.wire_offset = 300;
    check(Mav::readField(msg, badOffsetField) == "Invalid metadata", "readField rejects offset > MAX_PAYLOAD_LEN");
    check(Mav::readField(msg, invalidTypeField) == "Invalid metadata", "readField rejects typeSize == 0");

    std::cout << "PASS: [07/12] Field Reflection Metadata Bounds and Wire Offsets\n";
}

/**
 * @brief Validate catalog completeness, message length bounds, and full serialization integrity.
 */
static void testCatalogIntegrityAndBounds()
{
    const auto &catalog = Mav::catalog();
    check(!catalog.empty(), "Dialect catalog is non-empty");

    mavlink_status_t status{};

    for (const auto &info : catalog)
    {
        const auto *entry = mavlink_get_msg_entry(info.msgid);
        check(entry != nullptr, "Catalog message entry must exist in official registry");
        check(entry->max_msg_len <= MAVLINK_MAX_PAYLOAD_LEN, "Payload size bounded by MAVLink max");
        check(entry->min_msg_len <= entry->max_msg_len, "Min message len <= Max message len");

        auto item = fixture(info.msgid);
        std::string error;

        // Verify zero-initialization of every field
        for (unsigned i = 0; i < info.num_fields; ++i)
        {
            const auto &field = info.fields[i];
            std::string value = "0";
            if (field.type == MAVLINK_TYPE_CHAR && field.array_length)
            {
                value.clear();
            }
            else
            {
                for (unsigned j = 1; j < field.array_length; ++j)
                {
                    value += ",0";
                }
            }
            check(Mav::setField(item.payload, field, value, error), "Field bounds valid in catalog item");
        }

        // Serialization must succeed for MAVLink 2
        const auto v2 = Mav::encode(info.msgid, 1, 1, false, item.payload, status);
        check(!v2.empty(), "Catalog message serialized in MAVLink 2");

        // Serialization in MAVLink 1 if ID <= 255
        if (info.msgid <= 255)
        {
            const auto v1 = Mav::encode(info.msgid, 1, 1, true, item.payload, status);
            check(!v1.empty(), "MAVLink 1 serialization for ID <= 255");
        }
        else
        {
            const auto v1 = Mav::encode(info.msgid, 1, 1, true, item.payload, status);
            check(v1.empty(), "MAVLink 1 must reject message ID > 255");
        }
    }

    // Non-existent message ID serialization rejection
    Mav::Bytes dummyPayload(255, 0);
    check(Mav::encode(999999, 1, 1, false, dummyPayload, status).empty(), "Encode rejects invalid ID");

    std::cout << "PASS: [08/12] Message Catalog Integrity and Serialization Boundaries\n";
}

/**
 * @brief Test MAVLink parameter protocol encoding, formatting, negative integer union punning, and bounds.
 */
static void testParameterProtocol()
{
    // Type name mapping
    check(Mav::paramTypeName(MAV_PARAM_TYPE_UINT8) == "UINT8", "Param type UINT8");
    check(Mav::paramTypeName(MAV_PARAM_TYPE_INT8) == "INT8", "Param type INT8");
    check(Mav::paramTypeName(MAV_PARAM_TYPE_UINT16) == "UINT16", "Param type UINT16");
    check(Mav::paramTypeName(MAV_PARAM_TYPE_INT16) == "INT16", "Param type INT16");
    check(Mav::paramTypeName(MAV_PARAM_TYPE_UINT32) == "UINT32", "Param type UINT32");
    check(Mav::paramTypeName(MAV_PARAM_TYPE_INT32) == "INT32", "Param type INT32");
    check(Mav::paramTypeName(MAV_PARAM_TYPE_REAL32) == "FLOAT", "Param type FLOAT");
    check(Mav::paramTypeName(MAV_PARAM_TYPE_REAL64) == "DOUBLE", "Param type DOUBLE");
    check(Mav::paramTypeName(99) == "UNKNOWN", "Param type UNKNOWN");

    float wire = 0.0f;

    // UINT8 roundtrip
    check(Mav::encodeParamValue("255", MAV_PARAM_TYPE_UINT8, wire), "Encode UINT8 255");
    check(Mav::formatParamValue(wire, MAV_PARAM_TYPE_UINT8) == "255", "Format UINT8 255");

    // INT8 negative value roundtrip (critical: union punning must preserve signed bit)
    check(Mav::encodeParamValue("-50", MAV_PARAM_TYPE_INT8, wire), "Encode INT8 -50");
    check(Mav::formatParamValue(wire, MAV_PARAM_TYPE_INT8) == "-50", "Format INT8 -50");

    // UINT16 roundtrip
    check(Mav::encodeParamValue("65535", MAV_PARAM_TYPE_UINT16, wire), "Encode UINT16 65535");
    check(Mav::formatParamValue(wire, MAV_PARAM_TYPE_UINT16) == "65535", "Format UINT16 65535");

    // INT16 negative roundtrip
    check(Mav::encodeParamValue("-32000", MAV_PARAM_TYPE_INT16, wire), "Encode INT16 -32000");
    check(Mav::formatParamValue(wire, MAV_PARAM_TYPE_INT16) == "-32000", "Format INT16 -32000");

    // UINT32 roundtrip
    check(Mav::encodeParamValue("3000000000", MAV_PARAM_TYPE_UINT32, wire), "Encode UINT32 3000000000");
    check(Mav::formatParamValue(wire, MAV_PARAM_TYPE_UINT32) == "3000000000", "Format UINT32 3000000000");

    // INT32 negative roundtrip
    check(Mav::encodeParamValue("-2000000000", MAV_PARAM_TYPE_INT32, wire), "Encode INT32 -2000000000");
    check(Mav::formatParamValue(wire, MAV_PARAM_TYPE_INT32) == "-2000000000", "Format INT32 -2000000000");

    // REAL32 float roundtrip
    check(Mav::encodeParamValue("1.5", MAV_PARAM_TYPE_REAL32, wire), "Encode REAL32 1.5");
    check(Mav::formatParamValue(wire, MAV_PARAM_TYPE_REAL32) == "1.5", "Format REAL32 1.5");

    // Rejection of invalid param texts
    check(!Mav::encodeParamValue("abc", MAV_PARAM_TYPE_UINT32, wire), "Reject text abc for UINT32");
    check(!Mav::encodeParamValue("", MAV_PARAM_TYPE_REAL32, wire), "Reject empty text for REAL32");

    std::cout << "PASS: [09/12] Parameter Protocol Type-Punning, Negative Values and Bounds\n";
}

/**
 * @brief Test receiver bounded memory capacity, multi-device stream isolation, and param registry.
 */
static void testReceiverBoundedCapacity()
{
    Mav::Receiver receiver;

    // Multi-device stream separation
    mavlink_message_t msgA{}, msgB{}, msgC{};
    msgA.sysid = 1; msgA.compid = 1; msgA.msgid = 0; // Sys 1, Autopilot, Heartbeat
    msgB.sysid = 1; msgB.compid = 154; msgB.msgid = 0; // Sys 1, Gimbal, Heartbeat
    msgC.sysid = 2; msgC.compid = 1; msgC.msgid = 0; // Sys 2, Autopilot, Heartbeat

    receiver.accept(msgA, 1000);
    receiver.accept(msgB, 2000);
    receiver.accept(msgC, 3000);
    check(receiver.entries.size() == 3, "3 distinct device streams recorded");

    // Stream capacity bounded at MaxEntries (2048)
    // Flood with 2100 distinct message IDs to test memory bounding
    for (uint32_t id = 1; id <= 2100; ++id)
    {
        mavlink_message_t floodMsg{};
        floodMsg.sysid = 100;
        floodMsg.compid = 1;
        floodMsg.msgid = id;
        receiver.accept(floodMsg, 10000 + id);
    }
    check(receiver.entries.size() == Mav::Receiver::MaxEntries, "Receiver capped at MaxEntries (2048)");
    check(receiver.overflow == (3 + 2100) - Mav::Receiver::MaxEntries, "Receiver overflow counter recorded dropped streams");

    // Parameter collection per-device isolation
    Mav::Receiver paramReceiver;
    mavlink_message_t pMsg1{}, pMsg2{};
    mavlink_msg_param_value_pack(1, 1, &pMsg1, "SYS1_PARAM", 1.0f, MAV_PARAM_TYPE_REAL32, 1, 0);
    mavlink_msg_param_value_pack(2, 1, &pMsg2, "SYS2_PARAM", 2.0f, MAV_PARAM_TYPE_REAL32, 1, 0);

    paramReceiver.accept(pMsg1, 1000);
    paramReceiver.accept(pMsg2, 2000);

    const std::pair<uint32_t, uint8_t> dev1{1, 1};
    const std::pair<uint32_t, uint8_t> dev2{2, 1};
    check(paramReceiver.params[dev1].byId.count("SYS1_PARAM") == 1, "Dev1 param stored");
    check(paramReceiver.params[dev1].byId.count("SYS2_PARAM") == 0, "Dev2 param not in Dev1");
    check(paramReceiver.params[dev2].byId.count("SYS2_PARAM") == 1, "Dev2 param stored");

    // Parameter with full 16-character name without null terminator in wire array
    mavlink_message_t fullParamMsg{};
    char fullId[16] = {'1','2','3','4','5','6','7','8','9','0','A','B','C','D','E','F'};
    mavlink_param_value_t rawVal{};
    std::memcpy(rawVal.param_id, fullId, 16);
    rawVal.param_value = 42.0f;
    rawVal.param_type = MAV_PARAM_TYPE_REAL32;
    rawVal.param_count = 1;
    rawVal.param_index = 0;
    mavlink_msg_param_value_encode(1, 1, &fullParamMsg, &rawVal);
    paramReceiver.accept(fullParamMsg, 3000);

    const std::string expectedName(fullId, 16);
    check(paramReceiver.params[dev1].byId.count(expectedName) == 1, "16-char non-null terminated param ID parsed safely");

    std::cout << "PASS: [10/12] Receiver Bounded Stream Capacity and Multi-Device Isolation\n";
}

/**
 * @brief Test WriteQueue multi-step consumption, exact capacity limit, and backpressure rejection.
 */
static void testWriteQueueComprehensive()
{
    Mav::WriteQueue queue;

    // Initial state
    check(queue.size() == 0, "Initial queue size 0");
    check(queue.front().empty(), "Initial front empty");
    check(!queue.consume(1), "Consume on empty queue rejected");

    // Reject empty push
    check(!queue.push({}), "Empty frame push rejected");

    // Partial multi-step consume
    const Mav::Bytes frameA = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
    const Mav::Bytes frameB = {1, 2, 3};
    check(queue.push(frameA), "Pushed frame A (10 bytes)");
    check(queue.push(frameB), "Pushed frame B (3 bytes)");
    check(queue.size() == 13, "Queue size 13 bytes");

    // Step 1: consume 3 bytes of frame A
    check(queue.consume(3), "Consume 3 bytes of frame A");
    check(queue.size() == 10, "Remaining 10 bytes");
    check(queue.front() == Mav::Bytes({40, 50, 60, 70, 80, 90, 100}), "Front matches remainder of frame A");

    // Step 2: consume 4 bytes of frame A
    check(queue.consume(4), "Consume 4 bytes of frame A");
    check(queue.size() == 6, "Remaining 6 bytes");
    check(queue.front() == Mav::Bytes({80, 90, 100}), "Front matches last 3 bytes of frame A");

    // Step 3: consume remaining 3 bytes of frame A -> queue transitions to frame B
    check(queue.consume(3), "Consume final 3 bytes of frame A");
    check(queue.size() == 3, "Remaining 3 bytes");
    check(queue.front() == frameB, "Front seamlessly transitioned to frame B");

    // Reject invalid consumption exceeding head frame
    check(!queue.consume(4), "Reject consuming 4 bytes when frame B has only 3 bytes");
    check(queue.size() == 3, "Size unchanged after rejected consumption");
    check(queue.front() == frameB, "Front unchanged after rejected consumption");

    // Consume frame B completely
    check(queue.consume(3), "Consume frame B");
    check(queue.size() == 0 && queue.front().empty(), "Queue empty after consuming all frames");

    // Exact queue capacity limit (16384 bytes)
    check(queue.push(Mav::Bytes(Mav::WriteQueue::Limit, 0xAA)), "Push exact 16384 limit");
    check(queue.size() == Mav::WriteQueue::Limit, "Queue holds exactly 16384 bytes");
    check(!queue.push({0x55}), "Pushing even 1 extra byte rejected when at Limit");

    // Clear operation
    queue.clear();
    check(queue.size() == 0 && queue.front().empty(), "Clear drops all frames and resets state");

    std::cout << "PASS: [11/12] WriteQueue Multi-Step Consumption, Exact Capacity and Rejection\n";
}

/**
 * @brief Test monotonic rate estimation, sliding window expiration, due deadlines, and bandwidth estimates.
 */
static void testRatesAndThroughputEstimation()
{
    // Mav::Rate sliding window
    {
        Mav::Rate rate;
        rate.add(1000000);
        rate.add(1500000);
        check(rate.hz(1999999) == 2.0, "2 events within 1-second window");
        check(rate.hz(2000001) == 1.0, "Old event expired past 1 second");
        check(rate.hz(2500001) == 0.0, "All events expired past window");
    }

    // Mav::ByteRate sliding window
    {
        Mav::ByteRate byteRate;
        check(byteRate.bytesPerSecond(1000000) == 0.0, "Initial throughput 0");
        byteRate.add(500, 1000000);
        byteRate.add(1000, 1500000);
        check(byteRate.bytesPerSecond(1500000) == 1500.0, "Accumulated 1500 B/s");
        check(byteRate.bytesPerSecond(2000001) == 1000.0, "First sample expired");
        check(byteRate.bytesPerSecond(2500001) == 0.0, "Full window expiration to 0");

        byteRate.add(800, 3000000);
        byteRate.clear();
        check(byteRate.bytesPerSecond(3000000) == 0.0, "Clear resets ByteRate");
    }

    // Byte rate formatting
    {
        check(Mav::formatByteRate(-10.0) == "0 B/s", "Negative byte rate formatted as 0 B/s");
        check(Mav::formatByteRate(0.0) == "0 B/s", "Zero byte rate formatted as 0 B/s");
        check(Mav::formatByteRate(750.0) == "750 B/s", "Sub-kilobyte formatted as B/s");
        check(Mav::formatByteRate(1500.0) == "1.5 KB/s", "Kilobyte formatted as KB/s");
        check(Mav::formatByteRate(2500000.0) == "2.50 MB/s", "Megabyte formatted as MB/s");
    }

    // TxEntry validation, frame size, and due deadline without catch-up bursts
    {
        auto entry = fixture(0); // HEARTBEAT
        entry.active = true;
        entry.hz = 10.0;
        check(Mav::validTx(entry), "Heartbeat valid TX");
        check(Mav::frameSize(entry) == 21, "Heartbeat v2 frame size == 21 bytes (9 payload + 12 header/crc)");

        // Deadline progression
        check(Mav::due(entry, 0), "Initial deadline due");
        check(!Mav::due(entry, 50000), "Not due at 50ms for 10Hz (period 100ms)");
        check(Mav::due(entry, 100000), "Due at 100ms");

        // High elapsed time must advance deadline to next period without bursting
        check(Mav::due(entry, 10000000), "Due after long sleep");
        check(!Mav::due(entry, 10000000), "No immediate burst at same timestamp");

        // High frequency stream pacing without cumulative drift despite OS timer jitter
        auto entry100Hz = fixture(0);
        entry100Hz.active = true;
        entry100Hz.hz = 100.0;
        entry100Hz.deadline = 0;
        int framesSent100 = 0;
        for (int64_t tickMs = 0; tickMs <= 1000; ++tickMs)
        {
            // Simulate 1ms timer ticks with 150us jitter
            const int64_t nowUs = tickMs * 1000 + 150;
            if (Mav::due(entry100Hz, nowUs))
            {
                ++framesSent100;
            }
        }
        check(framesSent100 == 101, "100Hz stream sends exactly 101 frames across 1000ms with jitter");

        auto entry500Hz = fixture(0);
        entry500Hz.active = true;
        entry500Hz.hz = 500.0;
        entry500Hz.deadline = 0;
        int framesSent500 = 0;
        for (int64_t tickMs = 0; tickMs <= 1000; ++tickMs)
        {
            // Simulate 1ms timer ticks with 150us jitter
            const int64_t nowUs = tickMs * 1000 + 150;
            if (Mav::due(entry500Hz, nowUs))
            {
                ++framesSent500;
            }
        }
        check(framesSent500 == 501, "500Hz stream sends exactly 501 frames across 1000ms with jitter");
    }

    // Link capacity and bandwidth utilization estimation
    {
        auto entryA = fixture(0);
        entryA.active = true;
        entryA.hz = 10.0; // 21 * 10 = 210 B/s

        // Normal capacity under 115200 baud (11520 B/s at 10 bits/byte)
        check(Mav::fitsLink({}, entryA, 115200, 10.0), "210 B/s fits in 115200 baud");

        // Over-capacity link
        check(!Mav::fitsLink({}, entryA, 100, 10.0), "210 B/s rejected on 100 baud link");

        // Aggregate bandwidth check
        auto entryB = entryA;
        entryB.key = 2;
        entryB.hz = 50.0; // 21 * 50 = 1050 B/s
        check(!Mav::fitsLink({entryB}, entryA, 2000, 10.0), "Aggregate rate rejected on slow link");

        // estimateTxThroughput details
        std::vector<Mav::TxEntry> list{entryA, entryB};
        const auto est = Mav::estimateTxThroughput(list, 115200, 10.0, true);
        check(est.totalBytesPerSec == 1260.0, "Total 1260 B/s (210 + 1050)");
        check(est.fits == true, "Fits within 115200 capacity");
        check(est.utilizationPercent > 10.0 && est.utilizationPercent < 11.5, "Utilization ~10.9%");

        // Overload estimate
        entryB.hz = 600.0; // 21 * 600 = 12600 B/s > 11520 B/s
        list[1] = entryB;
        const auto overEst = Mav::estimateTxThroughput(list, 115200, 10.0, true);
        check(overEst.fits == false, "Overload detected");
        check(overEst.utilizationPercent > 100.0, "Utilization > 100%");
    }

    std::cout << "PASS: [12/12] Monotonic Rate Tracking, Due Scheduling and Bandwidth Estimation\n";
}

/**
 * @brief Main entry point exercising comprehensive core telemetry tests.
 */
int main()
{
    try
    {
        testTypeMetadata();
        testParserNoiseAndRecovery();
        testParserFramingAndCrc();
        testFieldReflectionPrimitives();
        testFieldReflectionFloats();
        testFieldReflectionStringsAndArrays();
        testMetadataBoundsAndWireOffsets();
        testCatalogIntegrityAndBounds();
        testParameterProtocol();
        testReceiverBoundedCapacity();
        testWriteQueueComprehensive();
        testRatesAndThroughputEstimation();

        std::cout << "ALL CORE TESTS PASSED: 12 exhaustive test suites, 100% assertions satisfied\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
