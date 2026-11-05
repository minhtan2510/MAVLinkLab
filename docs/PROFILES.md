# TX profiles

**Purpose:** portable TX configuration saved through **File → Import Profile… / Export Profile…**. The `.mavprofile` extension contains uncompressed **CBOR**, supported by Qt Core without an additional runtime dependency.

## Schema version 1

The root is a CBOR map with exactly these text keys:

| Key | CBOR type | Value |
| --- | --- | --- |
| `format` | Text string | `MAVLinkLab TX Profile` |
| `version` | Integer | `1` |
| `dialect` | Text string | `ardupilotmega` |
| `messages` | Array | Zero to 128 message maps, in TX dashboard order |

Each message is a map with exactly these keys:

| Key | CBOR type | Meaning |
| --- | --- | --- |
| `id` | Integer | Supported message ID in the compiled catalog |
| `sysid` | Integer | Sender System ID, 1–255 |
| `compid` | Integer | Sender Component ID, 0–255 |
| `version1` | Boolean | True for MAVLink 1; false for MAVLink 2 |
| `hz` | Integer or floating point | Finite target rate, 0.1–1000 Hz |
| `payload` | Byte string | Exact full-length payload at generated wire offsets |

**Validation:** the complete file must be at most **1 MiB** and contain a single CBOR value. Unknown/duplicate keys, trailing data, unsupported schema/dialect, incorrect types/ranges, unsupported message IDs, incorrect payload lengths and MAVLink 1 IDs above 255 are rejected. Validation follows the currently compiled MAVLink metadata. Same-length semantic changes between dialect revisions cannot be detected by this schema; use matching library definitions when exchanging profiles.

Payload bytes are preserved verbatim, including zero bytes and array contents. Import does not reinterpret numeric field semantics or convert protocol-controlled fields. The official serializer still owns HEARTBEAT `mavlink_version` when transmitting.

## Import behavior

- **Append** is **off** on every dialog opening and is not persisted.
- **Replace:** validate everything, cancel pending output, then replace the entire TX list. Already transmitted bytes cannot be recalled.
- **Append:** validate against the authoritative worker list and the 128-entry limit, then add messages in file order without deduplication. Existing entries retain their active state, counters and schedule; pending output is retained.
- Imported entries receive fresh internal keys and start **Stopped**, with zero counts and timing state. Runtime keys, sequence state, active flags, errors and counters are never exported.
- An empty profile replaces the list with an empty list, or appends nothing.
- Validation failure or cancellation leaves the current list unchanged.
- Import/export do not change UART settings, RX data, theme, or the startup restoration preference.

## Ownership and file I/O

`src/profile.*` owns the Qt Core CBOR adapter and strict schema validation. `src/core.*` remains Qt-free. `SerialWorker` serializes its current entries on export and commits a complete validated import in one worker operation, avoiding partial imports and stale GUI snapshots. The GUI reserves fresh keys while the operation is pending and writes exports using `QSaveFile` atomic replacement.

Input reads are capped at the file limit plus one byte. Profile files are small configuration artifacts, so their bounded file reads/writes run in the GUI thread; serial I/O and scheduling remain in the worker thread.

## Verification

- **profile:** exact-byte round trips, malformed CBOR/schema/types, duplicate/trailing data, bounds and failure atomicity.
- **profile-worker:** authoritative export, atomic Replace/Append, runtime reset, key bounds and PTY preservation/cancellation.
- **profile-ui:** menu/dialog flow, default Append reset, Replace/Append/cancel/error handling, export round trip and retained Light/Dark screenshots under `build/qa/profile-*.png`.

These tests do not establish physical UART delivery or native desktop file-dialog integration.
