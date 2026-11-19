# Public API Reference

**Audience:** Developers integrating, extending, or maintaining MAVLinkLab components.

This document describes the internal C++ programming interfaces across `src/core.*`, `src/worker.*`, and `src/profile.*`. These are in-tree C++17 interfaces rather than a public shared library SDK with ABI stability guarantees.

---

## 1. Core Codec and Metadata (`Mav` Namespace)

The core telemetry engine resides in namespace **`Mav`** within [src/core.h](../src/core.h) and [src/core.cpp](../src/core.cpp). It has **strictly zero dependencies on Qt** and can be compiled on headless embedded systems.

`Bytes` is defined as `std::vector<uint8_t>`. Callers own returned payload and frame buffers. Core objects contain no internal mutexes; instances must be confined to a single owning thread or synchronized externally.

### 1.1. Metadata and Reflection APIs

| Function | Signature | Description and Contract |
| --- | --- | --- |
| `catalog()` | `const std::vector<mavlink_message_info_t>& catalog()` | Returns a reference to the static dialect message catalog sorted by message ID. Process-wide lifetime; read-only. |
| `typeName()` | `std::string typeName(mavlink_message_type_t type)` | Returns the human-readable MAVLink primitive type name (e.g. `"uint8_t"`, `"float"`). Returns `"unknown"` if unsupported. |
| `typeSize()` | `size_t typeSize(mavlink_message_type_t type)` | Returns the byte width of a primitive type. Returns `0` if unsupported. |
| `setField()` | `bool setField(Bytes &payload, const mavlink_field_info_t &field, const std::string &text, std::string &error)` | Parses human-readable string `text` into binary bytes at `field.wire_offset` inside `payload`. Returns `false` and populates `error` on invalid input. Leaves `payload` unmodified on failure. |
| `readField()` | `std::string readField(const mavlink_message_t &message, const mavlink_field_info_t &field)` | Decodes a field into a formatted string using official wire offsets. Missing trailing bytes in truncated MAVLink 2 frames are treated as zero. |
| `readFieldNumeric()` | `std::optional<double> readFieldNumeric(const mavlink_message_t &message, const mavlink_field_info_t &field, size_t arrayIndex = 0)` | Safely extracts any primitive integer or float field as a `double` for plotting or telemetry analysis. Returns `std::nullopt` for strings, non-finite values (`NaN`, `Inf`), or out-of-bounds indices. |
| `encode()` | `Bytes encode(uint32_t id, uint8_t sysid, uint8_t compid, bool version1, const Bytes &payload, mavlink_status_t &status)` | Serializes a MAVLink frame using the official C finalizer. Returns an empty vector if the message ID is unknown, payload length is invalid, or MAVLink 1 ID exceeds 255. Callers maintain `status` across calls to preserve packet sequence numbers. |

### 1.2. In-Memory Codec Example

```cpp
#include "core.h"
#include <cassert>

int main()
{
    const auto *meta = mavlink_get_msg_entry(MAVLINK_MSG_ID_HEARTBEAT);
    assert(meta != nullptr);

    Mav::Bytes payload(meta->max_msg_len, 0);
    std::string error;

    const auto *typeField = mavlink_get_field_info(meta, "type");
    assert(typeField != nullptr);
    bool ok = Mav::setField(payload, *typeField, "2", error); // MAV_TYPE_QUADROTOR
    assert(ok);

    mavlink_status_t status{};
    Mav::Bytes frame = Mav::encode(MAVLINK_MSG_ID_HEARTBEAT, 1, 1, false, payload, status);
    assert(!frame.empty());

    Mav::Parser parser;
    std::vector<mavlink_message_t> messages = parser.feed(frame);
    assert(messages.size() == 1);
    assert(messages[0].msgid == MAVLINK_MSG_ID_HEARTBEAT);

    return 0;
}
```

---

## 2. Parser, Rate Estimator, and Sample Ring Buffer

### 2.1. Stream Parser (`Mav::Parser`)

`Mav::Parser` maintains incremental state across arbitrary serial read fragments:

```cpp
struct Parser
{
    mavlink_message_t buffer{};
    mavlink_status_t status{};
    uint64_t crcErrors = 0;
    uint64_t signatureErrors = 0;

    std::vector<mavlink_message_t> feed(const Bytes &bytes);
};
```

- `feed(const Bytes &bytes)`: Consumes an incoming chunk of arbitrary size. Emits complete, valid frames. Preserves partial frame state across invocations. Increments `crcErrors` on checksum mismatches and `signatureErrors` on invalid signatures.

### 2.2. Rate Tracking (`Mav::Rate` and `Mav::ByteRate`)

- **`Mav::Rate`:** Tracks message frequency (Hz) using a microsecond deque sliding window. `add(int64_t now)` records an event; `hz(int64_t now)` purges timestamps older than 1.0 second and returns the event count.
- **`Mav::ByteRate`:** Tracks physical wire throughput (bytes/second) over a 1.0-second sliding window ring buffer without periodic interval resets:
  ```cpp
  struct ByteRate
  {
      void add(size_t count, int64_t now);
      double bytesPerSecond(int64_t now);
      void clear();
  };
  ```

### 2.3. Telemetry Plotter Ring Buffer (`Mav::PlotBuffer`)

A bounded, circular ring buffer designed for real-time waveform visualization with zero runtime heap allocations:

```cpp
class PlotBuffer
{
public:
    static constexpr size_t DefaultCapacity = 2048;

    explicit PlotBuffer(size_t capacity = DefaultCapacity);
    void push(int64_t timeUs, double value);
    void clear();
    size_t size() const;
    bool empty() const;
    size_t capacity() const;
    PlotSample at(size_t index) const; // 0 is oldest, size()-1 is newest
    std::pair<double, double> range() const; // {min, max}
    std::vector<PlotSample> samples() const;
};
```

- Overwrites oldest samples when capacity (2048) is reached.
- Non-finite values (`NaN`, `Infinity`) are automatically rejected by `push()`.

---

## 3. Parameter Protocol Engine

### 3.1. Types and Collections

```cpp
struct ParamItem
{
    std::string id;        // Onboard parameter identifier (up to 16 chars)
    float value = 0.0f;    // Wire-encoded 32-bit float value
    uint8_t type = 0;      // MAV_PARAM_TYPE (e.g. MAV_PARAM_TYPE_UINT8, MAV_PARAM_TYPE_REAL32)
    uint16_t index = 0;    // Parameter index reported by target node
    uint16_t count = 0;    // Total parameters reported by target node
};

struct ParamCollection
{
    uint16_t totalCount = 0;
    std::map<std::string, ParamItem> byId;
    std::map<uint16_t, std::string> byIndex;
};
```

### 3.2. Parameter Conversion APIs

| Function | Signature | Description |
| --- | --- | --- |
| `paramTypeName()` | `std::string paramTypeName(uint8_t type)` | Converts numeric `MAV_PARAM_TYPE` to string (e.g. `"UINT8"`, `"INT32"`, `"REAL32"`). |
| `formatParamValue()` | `std::string formatParamValue(float wireValue, uint8_t type)` | Decodes a 32-bit float wire representation into formatted string using union type-punning (`mavlink_param_union_t`). |
| `encodeParamValue()` | `bool encodeParamValue(const std::string &text, uint8_t type, float &outWireValue)` | Parses user text and encodes it into 32-bit wire float using `mavlink_param_union_t`. Rejects out-of-range values. |

---

## 4. Transmission Model and Scheduling

### 4.1. Transmission Entry (`Mav::TxEntry`)

```cpp
struct TxEntry
{
    uint64_t key = 0;         // Monotonically increasing GUI identifier (>= 1)
    uint32_t id = 0;          // MAVLink message ID
    uint8_t sysid = 255;      // Sender System ID (1-255)
    uint8_t compid = 190;     // Sender Component ID (0-255)
    bool version1 = false;    // True for MAVLink 1; false for MAVLink 2
    double hz = 1.0;          // Target rate in Hertz (0.1 to 1000)
    Bytes payload;            // Full wire payload buffer matching max_msg_len
    bool active = false;      // True if periodic scheduling is enabled
    int64_t deadline = 0;     // Next execution deadline in monotonic microseconds
    uint64_t count = 0;       // Total frames handed off to serial output
    Rate measured;            // Trailing event window for rate calculation
    std::string error;        // Last error message
};
```

### 4.2. Scheduling Functions

| Function | Signature | Contract |
| --- | --- | --- |
| `validTx()` | `bool validTx(const TxEntry &entry)` | Validates message ID presence, exact payload length match, finite frequency in $[0.1, 1000]$, and MAVLink 1 ID $\le 255$. |
| `frameSize()` | `size_t frameSize(const TxEntry &entry)` | Calculates maximum wire frame size: v1 payload + 8 bytes, v2 payload + 12 bytes. |
| `fitsLink()` | `bool fitsLink(const std::vector<TxEntry> &entries, const TxEntry &candidate, int baud, double bitsPerByte)` | Computes collective offered bit rate against available baud rate. |
| `due()` | `bool due(TxEntry &entry, int64_t now)` | If `active` and deadline reached, advances `deadline += period` isochronously and returns `true`. Resynchronizes deadline if overdue beyond `max(20ms, 2 * period)` to prevent catch-up bursts. |
| `estimateTxThroughput()` | `TxEstimate estimateTxThroughput(const std::vector<TxEntry> &entries, int baud, double bitsPerByte = 10.0, bool activeOnly = true)` | Calculates planned byte rate, bit rate, and channel utilization percentage. |
| `formatByteRate()` | `std::string formatByteRate(double bytesPerSec)` | Formats throughput into human-readable string (e.g. `"350 B/s"`, `"1.25 KB/s"`). |

---

## 5. Bounded Output Queue (`Mav::WriteQueue`)

`WriteQueue` manages buffered frames waiting to be written to the serial device:

```cpp
class WriteQueue
{
public:
    static constexpr size_t Limit = 16384; // 16 KiB capacity limit

    bool push(const Bytes &bytes);   // Rejects if empty or would exceed Limit
    Bytes front() const;             // Returns unwritten remainder of head frame
    bool consume(size_t count);      // Pops fully written frames; advances offset
    void clear();                    // Drops all queued frames
    size_t size() const;             // Current pending byte count
};
```

---

## 6. SerialWorker Interface (`src/worker.h`)

`SerialWorker` executes in its own `QThread` and manages `QSerialPort`:

### 6.1. Public Slots

| Slot | Parameters | Behavior |
| --- | --- | --- |
| `initialize()` | None | Allocates `QSerialPort` and 1 ms scheduler `QTimer` inside worker thread. |
| `openPort()` | `PortConfig config` | Closes previous session, configures framing, opens port, deasserts modem control lines (if enabled), and purges buffers. Emits `stateChanged("Connected", true)`. |
| `closePort()` | None | Halts periodic TX, purges buffers, closes port, and emits `stateChanged("Disconnected", false)`. |
| `saveEntry()` | `Mav::TxEntry entry` | Validates and stores entry in stopped state (`active = false`). Discards pending queue. |
| `removeEntry()` | `quint64 key` | Removes entry by key and discards pending queue. |
| `setActive()` | `quint64 key, bool active` | Sets entry transmission state. If starting, sets immediate deadline. |
| `setRate()` | `quint64 key, double hz` | Dynamically updates target frequency for an existing entry. |
| `sendOnce()` | `quint64 key` | Serializes and enqueues a single frame immediately without activating periodic schedule. |
| `sendRawMessage()` | `mavlink_message_t msg` | Enqueues raw pre-encoded MAVLink frame directly. |
| `requestParameters()` | `uint8_t sys, uint8_t comp` | Transmits `PARAM_REQUEST_LIST` (msgid 21) to request all parameters from target. |
| `setParameter()` | `uint8_t sys, uint8_t comp, QString id, float val, uint8_t type` | Transmits `PARAM_SET` (msgid 23) to modify an individual parameter. |
| `runAll()` | None | Activates all valid configured TX entries simultaneously. |
| `stopAll()` | None | Halts periodic TX on all entries and clears pending queue. |
| `deleteAll()` | None | Stops TX, clears pending queue, and deletes all configured TX entries. |
| `importProfile()` | `QByteArray data, bool append, quint64 minKey` | Atomically validates and applies CBOR profile. |
| `exportProfile()` | None | Serializes active TX entries into CBOR format and emits snapshot. |
| `clearStatistics()` | None | Resets RX statistics, CRC error counters, and byte rate accumulators. |
| `requestSnapshot()` | None | Constructs an immutable `SnapshotPtr` and emits `snapshotReady()`. |
| `setAutoReconnect()`| `bool enabled` | Configures auto-reconnect behavior on unexpected link loss. |
| `setDeassertControlLines()`| `bool enabled` | Configures DTR/RTS modem deassertion on subsequent connections. |

### 6.2. Signals and Snapshots

- `stateChanged(QString stateText, bool connected)`: Emitted on connection state changes (`"Disconnected"`, `"Connecting"`, `"Connected"`, `"Reconnecting"`).
- `failure(QString reason)`: Emitted on transmission errors, port errors, or queue overruns.
- `snapshotReady(SnapshotPtr snapshot)`: Delivers an immutable `std::shared_ptr<const Snapshot>` to the GUI thread containing:
  - `rx`: Vector of `RxView` (message, count, Hz, age).
  - `tx`: Vector of `TxView` (entry, measuredHz, estimatedBps).
  - `params`: Map of parameter collections grouped by `(sysid, compid)`.
  - `connected`: Boolean link status.
  - `crcErrors`, `overflow`: Cumulative counters.
  - `rxBytesPerSecond`, `txBytesPerSecond`: Measured throughput.
  - `estimatedTxBps`, `estimatedTxUtilization`: Planned link load.
  - `pending`: Unwritten bytes in application and Qt buffers.

---

## 7. Profile Serialization (`Profile` Namespace)

Located in [src/profile.h](../src/profile.h) and [src/profile.cpp](../src/profile.cpp):

```cpp
namespace Profile
{
    inline constexpr qsizetype MaxFileSize = 1024 * 1024; // 1 MiB
    inline constexpr size_t MaxMessages = 128;

    QByteArray encode(const std::vector<Mav::TxEntry> &entries, QString &error);
    bool decode(const QByteArray &data, std::vector<Mav::TxEntry> &entries, QString &error);
}
```

- Serializes TX configurations into CBOR Schema 1 format (`format`, `version = 1`, `dialect = "ardupilotmega"`, `messages`).
- Strict schema validation: Unknown keys, trailing bytes, or payload length discrepancies trigger atomic rejection without modifying active entries.
- Runtime state (sequence numbers, active flags, counters, deadlines) is strictly excluded from serialization.
