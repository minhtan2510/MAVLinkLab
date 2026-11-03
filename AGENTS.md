# Hướng dẫn cho coding agents

## Ngôn ngữ và cách làm việc

- Trả lời và giải thích bằng **tiếng Việt**; giữ nguyên technical terms bằng English.
- Code, identifiers, API names, keywords, code comments, Doxygen và UI text dùng **English**.
- Chỉ hỏi khi thông tin còn thiếu ảnh hưởng đáng kể đến kết quả; nếu đủ thông tin thì tiến hành. Nêu rõ assumption khi cần.
- Trong phase **analysis/review** được yêu cầu riêng, không chỉnh sửa file trước khi có yêu cầu implementation.
- Khi chỉnh code, tóm tắt thay đổi trong 1–2 câu đầu. Báo kết quả, cách kiểm tra và giới hạn thực tế; không khẳng định đã kiểm chứng nếu chưa chạy.
- Chỉ thay đổi trong scope được yêu cầu, giữ nguyên công việc sẵn có của user. Không commit/push/publish nếu chưa được yêu cầu.
- Hỏi trước khi xóa file có sẵn, thêm dependency ngoài stack đã chốt, cài package hệ thống hoặc thay đổi cấu hình OS. Không hỏi lại quyền đã được cấp trong session.
- Không chỉnh sửa `.agents/`, `.codex/`, `.aws/`, global configuration hoặc truy xuất credentials cho công việc thông thường của project.
- Với các tác vụ nhiều bước hoặc có độ phức tạp kỹ thuật (phân tích dialect MAVLink, can thiệp worker thread, refactor UI, kiểm thử tự động), chủ động chia việc cho các **Subagents** chuyên trách (Research, Developer, Review, QA) để phối hợp xử lý song song, rà soát chéo và tối ưu thời gian.

## Project và architecture

**MAVLinkSimulator** là desktop serial workbench cho **Ubuntu/Linux**.

| Thành phần | Công nghệ / trách nhiệm |
| --- | --- |
| Language / build | C++17, CMake ≥3.21 |
| GUI | Qt ≥6.2 Widgets |
| UART | Qt SerialPort, asynchronous buffered I/O |
| MAVLink | Official C library, git submodule `third_party/mavlink`, dialect `ardupilotmega` gồm `common` |
| `src/core.*` | Codec/reflection adapter, parser, RX statistics, scheduler primitives, bounded write queue; không phụ thuộc Qt |
| `src/worker.*` | Serial I/O, parser và TX scheduling trong worker thread |
| `src/window.*` | GUI, payload editor, dashboards và theme |
| `src/main.cpp` | Application entry point |
| `tests/` | Core tests, PTY integration và Qt widget/render checks |

- Đọc `README.md`, `docs/DESIGN.md` và `docs/VERIFICATION.md` khi liên quan. Kiểm tra lại dependency/environment hiện tại; các đường dẫn Qt trong `/tmp` chỉ là môi trường kiểm chứng tạm thời.
- Không đưa GUI/application policy vào codec/transport layer khi không có lý do rõ ràng.
- Không thêm Python application runtime, UDP/TCP, firmware flashing, database hoặc chức năng khác ngoài yêu cầu hiện tại.

## Code style và Doxygen

- Dùng **BSD/Allman style**: opening brace nằm trên dòng riêng cho function, control statement, class/struct/enum và các block tương tự.
- Một dòng trống giữa các function. Giữ naming/style nhất quán với module; không refactor style ngoài scope.
- Với C++, dùng **camelCase** cho biến/function theo code hiện có; type/class dùng **PascalCase**, macro dùng **UPPER_SNAKE_CASE**.
- Function mới hoặc sửa đáng kể phải có **Doxygen ngắn bằng English**: `@brief`, thêm `@param`, `@return`/`@retval` khi phù hợp, `@warning` cho side effects và `@note` khi cần.
- Ưu tiên RAII, ownership rõ ràng, bounds checks và error handling. Không dùng `volatile` thay cho synchronization giữa threads.

## MAVLink, UART và concurrency

- Dùng official MAVLink library cho parsing, serialization và CRC. Không tự viết codec/CRC hoặc sửa generated headers trong submodule.
- Giữ submodule ở commit xác định; chỉ cập nhật revision khi task yêu cầu.
- Dùng metadata **wire offsets**, field types và array lengths thực tế; không giả định C struct layout. Không bịa units, enums hoặc API không có trong source.
- Parser phải giữ state xuyên serial chunks, xử lý fragmented/multiple frames, noise và CRC errors; RX identity gồm **sysid + compid + msgid**.
- Validate payload types, numeric/array bounds, MAVLink version và offered load trước TX.
- Không tự động gửi message khi connect/reconnect. Disconnect phải dừng periodic TX và hủy pending output; dữ liệu đã gửi không thể thu hồi.
- Giữ queue/buffer có giới hạn, xử lý partial writes/backpressure và dùng monotonic clock; không phát catch-up burst khi trễ deadline.
- Tạo và sử dụng QSerialPort/timers đúng thread affinity. GUI chỉ cập nhật trên GUI thread; dùng queued commands và immutable snapshots khi trao đổi dữ liệu.
- Kiểm tra return values/error states, lifetime, shutdown và race conditions. Không block GUI bằng serial I/O/parser hoặc chờ đồng bộ dài.
- Không chủ động toggle DTR/RTS để reset thiết bị. Không tự thay permission, group membership hoặc cấu hình OS.
- **DMA phụ thuộc hardware/kernel driver**; không thêm “Enable DMA” nếu không có API thực tế. Không suy ra DMA từ asynchronous I/O hoặc PTY.
- Phân biệt queued/OS-written bytes với physical transmission và peer delivery. Không coi sequence gaps là packet loss chắc chắn, hoặc signed frames là authenticated nếu chưa verify signature.

## UI/UX

- Khi design/review/sửa GUI, áp dụng skill **ui-ux-pro-max** nếu có trong session; chọn guidance phù hợp Qt desktop, không áp đặt web/mobile stack.
- Giữ **UART bar gọn**, Port/Baud/Connect/status dễ tìm; thông số ít dùng nằm trong **Settings**. Ưu tiên diện tích cho TX/RX dashboards.
- Dùng semantic theme tokens, hierarchy rõ, keyboard navigation và status text; không truyền đạt trạng thái chỉ bằng màu.
- Giữ selection, scroll position và field đang xem khi refresh. Giới hạn GUI refresh; Pause Display không dừng parser/counters.
- Khi đồng bộ hai chiều giữa UI controls và `QSettings`:
  - Dùng `QSignalBlocker` trong các hàm khôi phục (`restoreSettings`) để tránh phát tín hiệu ngược kích hoạt `saveSettings`.
  - Luôn gọi `settings.sync()` sau khi cập nhật dữ liệu để đảm bảo cấu hình được ghi tức thì xuống đĩa, tránh sai lệch giữa các tiến trình/test case.
- Với thay đổi UI, tự kiểm tra rendered consistency ở **Light/Dark**, hover, pressed, focus, enabled/disabled, dropdown popup rows, resize và theme changes trong cùng session. Sửa regression tái hiện được trước khi kết luận.
- Phân biệt **offscreen render** với native desktop integration. Nếu không có môi trường render, báo rõ giới hạn.
- Các form controls có indicator (như checkbox, combobox chevrons, spinbox arrows) phải kế thừa lớp chuyên dụng (`WorkbenchCheckBox`, `WorkbenchComboBox`, `WorkbenchSpinBox`) và tự vẽ bằng `QPainter` vector antialiasing theme-aware; không dùng QSS màu khối đặc thô sơ làm mất biểu tượng checkmark/indicator.
- Các tùy chọn tự động khôi phục dữ liệu phiên làm việc (như Restore TX messages) phải luôn có giá trị **mặc định là tắt (`false` / disabled)** để đảm bảo an toàn và giữ trạng thái ứng dụng sạch sẽ khi khởi động.

## Build và verification

**Build đầy đủ** cần Qt Widgets, SerialPort và Qt Test khi `BUILD_TESTING=ON`:

```bash
git submodule update --init --recursive
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/mavlink-simulator
```

**Core-only** khi không có Qt:

```bash
cmake -S . -B build-core -DBUILD_GUI=OFF
cmake --build build-core -j
ctest --test-dir build-core --output-on-failure
```

- Nếu Qt không nằm trong system prefix, dùng `CMAKE_PREFIX_PATH` phù hợp. Không trộn Qt major/minor versions; không hardcode đường dẫn `/tmp` vào source/build configuration được commit.
- Khi chạy `ctest` cho desktop tests trong môi trường có Qt nằm ngoài system prefix (như `/tmp/mavlink-qt-prefix`), cần thiết lập `QT_PLUGIN_PATH` và `LD_LIBRARY_PATH` tương ứng với cấu hình trong `CMakeCache.txt` (tương tự `run.sh`) để nạp đúng plugin `offscreen`.
- Chạy checks phù hợp với thay đổi. Test logic mới/sửa bằng các trường hợp có ý nghĩa, gồm invalid input, bounds và failure paths; tránh tests chỉ lặp lại implementation.
- Tự kiểm tra syntax/types, API usage, initialization/lifetime, buffer bounds, state machine, concurrency và error handling. Cân nhắc sanitizers/static analysis khi có nguy cơ tương ứng.
- Báo riêng **build**, **automated tests**, **rendered GUI**, **PTY**, **physical UART** và **DMA**. Test fixture không phải bằng chứng hardware.
- Qt desktop tests tự chọn offscreen platform qua CTest và tạo ảnh tại `build/qa/`. Không đưa build artifacts hoặc `.codegraph/` vào Git.
- Cập nhật documentation liên quan khi behavior/workflow thay đổi. Gợi ý commit message khi user yêu cầu version-control work.

<!-- CODEGRAPH_START -->
## CodeGraph

Trong repository có `.codegraph/`, dùng **CodeGraph trước `rg`/đọc source** khi cần hiểu hoặc định vị code:

- Ưu tiên MCP `codegraph_explore` nếu có, hoặc CLI `codegraph explore "<file, symbol hoặc câu hỏi>"`.
- Dùng `codegraph node <file-or-symbol>` cho source cụ thể; thu hẹp query nếu kết quả bị cắt.
- Nếu CodeGraph không có thông tin cần thiết, dùng `rg` và đọc source trực tiếp. Không tạo index ở repository chưa được user cho phép.
- Sau khi sửa source, chạy `codegraph sync` và kiểm tra `codegraph status`.
<!-- CODEGRAPH_END -->
