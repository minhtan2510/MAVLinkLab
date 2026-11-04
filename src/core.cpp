#include "core.h"
#include "mavlink_enums.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace Mav
{
const std::vector<mavlink_message_info_t> &catalog()
{
    static const std::vector<mavlink_message_info_t> items = MAVLINK_MESSAGE_INFO;
    return items;
}

size_t typeSize(mavlink_message_type_t type)
{
    static constexpr size_t sizes[] = {1, 1, 1, 2, 2, 4, 4, 8, 8, 4, 8};
    return unsigned(type) < 11 ? sizes[type] : 0;
}

std::string typeName(mavlink_message_type_t type)
{
    static const char *names[] = {"char", "uint8_t", "int8_t", "uint16_t", "int16_t",
                                 "uint32_t", "int32_t", "uint64_t", "int64_t", "float", "double"};
    return unsigned(type) < 11 ? names[type] : "unknown";
}

/** @brief Parse a single primitive and preserve its exact wire bits. */
static uint64_t parseBits(const std::string &text, mavlink_message_type_t type)
{
    size_t used = 0;
    const auto width = typeSize(type);
    uint64_t bits = 0;
    if (type == MAVLINK_TYPE_FLOAT || type == MAVLINK_TYPE_DOUBLE)
    {
        const auto first = text.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
        {
            throw std::invalid_argument("Empty number");
        }
        const auto last = text.find_last_not_of(" \t\r\n");
        const char *begin = text.data() + first;
        const char *end = text.data() + last + 1;
        if (*begin == '+')
        {
            ++begin;
            if (begin == end)
            {
                throw std::invalid_argument("Unexpected trailing characters");
            }
        }
        if (type == MAVLINK_TYPE_FLOAT)
        {
            float value = 0.0f;
            const auto [ptr, ec] = std::from_chars(begin, end, value);
            if (ec == std::errc::result_out_of_range)
            {
                throw std::out_of_range("float overflow");
            }
            if (ec != std::errc() || ptr != end)
            {
                throw std::invalid_argument("Unexpected trailing characters");
            }
            if (!std::isfinite(value))
            {
                throw std::invalid_argument("Non-finite values are not accepted");
            }
            uint32_t raw = 0;
            std::memcpy(&raw, &value, sizeof(raw));
            bits = raw;
        }
        else
        {
            double value = 0.0;
            const auto [ptr, ec] = std::from_chars(begin, end, value);
            if (ec == std::errc::result_out_of_range)
            {
                throw std::out_of_range("double overflow");
            }
            if (ec != std::errc() || ptr != end)
            {
                throw std::invalid_argument("Unexpected trailing characters");
            }
            if (!std::isfinite(value))
            {
                throw std::invalid_argument("Non-finite values are not accepted");
            }
            std::memcpy(&bits, &value, sizeof(bits));
        }
        return bits;
    }
    else if (type == MAVLINK_TYPE_INT8_T || type == MAVLINK_TYPE_INT16_T ||
             type == MAVLINK_TYPE_INT32_T || type == MAVLINK_TYPE_INT64_T)
    {
        const auto enumVal = lookupEnumValue(text);
        if (enumVal.has_value())
        {
            const auto value = static_cast<int64_t>(*enumVal);
            if (width < 8 && (value < -(int64_t(1) << (width * 8 - 1)) ||
                              value > (int64_t(1) << (width * 8 - 1)) - 1))
            {
                throw std::out_of_range("Signed integer out of range");
            }
            return static_cast<uint64_t>(value);
        }
        const auto value = std::stoll(text, &used, 10);
        if (width < 8 && (value < -(int64_t(1) << (width * 8 - 1)) ||
                          value > (int64_t(1) << (width * 8 - 1)) - 1))
        {
            throw std::out_of_range("Signed integer out of range");
        }
        bits = static_cast<uint64_t>(value);
    }
    else
    {
        const auto enumVal = lookupEnumValue(text);
        if (enumVal.has_value())
        {
            bits = *enumVal;
            if (width < 8 && bits >= (uint64_t(1) << (width * 8)))
            {
                throw std::out_of_range("Unsigned integer out of range");
            }
            return bits;
        }
        if (text.find('-') != std::string::npos)
        {
            throw std::invalid_argument("Unsigned integer cannot be negative");
        }
        bits = std::stoull(text, &used, 10);
        if (width < 8 && bits >= (uint64_t(1) << (width * 8)))
        {
            throw std::out_of_range("Unsigned integer out of range");
        }
    }
    if (text.find_first_not_of(" \t\r\n", used) != std::string::npos)
    {
        throw std::invalid_argument("Unexpected trailing characters");
    }
    return bits;
}

bool setField(Bytes &payload, const mavlink_field_info_t &field,
              const std::string &text, std::string &error)
{
    error.clear();
    const size_t width = typeSize(field.type);
    const size_t length = field.array_length ? field.array_length : 1;
    if (!width || field.wire_offset + width * length > payload.size())
    {
        error = "Invalid field bounds";
        return false;
    }
    Bytes value(width * length, 0);
    try
    {
        if (field.type == MAVLINK_TYPE_CHAR && field.array_length)
        {
            if (text.size() > length)
            {
                throw std::out_of_range("Text exceeds byte array length");
            }
            std::copy(text.begin(), text.end(), value.begin());
        }
        else
        {
            std::vector<std::string> parts;
            std::istringstream input(text);
            std::string part;
            while (std::getline(input, part, ','))
            {
                parts.push_back(part);
            }
            if (parts.size() != length || (!text.empty() && text.back() == ','))
            {
                throw std::invalid_argument("Enter exactly " + std::to_string(length) + " value(s)");
            }
            for (size_t i = 0; i < length; ++i)
            {
                const uint64_t bits = parseBits(parts[i], field.type);
                for (size_t j = 0; j < width; ++j)
                {
                    value[i * width + j] = uint8_t(bits >> (8 * j));
                }
            }
        }
        std::copy(value.begin(), value.end(), payload.begin() + field.wire_offset);
        return true;
    }
    catch (const std::exception &exception)
    {
        error = exception.what();
        return false;
    }
}

std::string readField(const mavlink_message_t &message, const mavlink_field_info_t &field)
{
    const size_t width = typeSize(field.type);
    const size_t length = field.array_length ? field.array_length : 1;
    if (!width || field.wire_offset + width * length > MAVLINK_MAX_PAYLOAD_LEN)
    {
        return "Invalid metadata";
    }
    const auto *data = reinterpret_cast<const uint8_t *>(_MAV_PAYLOAD(&message));
    if (field.type == MAVLINK_TYPE_CHAR && field.array_length)
    {
        std::string text;
        for (size_t i = 0; i < length && field.wire_offset + i < message.len; ++i)
        {
            const uint8_t byte = data[field.wire_offset + i];
            if (!byte)
            {
                break;
            }
            text += char(byte);
        }
        return text;
    }
    std::ostringstream output;
    output << std::setprecision(17);
    for (size_t i = 0; i < length; ++i)
    {
        if (i)
        {
            output << ", ";
        }
        uint64_t bits = 0;
        for (size_t j = 0; j < width; ++j)
        {
            const size_t position = field.wire_offset + i * width + j;
            if (position < message.len)
            {
                bits |= uint64_t(data[position]) << (8 * j);
            }
        }
        if (field.type == MAVLINK_TYPE_FLOAT)
        {
            const uint32_t raw = uint32_t(bits);
            float value = 0.0f;
            std::memcpy(&value, &raw, sizeof(value));
            char buffer[32];
            const auto res = std::to_chars(buffer, buffer + sizeof(buffer), value);
            output << std::string_view(buffer, res.ptr - buffer);
        }
        else if (field.type == MAVLINK_TYPE_DOUBLE)
        {
            double value = 0.0;
            std::memcpy(&value, &bits, sizeof(value));
            char buffer[64];
            const auto res = std::to_chars(buffer, buffer + sizeof(buffer), value);
            output << std::string_view(buffer, res.ptr - buffer);
        }
        else if (field.type == MAVLINK_TYPE_INT8_T || field.type == MAVLINK_TYPE_INT16_T ||
                 field.type == MAVLINK_TYPE_INT32_T || field.type == MAVLINK_TYPE_INT64_T)
        {
            if (width < 8 && (bits & (uint64_t(1) << (width * 8 - 1))))
            {
                bits |= (~uint64_t(0)) << (width * 8);
            }
            int64_t value = 0;
            std::memcpy(&value, &bits, sizeof(value));
            output << value;
        }
        else
        {
            output << bits;
        }
    }
    return output.str();
}

Bytes encode(uint32_t id, uint8_t sysid, uint8_t compid, bool version1,
             const Bytes &payload, mavlink_status_t &status)
{
    const auto *entry = mavlink_get_msg_entry(id);
    if (!entry || payload.size() != entry->max_msg_len || (version1 && id > 255))
    {
        return {};
    }
    mavlink_message_t message{};
    message.msgid = id;
    message.len = uint8_t(payload.size());
    std::memcpy(_MAV_PAYLOAD_NON_CONST(&message), payload.data(), payload.size());
    status.flags = version1 ? MAVLINK_STATUS_FLAG_OUT_MAVLINK1 : 0;
    uint16_t size = 0;
    if (id == MAVLINK_MSG_ID_HEARTBEAT)
    {
        // The generated packer owns the non-writable mavlink_version constant.
        mavlink_heartbeat_t heartbeat{};
        mavlink_msg_heartbeat_decode(&message, &heartbeat);
        size = mavlink_msg_heartbeat_encode_status(sysid, compid, &status, &message, &heartbeat);
    }
    else
    {
        size = mavlink_finalize_message_buffer(&message, sysid, compid, &status,
                                              entry->min_msg_len, entry->max_msg_len, entry->crc_extra);
    }
    if (!size)
    {
        return {};
    }
    Bytes result(MAVLINK_MAX_PACKET_LEN);
    result.resize(mavlink_msg_to_send_buffer(result.data(), &message));
    return result;
}

std::vector<mavlink_message_t> Parser::feed(const Bytes &bytes)
{
    std::vector<mavlink_message_t> result;
    for (const uint8_t byte : bytes)
    {
        mavlink_message_t message{};
        mavlink_status_t report{};
        const auto framing = mavlink_frame_char_buffer(&buffer, &status, byte, &message, &report);
        if (framing == MAVLINK_FRAMING_OK)
        {
            result.push_back(message);
        }
        else if (framing == MAVLINK_FRAMING_BAD_CRC)
        {
            ++crcErrors;
        }
        else if (framing == MAVLINK_FRAMING_BAD_SIGNATURE)
        {
            ++signatureErrors;
        }
    }
    return result;
}

void Rate::add(int64_t now)
{
    hz(now);
    times.push_back(now);
}

double Rate::hz(int64_t now)
{
    while (!times.empty() && times.front() <= now - 1000000)
    {
        times.pop_front();
    }
    return static_cast<double>(times.size());
}

void Receiver::accept(const mavlink_message_t &message, int64_t now)
{
    const RxKey key{message.sysid, message.compid, message.msgid};
    if (!entries.count(key) && entries.size() >= MaxEntries)
    {
        ++overflow;
        return;
    }
    auto &entry = entries[key];
    entry.message = message;
    entry.last = now;
    ++entry.count;
    entry.rate.add(now);
}

bool validTx(const TxEntry &entry)
{
    const auto *metadata = mavlink_get_msg_entry(entry.id);
    return metadata && (!entry.version1 || entry.id <= 255) && std::isfinite(entry.hz) &&
           entry.hz >= 0.1 && entry.hz <= 1000 && entry.payload.size() == metadata->max_msg_len;
}

size_t frameSize(const TxEntry &entry)
{
    const auto *metadata = mavlink_get_msg_entry(entry.id);
    if (!metadata)
    {
        return 0;
    }
    return (entry.version1 ? metadata->min_msg_len : metadata->max_msg_len) +
           (entry.version1 ? 8 : 12);
}

bool fitsLink(const std::vector<TxEntry> &entries, const TxEntry &candidate,
              int baud, double bitsPerByte)
{
    if (!validTx(candidate) || baud <= 0 || !std::isfinite(bitsPerByte) || bitsPerByte < 7)
    {
        return false;
    }
    double bytesPerSecond = frameSize(candidate) * candidate.hz;
    for (const auto &entry : entries)
    {
        if (entry.key != candidate.key && entry.active)
        {
            bytesPerSecond += frameSize(entry) * entry.hz;
        }
    }
    return bytesPerSecond * bitsPerByte <= baud;
}

bool due(TxEntry &entry, int64_t now)
{
    if (!entry.active || !validTx(entry) || now < entry.deadline)
    {
        return false;
    }
    entry.deadline = now + static_cast<int64_t>(1000000.0 / entry.hz);
    return true;
}

bool WriteQueue::push(const Bytes &bytes)
{
    if (bytes.empty() || bytes.size() > Limit - pending)
    {
        return false;
    }
    frames.push_back(bytes);
    pending += bytes.size();
    return true;
}

Bytes WriteQueue::front() const
{
    return frames.empty() ? Bytes{} : Bytes(frames.front().begin() + offset, frames.front().end());
}

bool WriteQueue::consume(size_t count)
{
    if (frames.empty() || count > frames.front().size() - offset)
    {
        return false;
    }
    pending -= count;
    offset += count;
    if (offset == frames.front().size())
    {
        frames.pop_front();
        offset = 0;
    }
    return true;
}

void WriteQueue::clear()
{
    frames.clear();
    offset = 0;
    pending = 0;
}

size_t WriteQueue::size() const
{
    return pending;
}
}
