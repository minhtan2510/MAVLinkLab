# API reference

**Đối tượng:** developer mở rộng application hoặc tái sử dụng core. Đây là C++ interfaces nội bộ trong repository, không phải network API hay SDK có cam kết ABI ổn định. Source chuẩn là [core.h](../src/core.h), [core.cpp](../src/core.cpp), [worker.h](../src/worker.h), [worker.cpp](../src/worker.cpp).

## Core codec và metadata

Các API sau thuộc namespace **`Mav`**, không phụ thuộc Qt. `Bytes` là `std::vector<uint8_t>`; caller sở hữu payload/frame trả về. Core objects không có synchronization nội bộ; dùng trong một owning thread hoặc tự đồng bộ ở caller.

| API | Contract và lỗi |
| --- | --- |
| `catalog()` | Trả `const std::vector<mavlink_message_info_t>&` tới static official catalog; lifetime toàn process, không sửa nội dung |
| `typeName(type)` / `typeSize(type)` | Tên primitive và wire width; unsupported type trả `"unknown"` / `0` |
| `setField(payload, field, text, error)` | Ghi tại `field.wire_offset`; `payload` phải được cấp đủ bytes trước. Trả `false` và reason khi invalid bounds/value; payload không đổi nếu validation thất bại; clear `error` mỗi lần gọi |
| `readField(message, field)` | Trả formatted string; absent trailing bytes đọc như zero; char array dừng tại NUL hoặc message length. Invalid metadata bounds trả `"Invalid metadata"` |
| `encode(id, sysid, compid, version1, payload, status)` | Trả wire frame; empty vector nếu ID không có metadata, payload size khác `max_msg_len`, MAVLink 1 ID >255 hoặc finalization thất bại. Caller giữ `status` giữa lần gửi để giữ sequence. Không thực hiện I/O |

**Input của `setField`:** integer decimal trong range của type, finite float/double, numeric arrays có đúng số phần tử comma-separated. Char arrays nhận raw UTF-8 bytes tối đa `array_length`; zero-fill phần còn lại, không đảm bảo NUL cuối nếu text lấp đầy array. API không kiểm tra enum semantics, units hay tính hợp lệ theo application protocol.

**`encode`:** worker dùng zero-initialized status, không cấu hình signing. API chuyển status cho official library; caller tái sử dụng core phải tự quản lý status/signing pointers nếu cấu hình khác. HEARTBEAT đi qua generated packer để giữ `mavlink_version=3`; không dùng field này để chọn MAVLink wire version.

### Ví dụ encode và parse HEARTBEAT trong memory

```cpp
#include "core.h"

/** @brief Verify one in-memory HEARTBEAT codec round trip. */
int main()
{
    const auto *metadata = mavlink_get_msg_entry(MAVLINK_MSG_ID_HEARTBEAT);
    if (!metadata)
    {
        return 1;
    }
    Mav::Bytes payload(metadata->max_msg_len, 0);
    mavlink_status_t status{};
    const auto frame = Mav::encode(MAVLINK_MSG_ID_HEARTBEAT, 1, 42, false,
                                   payload, status);
    if (frame.empty())
    {
        return 1;
    }
    Mav::Parser parser;
    const auto messages = parser.feed(frame);
    return messages.size() == 1 && messages[0].compid == 42 ? 0 : 1;
}
```

Ví dụ chỉ kiểm tra codec; không connect UART hoặc xác nhận delivery. Trong application, include/link qua target `mavcore` để nhận cả include paths và `MAVLINK_USE_MESSAGE_INFO` từ CMake.

## Parser và RX storage

| Interface | Input / output / side effects |
| --- | --- |
| `Parser::feed(const Bytes&)` | Nhận arbitrary chunk, trả vector complete valid frames; giữ state cho frame chưa đủ bytes. `crcErrors` và `signatureErrors` tích lũy theo kết quả official parser |
| `Rate::add(int64_t now)` | Ghi một event, loại events quá cửa sổ; `now` là monotonic microseconds |
| `Rate::hz(int64_t now)` | Loại timestamps ≤ `now - 1000000`, trả số events còn lại; lời gọi có mutation |
| `Receiver::accept(message, now)` | Cập nhật `entries[RxKey]`; `RxKey` gồm sysid/compid/msgid. Identity mới khi đủ 2048 slots bị bỏ và tăng `overflow` |

`RxEntry` chứa latest `message`, cumulative `count`, `last` (microseconds), và `Rate`. Reset bằng value initialization, ví dụ `parser = {}; receiver = {};`; reset parser giữa fragmented reads sẽ làm mất frame đang nhận.

## TX model và scheduling

| `TxEntry` field | Ý nghĩa |
| --- | --- |
| `key` | Stable application identity; GUI cấp key tăng dần từ 1, worker không tự tạo key |
| `id`, `sysid`, `compid`, `version1` | Message ID, sender identity, wire version; defaults sender 255/190, MAVLink 2 |
| `hz`, `payload` | Target rate và fixed wire-layout payload; default 1 Hz |
| `active`, `deadline` | Scheduling state; deadline theo monotonic microseconds |
| `count`, `measured`, `error` | Complete frames handed to Qt output, trailing rate events, last entry error |

| API | Contract |
| --- | --- |
| `validTx(entry)` | Metadata tồn tại, payload đúng `max_msg_len`, finite Hz trong [0.1, 1000], MAVLink 1 ID ≤255; không validate từng field hay sender semantics |
| `frameSize(entry)` | Conservative bytes trước MAVLink 2 zero trimming: v1 minimum payload +8, v2 maximum payload +12; unknown ID trả 0 |
| `fitsLink(entries, candidate, baud, bitsPerByte)` | Validate candidate và so offered bytes/s × framing bits với baud. Cộng các entries active khác key; không tự đo physical throughput |
| `due(entry, now)` | Nếu active/valid và đến deadline: cập nhật deadline thành `now + period`, trả true; không tăng count hay encode |

## Bounded output queue

`WriteQueue` sở hữu copies của queued frames, giới hạn **16384 bytes**.

| API | Return / behavior |
| --- | --- |
| `push(bytes)` | False nếu empty hoặc không đủ capacity; không partial enqueue |
| `front()` | Copy phần còn lại của head frame; empty nếu queue trống |
| `consume(count)` | False nếu không có head hoặc count vượt phần còn lại; success giảm pending, pop frame khi hết |
| `clear()` / `size()` | Drop toàn application pending bytes / trả remaining byte count |

Queue không sở hữu QSerialPort. Clear queue không thu hồi bytes đã chuyển sang Qt/kernel; worker quản lý bước clear output riêng.

## SerialWorker configuration và lifecycle

**Thread contract:** constructor không tạo port/timer. Gọi `initialize()` **một lần** trong owning thread trước các I/O/snapshot slots. Sau `moveToThread`, gửi command qua queued invocation. Không dùng lại worker sau khi thread đã kết thúc.

`PortConfig` được copy theo command:

| Field | Default | Ghi chú |
| --- | --- | --- |
| `port` | Empty QString | Device/PTY path hoặc port name; GUI trim trước gửi |
| `baud` | 115200 | GUI chặn ngoài 1..12000000; QSerialPort/driver quyết định hỗ trợ thực tế |
| `data` | `Data8` | GUI có 8/7/6/5; binary MAVLink nên dùng 8 |
| `parity` | `NoParity` | Qt enum; GUI có None/Even/Odd/Mark/Space |
| `stop` | `OneStop` | GUI có 1/2; worker tính cả `OneAndHalfStop` nếu API caller cung cấp |
| `flow` | `NoFlowControl` | Qt enum; GUI có None/Hardware RTS/CTS/Software XON/XOFF |

| Slot | Side effects / error behavior |
| --- | --- |
| `initialize()` | Tạo QSerialPort và 1 ms precise timer, bắt đầu clock/timer; không idempotent |
| `openPort(config)` | Đóng session cũ, configure/open mới; success reset RX state/rates, failure phát `Error` + reason; không auto TX |
| `closePort()` | Stop all, discard shared output, đóng port, reset parser/sequence state; entries và RX storage không bị xóa trực tiếp |
| `saveEntry(entry)` | Reject invalid entry qua `failure`; add tối đa 128, hoặc replace cùng key. Luôn lưu stopped, clear entry error; replace giữ count cũ và discard shared pending output |
| `removeEntry(key)` | Discard shared output rồi xóa key nếu tồn tại; kể cả key không tồn tại vẫn discard output |
| `setActive(key, true)` | Require open port + capacity; failure ghi entry error và phát reason; success đặt immediate deadline. Không tìm thấy key thì không làm gì |
| `setActive(key, false)` | Stop key tìm thấy và discard shared pending output |
| `sendOnce(key)` | Require open port; serialize/enqueue/pump một frame, không bật periodic. Key không tồn tại thì không làm gì |
| `stopAll()` | Stop mọi entries và discard shared output |
| `clearStatistics()` | Reset Receiver/Parser và RX/TX byte accumulators; không reset TX frame counts/entries |
| `requestSnapshot()` | Phát immutable snapshot; consume/reset byte accumulators và cập nhật measurement window |

**Batch TX:** `runAll()` checks the complete periodic load against the open link before activating any additional entry; rejection leaves active states unchanged and emits `failure`. `deleteAll()` stops all entries, discards pending output and clears the TX list.

Các slots trả `void`; việc command đã được enqueue không chứng minh operation thành công. Dùng signals/snapshot để phản ánh kết quả. Khi queue full hoặc serialization thất bại, affected entry bị stopped và phát `"TX queue full or serialization failed; periodic TX stopped"`.

## Signals và snapshots

| Signal | Contract |
| --- | --- |
| `stateChanged(QString, bool)` | State text gồm Disconnected/Connecting/Connected/Error; bool phản ánh connected state do worker phát |
| `failure(QString)` | Reason cho lỗi; không phải mọi lỗi đều đóng port, ví dụ capacity rejection |
| `snapshotReady(SnapshotPtr)` | Shared immutable data; caller có thể giữ snapshot sau khi slot kết thúc |

`Snapshot` gồm `rx` (`RxView`: message/count/hz/age seconds), `tx` (`TxView`: entry/measuredHz), `connected`, `crcErrors`, RX `overflow`, RX/TX bytes per second và `pending`. `pending` cộng application queue + Qt `bytesToWrite`; không gồm mọi kernel/hardware buffer. TX entries trong snapshot đã bỏ timestamp deque `measured`, dùng `measuredHz` riêng. `signatureErrors` của parser không được expose trong snapshot hiện tại.

GUI đăng ký `PortConfig`, `Mav::TxEntry`, `SnapshotPtr` bằng `qRegisterMetaType` trước queued calls. Developer tạo harness riêng phải đăng ký tương tự; xem setup trong [desktop_tests.cpp](../tests/desktop_tests.cpp).

`MainWindow` là application shell, chỉ có constructor/destructor public; editor/filter/theme helpers là private, không phải extension API. Chi tiết flow và shutdown xem [ARCHITECTURE.md](ARCHITECTURE.md).
