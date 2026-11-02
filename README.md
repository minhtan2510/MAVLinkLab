# MAVLink Simulator

Ubuntu/Linux desktop serial workbench built with **C++17**, **Qt 6 Widgets**, **Qt SerialPort** and **CMake**. The official [MAVLink C library](https://github.com/mavlink/c_library_v2) is a pinned git submodule at `third_party/mavlink`. The dialect is **ardupilotmega**, including **common**.

## Build

Dependencies: C++17 compiler, CMake ≥3.21, Qt ≥6.2 development files for Widgets, SerialPort and Test. Qt Test is used when `BUILD_TESTING=ON` (the default).

On Ubuntu releases providing these packages:

```bash
sudo apt-get install build-essential cmake qt6-base-dev qt6-serialport-dev
git submodule update --init --recursive
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/mavlink-simulator
```

Some Ubuntu releases, including the inspected Jammy package catalog, do not provide `qt6-serialport-dev`. Use a matching Qt installation containing **Qt Serial Port**, and supply its prefix to CMake; do not mix Qt major/minor versions.

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/gcc_64
```

Core-only build requires no Qt:

```bash
cmake -S . -B build-core -DBUILD_GUI=OFF
cmake --build build-core -j
ctest --test-dir build-core --output-on-failure
```

## Quick launch

After building, open a terminal in the project and run:

```bash
./run.sh
```

Open two independent instances:

```bash
./run.sh --two
```

The launcher discovers the matching Qt library/plugin paths from `build/CMakeCache.txt`; manual environment exports are unnecessary. It does not create a virtual UART, connect a port or start TX. Keep the terminal open while using the application; Ctrl+C stops the instances it launched. To use another build directory, set `MAVLINK_BUILD_DIR`. If the configured Qt installation was removed (for example, a temporary prefix after reboot), reconfigure and rebuild with an available matching Qt installation.

## Use

1. Select a **Serial port** or enter a device path such as `/dev/ttyUSB0` or a PTY slave; the adjacent refresh icon rescans ports while keeping a typed path. Select **Baud rate** and click **Connect**. Open the **UART settings** sliders icon to change data bits, parity, stop bits or flow control in a popup; Escape or clicking outside closes it without resetting values. The popup summary shows framing and flow control. Configuration is disabled while connected.
2. Click **Add message**, search by ID/name, select sender and protocol, then edit payload values. Numeric arrays require the exact comma-separated element count; integer input is decimal. Character arrays accept UTF-8 text within the byte limit. Non-finite floating-point values are rejected. Drag the catalog divider to adjust its width; selected-message identity appears above the editor. The primary action is **Add & start** while connected or **Add message** offline; editing uses **Save changes**. Fixed protocol fields show **Read-only** and invalid values are marked directly in the table.
3. Select a message card in **TX Dashboard**. Each card shows ID, sender, frame count, state and target/actual Hz. Right-click a card for **Send once**, **Start** or **Stop**, **Edit message…** and **Remove message**. The menu selects the clicked card; Start/Stop follows its running state and transmission actions require a connection. Double-click a card or choose **Edit message…** to change its payload. The bottom row offers **Run all**, **Stop all** and **Delete all**. Run all starts every configured message only if their combined load fits the connected link; Delete all stops TX, cancels pending output and removes all TX entries. **Send once** queues one frame; **Start** enables periodic TX. **Stop**, **Stop all**, saving an edited entry, removing an entry or disconnecting cancels pending output. Stopping/editing/removing any entry discards the shared pending output queue, including other entries' pending frames; other active entries continue at their next deadlines.
4. Inspect **System → Component → Message** in **RX Dashboard**. Select a message to view fields below the stream list. The field header identifies its message ID and Sys/Comp; right-click a field row and choose **Copy value** to copy the displayed value. Filter by sender ID, message ID or name. **Pause display** freezes RX presentation while parsing/counting continues. **Clear** resets RX data and parser state, including any incomplete frame. Both workspace columns and the stream/field regions are resizable.

**New messages start periodic TX when saved while connected**, subject to link-capacity validation. Messages added while disconnected stay stopped. Connecting/reconnecting does not start TX; saving an edited entry stops it. There is no automatic HEARTBEAT. Payload values, including timestamps and counters, stay fixed until edited; they are not synthesized by the scheduler. Inspect protocol-specific constants and semantics before sending.

## Measurement and resource limits

- **Queued frames / measured TX Hz:** complete frames handed to Qt's output buffer. These are not delivery acknowledgements. TX throughput uses `bytesWritten`, which still does not prove reception or physical wire completion.
- **RX/TX Hz:** count of events in the trailing one-second window. At startup or below 1 Hz this is an event-window count, not an interarrival-period estimate. Idle streams become 0 Hz; **Stale** means no frame for more than two seconds.
- **Capacity:** conservative maximum payload length, protocol overhead and selected serial framing. Start is rejected if the configured aggregate periodic load exceeds baud capacity. Send Once uses bounded queues. Flow-control pauses can still produce backpressure.
- **Timing:** best effort on the Linux/Qt event loop using a monotonic clock and a 1 ms precise timer. Missed deadlines are skipped; there is no catch-up burst or hard realtime guarantee.
- **Bounds:** 128 TX entries, 2048 RX stream identities, 16 KiB application TX queue, approximately 4 KiB Qt TX queue and 64 KiB Qt read buffer. RX overflow counts frames from new stream identities beyond the limit. Queue overflow stops the affected periodic entry and reports an error.
- **Frames:** official MAVLink 1/2 parser preserves state across reads. Unknown dialect message IDs cannot be reliably CRC-validated/decoded and are not listed as valid RX messages. CRC errors can include unknown-dialect traffic. Sequence gaps are not reported as confirmed packet loss.
- **Signing:** no key management, outbound signing or authentication verification is configured. Signed frames accepted by the library are explicitly **not authenticated**.
- **Metadata:** generated field types and **wire offsets** drive payload editing/decoding. Generated C metadata does not include units or enum descriptions; units display **Unavailable**. Missing extension bytes decode as zero. MAVLink 1 drops extension fields and supports IDs ≤255 only.
- **Control lines:** the application never explicitly toggles DTR/RTS to reset hardware. Opening/closing a port and the OS/backend may still affect these lines; RTS/CTS intentionally delegates RTS control to the driver. No no-reset hardware guarantee is made. Software flow control can interfere with arbitrary binary traffic; use it only when the peer is configured accordingly.
- **DMA:** asynchronous serial I/O is used. Native UART DMA, if supported, is owned by the kernel driver; USB-to-UART uses the USB stack and bridge hardware. No application DMA switch exists, and DMA has not been verified on physical hardware.

## Architecture and validation

- `src/core.*`: official MAVLink codec/reflection adapter, parser, sender-indexed statistics, scheduler primitives and bounded write queue. No GUI or Qt dependency.
- `src/worker.*`: QSerialPort and timers created/used in one dedicated QThread. Reads are processed in bounded chunks. Queued commands and immutable snapshots cross the thread boundary.
- `src/window.*`: UART panel, searchable TX editor, dashboards, theme tokens and bounded GUI refresh. It owns worker shutdown and joins the thread on exit.
- `tests/core_tests.cpp`: catalog/codec, malformed input, numeric boundaries, queue limits and timing.
- `tests/desktop_tests.cpp`: PTY serial end-to-end tests plus widget actions and offscreen renders under `build/qa/`.

```bash
QT_QPA_PLATFORM=offscreen ./build/desktop-tests
codegraph sync
```

PTY checks demonstrate host serial processing, not electrical UART behavior or DMA. Offscreen renders demonstrate Qt rendering, not integration with every native desktop theme. No physical UART or DMA validation is implied. See [verification evidence](docs/VERIFICATION.md) for the checks actually completed in this checkout.

For permission/busy/device errors, read the persistent status-bar error and verify the selected device and its ownership in your terminal. The tool does not change permissions or group membership. Configuration and payloads are kept in memory for the current session only.
