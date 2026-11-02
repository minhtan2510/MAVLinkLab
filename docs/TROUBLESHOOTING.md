# Troubleshooting

**Phạm vi:** Ubuntu/Linux desktop application hiện tại. Các nguyên nhân bên dưới là điều kiện được source/tests hỗ trợ; lỗi device thực tế cần kiểm tra theo status-bar reason, không suy ra từ UI đơn lẻ.

## Không configure/build được

| Triệu chứng | Chẩn đoán | Cách xử lý |
| --- | --- | --- |
| `Initialize MAVLink: git submodule update --init --recursive` | CMake không thấy `third_party/mavlink/ardupilotmega/mavlink.h` | Khởi tạo submodule theo command dưới |
| Không tìm thấy Qt6, Widgets, SerialPort hoặc Test | So dependency với [CMakeLists.txt](../CMakeLists.txt); Test chỉ cần khi `BUILD_TESTING=ON` | Dùng Qt ≥6.2 đầy đủ, đúng version; cung cấp `CMAKE_PREFIX_PATH`. Nếu chỉ cần core, dùng `BUILD_GUI=OFF` |
| Core-only vẫn lỗi MAVLink | Core vẫn dùng official generated headers | Khởi tạo submodule; core-only chỉ bỏ Qt |

```bash
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/gcc_64
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Đường dẫn Qt trên là placeholder, thay bằng installation thực tế. Không trộn Qt library/plugin từ các versions khác nhau. Dependency packages và core-only commands có trong [README](../README.md); availability của package phụ thuộc Ubuntu release. Không cần cài Qt nếu chỉ chạy core-only tests.

## Launcher không mở ứng dụng hoặc dùng build cũ

**Triệu chứng:** `Application not found`, `Configured Qt installation is missing`, `Qt runtime not found`, hoặc cửa sổ không phản ánh source mới.

**Chẩn đoán:** [run.sh](../run.sh) không build; nó chạy executable có sẵn trong `build/` hoặc `MAVLINK_BUILD_DIR`. Qt path lấy từ `Qt6Core_DIR:PATH` trong CMake cache. Temporary prefix bị xóa khiến build đã link không còn runtime.

**Xử lý:** configure/build lại với Qt đang tồn tại, đóng rồi mở lại instance để dùng executable mới. Chọn build khác bằng:

```bash
MAVLINK_BUILD_DIR=build-debug ./run.sh
./run.sh --help
```

`build-debug` phải chứa build GUI hoàn chỉnh trước khi chạy. Relative build directory được tính từ project root, không từ terminal working directory. `--two` chỉ mở hai process độc lập; không tạo UART pair hay connect tự động.

**Platform plugin/display:** CTest đặt offscreen platform để chạy không cần desktop. Chạy GUI bình thường cần desktop session và Qt platform plugins phù hợp. Nếu chạy executable trực tiếp thiếu library/plugin paths, dùng launcher với CMake cache đúng installation. Offscreen test pass không xác nhận native desktop integration.

## Không connect được serial port

**Triệu chứng:** state Error hoặc status-bar reason sau Connect.

1. Xác nhận device path trong **Serial port**; custom PTY/device path có thể nhập trực tiếp.
2. Nếu path không còn tồn tại, refresh danh sách rồi chọn lại; Refresh giữ path đã nhập, không xác nhận path còn hợp lệ.
3. Dựa trên reason của QSerialPort để kiểm tra device ownership, permission hoặc process khác đang sử dụng port. Đây là possible causes, không phải kết luận cho mọi open failure.
4. Chọn baud/framing/flow được driver và peer hỗ trợ. GUI chấp nhận một baud integer không có nghĩa hardware hỗ trợ baud đó.

Application không đổi permissions/group membership, không tự reconnect và không bảo đảm control lines không đổi khi driver open/close. Không chạy hai instances trên cùng một physical serial endpoint để giả lập TX/RX pair; mỗi instance cần endpoint tương ứng của kết nối thử nghiệm.

## Connect thành công nhưng TX không chạy

**Behavior có chủ đích:** Connect/reconnect không gửi message, không tự HEARTBEAT. Saved/edited entry luôn stopped.

**Chẩn đoán và xử lý:**

1. Add message, chọn card, kiểm tra sender/payload/protocol.
2. Dùng **Send once** để gửi một frame hoặc **Start** để bật periodic.
3. Nếu báo `Port closed or offered load exceeds link capacity`, kiểm tra connected state, giảm rate hoặc số entries đang running; chỉ tăng baud khi peer/driver cùng hỗ trợ.
4. MAVLink 1 không cho IDs >255; chuyển protocol sang MAVLink 2 cho các messages đó.

Payload timestamps/counters không tự cập nhật. Cần chỉnh payload hoặc bổ sung application logic nếu muốn dữ liệu động; scheduler hiện chỉ lặp payload đã lưu.

## TX dừng, Pending tăng hoặc peer không nhận đủ

**Triệu chứng:** `TX queue full or serialization failed; periodic TX stopped`, entry Error/Stopped, hoặc TX Hz thấp hơn target.

**Possible causes:** output path bị chậm/stall, flow control giữ truyền, offered load thực tế không theo kịp, hoặc serialization thất bại. Unread PTY peer gây backpressure đã có automated test.

**Chẩn đoán:** xem entry error, Pending, configured framing/flow và peer có đọc dữ liệu không. `Pending` chỉ gồm application + Qt queues, không đo toàn bộ kernel/hardware buffer. Capacity check là conservative estimate; không loại trừ flow-control stall hay event-loop delay.

**Xử lý:** giải quyết downstream stall hoặc giảm load, rồi Start lại entry. Disconnect/reconnect sẽ clear pending output và giữ TX stopped. Stop/Edit/Remove một entry cũng hủy **shared output queue**, có thể ảnh hưởng pending frames của entry khác. Bytes đã truyền không thu hồi được; frame truyền dở có thể bị cắt.

**Không dùng count làm delivery ACK:** frame count/TX Hz đo hoàn tất handing-off vào Qt; bytesWritten không chứng minh peer nhận đúng. Cần RX ở peer hoặc thiết bị đo để xác nhận truyền thực tế.

## RX trống, CRC errors tăng hoặc decoded values không đúng

1. Kiểm tra peer thực sự gửi MAVLink; Connect không yêu cầu peer tự stream.
2. Đồng bộ baud, parity, stop bits và flow control. Với binary MAVLink dùng 8 data bits; cấu hình khác có thể làm mất high bits. Software XON/XOFF có thể can thiệp binary bytes nếu peer không cấu hình phù hợp.
3. Kiểm tra filter và **Pause display**; Resume để nhận presentation mới. Parser/counters vẫn chạy trong Pause.
4. Kiểm tra sender system/component và message ID. Mỗi tuple là stream riêng; không gộp message cùng ID từ nhiều components.
5. Đối chiếu dialect compile là `ardupilotmega`. Unknown IDs không được đảm bảo CRC-validated/decoded; CRC errors không đủ để kết luận lỗi điện hoặc packet loss.

**Rate/age:** rate là event count trong trailing một giây; 0 Hz giữa hai messages rate thấp là bình thường. Stale nghĩa age >2 seconds. GUI cập nhật tối đa khoảng 5 Hz.

**Clear:** xóa RX storage và parser state, gồm fragmented frame chưa hoàn chỉnh, cùng RX/TX byte accumulators. Không xóa TX entries/counts. Nếu RX overflow tăng, đã vượt 2048 stream identities; Clear giải phóng identities nhưng incoming traffic có thể lấp đầy lại.

## Payload editor từ chối giá trị

**Triệu chứng:** inline error theo field name hoặc `MAVLink 1 supports message IDs 0–255 only`.

| Input | Constraint |
| --- | --- |
| Integer | Decimal, đúng signedness và range của type |
| Float/double | Finite; không chấp nhận NaN/Infinity hoặc overflow |
| Numeric array | Đúng số phần tử, comma-separated; không trailing comma |
| Char array | UTF-8 text theo **byte limit**, không phải số ký tự |
| HEARTBEAT `mavlink_version` | Read-only 3; không phải selector wire version |

Units/enum descriptions không có trong adapter; field values không được tự chuyển unit. Missing extension bytes decode thành zero. MAVLink 1 bỏ extension fields khi serialize; chọn MAVLink 2 nếu cần chúng.

## Signed frame, DMA và kiểm chứng

Signed-frame tooltip không phải authentication result: project không cấu hình signing keys/verification. Không có nút DMA vì QSerialPort asynchronous I/O không chứng minh driver dùng DMA; muốn xác nhận cần hardware/kernel evidence riêng.

Chạy test suites được repository cung cấp:

```bash
ctest --test-dir build --output-on-failure
cmake -S . -B build-core -DBUILD_GUI=OFF
cmake --build build-core -j
ctest --test-dir build-core --output-on-failure
```

Core tests kiểm tra codec/validation/parser/rate/queue; desktop tests kiểm tra PTY/backpressure/widgets và tạo screenshots trong `build/qa/`. Qt runtime ngoài system prefix cần đúng environment như [VERIFICATION.md](VERIFICATION.md). Các warnings offscreen về raise/keyboard grabs/size hints không đại diện cho native desktop behavior; vẫn kiểm tra exit status và failed assertions.

Không có physical-UART/DMA test, deployment packaging, persistence hoặc automated CI workflow trong tracked project hiện tại. Xem [architecture](ARCHITECTURE.md), [API contracts](API.md) và [verification evidence](VERIFICATION.md) trước khi mở rộng transport hay diễn giải metrics.
