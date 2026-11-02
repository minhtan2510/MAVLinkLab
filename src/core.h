#pragma once

#include <cstddef>
#include <ardupilotmega/mavlink.h>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace Mav
{
using Bytes = std::vector<uint8_t>;

/** @brief Return the generated dialect catalog in message ID order. */
const std::vector<mavlink_message_info_t> &catalog();
/** @brief Return a readable MAVLink primitive type name. */
std::string typeName(mavlink_message_type_t type);
/** @brief Return the wire width of a primitive type, or zero if unsupported. */
size_t typeSize(mavlink_message_type_t type);
/** @brief Parse an editor value into payload bytes at the official wire offset.
 * @param payload Full zero-initialized payload buffer.
 * @param field Generated field metadata.
 * @param text Scalar, comma-separated numeric array, or UTF-8 character array.
 * @param error Validation error on failure.
 * @return True if the value is valid and stored.
 */
bool setField(Bytes &payload, const mavlink_field_info_t &field,
              const std::string &text, std::string &error);
/** @brief Decode a field using official wire offsets; absent trailing bytes are zero. */
std::string readField(const mavlink_message_t &message, const mavlink_field_info_t &field);
/** @brief Serialize an unsigned frame through the official MAVLink finalizer.
 * @warning Success indicates serialization only, not device delivery.
 */
Bytes encode(uint32_t id, uint8_t sysid, uint8_t compid, bool version1,
             const Bytes &payload, mavlink_status_t &status);

struct Parser
{
    mavlink_message_t buffer{};
    mavlink_status_t status{};
    uint64_t crcErrors = 0;
    uint64_t signatureErrors = 0;
    /** @brief Parse an arbitrary fragment while preserving frame state. */
    std::vector<mavlink_message_t> feed(const Bytes &bytes);
};

struct Rate
{
    std::deque<int64_t> times;
    /** @brief Record an event and bound storage to the measurement window. */
    void add(int64_t now);
    /** @brief Return events per second in the trailing one-second window. */
    double hz(int64_t now);
};

struct RxEntry
{
    mavlink_message_t message{};
    uint64_t count = 0;
    int64_t last = 0;
    Rate rate;
};
using RxKey = std::tuple<uint32_t, uint8_t, uint32_t>;

struct Receiver
{
    // Bounded to prevent untrusted sender IDs from exhausting memory.
    static constexpr size_t MaxEntries = 2048;
    std::map<RxKey, RxEntry> entries;
    uint64_t overflow = 0;
    /** @brief Update an independent system/component/message stream. */
    void accept(const mavlink_message_t &message, int64_t now);
};

struct TxEntry
{
    uint64_t key = 0;
    uint32_t id = 0;
    uint8_t sysid = 255;
    uint8_t compid = 190;
    bool version1 = false;
    double hz = 1.0;
    Bytes payload;
    bool active = false;
    int64_t deadline = 0;
    uint64_t count = 0;
    Rate measured;
    std::string error;
};
/** @brief Validate a TX entry including payload and MAVLink version limits. */
bool validTx(const TxEntry &entry);
/** @brief Return conservative wire bytes per frame before zero trimming. */
size_t frameSize(const TxEntry &entry);
/** @brief Check offered load against baud rate and serial framing bits. */
bool fitsLink(const std::vector<TxEntry> &entries, const TxEntry &candidate,
              int baud, double bitsPerByte);
/** @brief Advance a due deadline without catch-up bursts. */
bool due(TxEntry &entry, int64_t now);

class WriteQueue
{
public:
    static constexpr size_t Limit = 16384;
    /** @brief Enqueue a whole frame only if the application queue has capacity. */
    bool push(const Bytes &bytes);
    /** @brief Return the remaining bytes for the head frame. */
    Bytes front() const;
    /** @brief Consume accepted bytes; reject invalid counts without mutation. */
    bool consume(size_t count);
    /** @brief Drop all application pending bytes. */
    void clear();
    /** @brief Return pending byte count. */
    size_t size() const;
private:
    std::deque<Bytes> frames;
    size_t offset = 0;
    size_t pending = 0;
};
}
