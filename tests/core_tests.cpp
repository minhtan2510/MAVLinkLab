#include "core.h"
#include "mavlink_enums.h"
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
        check(Mav::setField(payload, integer, "MAV_PARAM_TYPE_REAL32", error), "Enum symbolic name resolution");
        check(payload[0] == 9, "Enum resolved to 9 in wire payload");
        check(Mav::lookupEnumName(22, "param_type", 9) == "MAV_PARAM_TYPE_REAL32", "Reverse enum lookup");
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

        // Test Parameter Protocol decoding and collection
        {
            Mav::Receiver receiver;
            const uint32_t sysid = 1;
            const uint8_t compid = 154; // Gimbal
            const uint16_t totalParams = 3;

            mavlink_message_t msg1, msg2, msg3;
            mavlink_param_union_t u;

            mavlink_msg_param_value_pack(sysid, compid, &msg1, "GMB_PITCH_P", 1.5f, MAV_PARAM_TYPE_REAL32, totalParams, 0);

            u.param_uint32 = 115200;
            mavlink_msg_param_value_pack(sysid, compid, &msg2, "GMB_BAUD", u.param_float, MAV_PARAM_TYPE_UINT32, totalParams, 1);

            u.param_uint8 = 1;
            mavlink_msg_param_value_pack(sysid, compid, &msg3, "GMB_SYSID", u.param_float, MAV_PARAM_TYPE_UINT8, totalParams, 2);

            receiver.accept(msg1, 1000);
            receiver.accept(msg2, 2000);
            receiver.accept(msg3, 3000);

            const auto devKey = std::make_pair(sysid, compid);
            check(receiver.params.count(devKey) == 1, "Param collection exists for device");
            const auto &col = receiver.params[devKey];
            check(col.totalCount == 3, "Total param count matches");
            check(col.byId.size() == 3, "All 3 distinct params preserved");
            check(col.byId.count("GMB_PITCH_P") == 1, "GMB_PITCH_P exists");
            check(col.byId.count("GMB_BAUD") == 1, "GMB_BAUD exists");
            check(col.byId.count("GMB_SYSID") == 1, "GMB_SYSID exists");

            // Format check
            check(Mav::formatParamValue(col.byId.at("GMB_BAUD").value, MAV_PARAM_TYPE_UINT32) == "115200", "UINT32 param formatting");
            check(Mav::formatParamValue(col.byId.at("GMB_SYSID").value, MAV_PARAM_TYPE_UINT8) == "1", "UINT8 param formatting");

            // Encode check
            float encoded = 0;
            check(Mav::encodeParamValue("2.25", MAV_PARAM_TYPE_REAL32, encoded) && encoded == 2.25f, "Float param encoding");
            check(Mav::encodeParamValue("57600", MAV_PARAM_TYPE_UINT32, encoded), "UINT32 param encoding");
            check(Mav::formatParamValue(encoded, MAV_PARAM_TYPE_UINT32) == "57600", "UINT32 roundtrip format");
        }

        // 10. Throughput tracking (sliding window ByteRate)
        {
            Mav::ByteRate rate;
            check(rate.bytesPerSecond(1000000) == 0.0, "Initial ByteRate is zero");
            rate.add(500, 1000000);
            rate.add(1000, 1500000);
            check(rate.bytesPerSecond(1500000) == 1500.0, "ByteRate accumulates inside sliding window");
            // At 2000001, sample at 1000000 expires (> 1s old)
            check(rate.bytesPerSecond(2000001) == 1000.0, "ByteRate expires old samples outside one second");
            check(rate.bytesPerSecond(2500001) == 0.0, "ByteRate returns zero after full window expiration");
            rate.add(300, 3000000);
            rate.clear();
            check(rate.bytesPerSecond(3000000) == 0.0, "ByteRate clear resets byte count");
        }

        // 11. Throughput estimation (TxEstimate)
        {
            Mav::TxEntry entry;
            entry.id = 0; // HEARTBEAT, min_msg_len = 9, max_msg_len = 9
            entry.version1 = false;
            entry.payload.assign(9, 0);
            entry.hz = 10.0;
            entry.active = true;
            check(Mav::validTx(entry), "Heartbeat TX valid");

            // Frame size: 9 payload + 12 v2 header = 21 bytes
            const size_t size = Mav::frameSize(entry);
            check(size == 21, "Heartbeat v2 frame size is 21 bytes");
            const double entryBps = Mav::estimateEntryThroughput(entry);
            check(entryBps == 210.0, "Heartbeat at 10 Hz is 210 B/s");

            std::vector<Mav::TxEntry> entries{entry};
            const auto est = Mav::estimateTxThroughput(entries, 115200, 10.0, true);
            check(est.totalBytesPerSec == 210.0, "TxEstimate totalBytesPerSec matches");
            check(est.linkCapacityBps == 11520.0, "115200 baud capacity at 10 bits/byte is 11520 B/s");
            check(est.utilizationPercent > 1.8 && est.utilizationPercent < 1.9, "Utilization ~1.82%");
            check(est.fits == true, "Fits within link capacity");

            // Overloaded link scenario
            entry.hz = 600.0; // 600 * 21 = 12600 B/s > 11520 B/s
            entries[0] = entry;
            const auto overEst = Mav::estimateTxThroughput(entries, 115200, 10.0, true);
            check(overEst.fits == false, "Overloaded link detected");
            check(overEst.utilizationPercent > 100.0, "Utilization exceeds 100%");

            // Format helper
            check(Mav::formatByteRate(500.0) == "500 B/s", "Format B/s");
            check(Mav::formatByteRate(2500.0) == "2.5 KB/s", "Format KB/s");
            check(Mav::formatByteRate(2000000.0) == "2.00 MB/s", "Format MB/s");
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
