#include "core.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>

/** @brief Fail independently of NDEBUG when a test expectation is false. */
static void check(bool condition, const char *description)
{
    if (!condition)
    {
        throw std::runtime_error(description);
    }
}

/** @brief Create a zero-payload TX fixture from official message metadata. */
static Mav::TxEntry fixture(uint32_t id = 0)
{
    Mav::TxEntry entry;
    entry.key = 1;
    entry.id = id;
    entry.payload.resize(mavlink_get_msg_entry(id)->max_msg_len);
    return entry;
}

/** @brief Exercise real codec boundaries, sender isolation, timing and queue behavior. */
int main()
{
    try
    {
        auto entry = fixture();
        mavlink_status_t status{};
        const auto bytes = Mav::encode(0, 1, 2, false, entry.payload, status);
        check(!bytes.empty() && bytes[0] == MAVLINK_STX, "MAVLink2 encoding");
        Mav::Parser parser;
        check(parser.feed({0x00, 0x55, 0xff}).empty(), "Noise must not decode");
        std::vector<mavlink_message_t> decoded;
        for (size_t i = 0; i < bytes.size(); ++i)
        {
            auto result = parser.feed({bytes[i]});
            decoded.insert(decoded.end(), result.begin(), result.end());
            check(i == bytes.size() - 1 || result.empty(), "No premature fragmented frame");
        }
        check(decoded.size() == 1 && decoded[0].sysid == 1 && decoded[0].compid == 2, "Fragmented identity");
        auto corrupt = bytes;
        corrupt[10] ^= 0x80;
        check(parser.feed(corrupt).empty() && parser.crcErrors == 1, "Corrupt frame rejected");
        auto combined = bytes;
        const auto version1 = Mav::encode(0, 3, 4, true, entry.payload, status);
        combined.insert(combined.end(), version1.begin(), version1.end());
        decoded = parser.feed(combined);
        check(decoded.size() == 2 && decoded[1].magic == MAVLINK_STX_MAVLINK1, "Mixed versions and frames per read");
        Mav::Receiver receiver;
        receiver.accept(decoded[0], 100);
        receiver.accept(decoded[1], 200);
        auto otherComponent = decoded[0];
        otherComponent.compid = 5;
        receiver.accept(otherComponent, 300);
        check(receiver.entries.size() == 3, "Independent sender/component streams");
        Mav::Rate rate;
        rate.add(0);
        rate.add(500000);
        check(rate.hz(999999) == 2 && rate.hz(1500000) == 0, "Rate expires after one second");
        entry.active = true;
        entry.hz = 10;
        check(Mav::due(entry, 0), "Initial deadline due");
        check(!Mav::due(entry, 99999), "No early send");
        check(Mav::due(entry, 5000000) && !Mav::due(entry, 5000000), "No catch-up burst");
        check(Mav::fitsLink({}, entry, 115200, 10), "Normal capacity");
        check(!Mav::fitsLink({}, entry, 100, 10), "Over capacity rejected");
        auto extra = entry;
        extra.key = 2;
        check(!Mav::fitsLink({extra}, entry, 3000, 10), "Aggregate capacity rejected");
        auto extended = fixture(265);
        extended.version1 = true;
        check(!Mav::validTx(extended) && Mav::encode(265, 1, 2, true, extended.payload, status).empty(), "MAVLink1 ID limit");

        std::string error;
        const auto *heartbeat = mavlink_get_message_info_by_id(0);
        for (unsigned i = 0; i < heartbeat->num_fields; ++i)
        {
            const auto &field = heartbeat->fields[i];
            if (field.type == MAVLINK_TYPE_UINT8_T)
            {
                check(!Mav::setField(entry.payload, field, "256", error), "uint8 overflow");
                check(!Mav::setField(entry.payload, field, "-1", error), "uint8 negative");
                check(!Mav::setField(entry.payload, field, "1junk", error), "Trailing garbage");
                check(Mav::setField(entry.payload, field, "255", error), "uint8 maximum");
            }
        }
        mavlink_field_info_t integer{"value", nullptr, MAVLINK_TYPE_UINT64_T, 0, 0, 0};
        Mav::Bytes payload(8);
        check(Mav::setField(payload, integer, "18446744073709551615", error), "Exact uint64 maximum");
        mavlink_message_t message{};
        message.len = 8;
        std::copy(payload.begin(), payload.end(), reinterpret_cast<uint8_t *>(_MAV_PAYLOAD_NON_CONST(&message)));
        check(Mav::readField(message, integer) == "18446744073709551615", "uint64 roundtrip without double loss");
        integer.type = MAVLINK_TYPE_INT64_T;
        check(Mav::setField(payload, integer, "-9223372036854775808", error), "int64 minimum");
        integer.type = MAVLINK_TYPE_FLOAT;
        check(Mav::setField(payload, integer, "0.2", error), "Parse float 0.2");
        std::copy(payload.begin(), payload.end(), reinterpret_cast<uint8_t *>(_MAV_PAYLOAD_NON_CONST(&message)));
        check(Mav::readField(message, integer) == "0.2", "float 0.2 roundtrip formatted cleanly");
        check(Mav::setField(payload, integer, "+0.2", error), "Parse float +0.2");
        check(Mav::setField(payload, integer, "-0.2", error), "Parse float -0.2");
        check(!Mav::setField(payload, integer, "0.2junk", error), "Trailing chars in float rejected");
        check(!Mav::setField(payload, integer, "1e100", error), "float overflow");
        check(!Mav::setField(payload, integer, "nan", error), "Non-finite rejected");
        integer.type = MAVLINK_TYPE_UINT16_T;
        integer.array_length = 3;
        check(!Mav::setField(payload, integer, "1,2", error), "Array length validation");
        check(!Mav::setField(payload, integer, "1,2,3,", error), "Trailing array delimiter");
        check(Mav::setField(payload, integer, "1,2,65535", error), "Array boundary");
        integer.wire_offset = 7;
        check(!Mav::setField(payload, integer, "1,2,3", error), "Metadata bounds");

        Mav::WriteQueue queue;
        check(queue.push({1, 2, 3, 4}), "Queue push");
        check(queue.consume(2) && queue.front() == Mav::Bytes({3, 4}), "Partial write remainder");
        check(!queue.consume(3) && queue.size() == 2, "Invalid consumption rejected");
        check(queue.push(Mav::Bytes(Mav::WriteQueue::Limit - 2, 1)), "Exact queue bound");
        check(!queue.push({1}), "Backpressure queue limit");
        queue.clear();
        check(queue.size() == 0 && queue.front().empty(), "Disconnect queue cancellation");

        // Cross-check every generated field against the codec's bounds checks.
        for (const auto &info : Mav::catalog())
        {
            auto item = fixture(info.msgid);
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
                check(Mav::setField(item.payload, field, value, error), "Generated catalog field bounds");
            }
            check(!Mav::encode(info.msgid, 1, 2, false, item.payload, status).empty(), "Full catalog serialization");
        }
        std::cout << "PASS: codec, catalog, validation, parser, sender isolation, rates, scheduler, capacity, partial writes and queue bounds\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
