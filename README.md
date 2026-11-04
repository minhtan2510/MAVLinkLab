# MAVLinkLab

**Send, receive and inspect MAVLink messages over serial UART.**

A desktop workbench for **Ubuntu/Linux**, built with C++17 and Qt 6. Supports **MAVLink 1 & 2** using the official MAVLink C library and the **ardupilotmega** dialect, including **common**.

Previously named MAVLinkSimulator. Existing preferences, layouts and optional TX workspaces remain compatible through the legacy `MAVLinkSimulator` settings namespace.

## What you can do

| Area | Features |
| --- | --- |
| **Serial** | Configurable port, baud rate, framing and flow control; optional auto-reconnect |
| **TX Dashboard** | Search messages, edit payloads, send once or periodically at 1–1000 Hz |
| **RX Dashboard** | Browse streams grouped by System/Component, view rates and counts, inspect decoded fields |
| **Workspace** | System/Light/Dark themes, saved layout and optional TX message restoration |

## Build & run

**Requirements:** C++17 compiler, CMake ≥3.21 and Qt ≥6.2 with **Widgets**, **SerialPort** and **Test** development components. Qt Test is needed when `BUILD_TESTING=ON` (default).

From the project root:

```bash
git submodule update --init --recursive
cmake -S . -B build
cmake --build build -j
./build/mavlink-lab
```

If Qt is installed outside the system prefix, configure with its installation path:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/gcc_64
```

Replace the example path with your Qt installation. Use matching Qt libraries and plugins; a custom installation may also require `LD_LIBRARY_PATH` and `QT_PLUGIN_PATH` at runtime.

## Quick start

Run `./cli.sh` for the interactive workbench menu. Each menu remembers its last highlighted option after commands and when returning from a submenu, for the current CLI session.

1. **Connect:** select a serial port from the dropdown and choose a baud rate, then click **Connect**. Port does not accept typed paths and has no startup focus or placeholder. Use the UART settings icon for framing and flow control. On startup or Refresh, unavailable previous ports are discarded; Port is empty when no ports are available.
2. **Create a message:** click **Add message**, search by ID or name, set sender IDs, protocol, rate and payload. **Add & start** immediately starts periodic TX when connected; messages added offline stay stopped.
3. **Control TX:** use a card's start/stop button or right-click for **Send once**, **Start/Stop**, **Edit message…** and **Remove message**. Batch controls run, stop or delete all entries.
4. **Inspect RX:** select a message under its System/Component group to view decoded fields. Search filters streams; **Pause display** freezes the view while parsing and counters continue. Right-click a field to **Copy value**.
5. **Customize:** open **Tool → Settings…** for theme, MAVLink defaults, serial options and workspace restoration.

## Important behavior

- **Connect/reconnect sends nothing automatically**, including HEARTBEAT. Saving an edited message stops it. Payload timestamps and counters stay fixed until edited.
- **Disconnect stops TX and clears pending output.** Stop/Edit/Remove also discard the shared pending output queue, which can affect other messages' pending frames. Bytes already sent cannot be recalled.
- **TX restoration is off by default.** When enabled, messages return in the Stopped state. Layout and preferences are saved separately.
- **TX counts are not delivery acknowledgements.** Rates use a trailing one-second event window; scheduling is best effort, with link-capacity checks and bounded queues.
- **DTR/RTS deassertion is on by default.** Hardware RTS/CTS leaves RTS driver-controlled. This cannot guarantee that opening a device never produces a reset pulse.
- **Signed frames are not authenticated.** Physical UART behavior and DMA require separate hardware verification.

## Tests

Run the core, PTY serial integration and Qt offscreen checks:

```bash
ctest --test-dir build --output-on-failure
```

For a custom Qt installation, set its matching library/plugin paths before running CTest. Offscreen screenshots are kept in **`build/qa/`**; PTY and offscreen results do not verify physical UART or native desktop integration.

**Core-only build** — no Qt required:

```bash
cmake -S . -B build-core -DBUILD_GUI=OFF
cmake --build build-core -j
ctest --test-dir build-core --output-on-failure
```

## Documentation

| Guide | Contents |
| --- | --- |
| [Architecture](docs/ARCHITECTURE.md) | Core, worker, GUI and threading boundaries |
| [API reference](docs/API.md) | Codec, parser, scheduling and queue contracts |
| [Troubleshooting](docs/TROUBLESHOOTING.md) | Build, serial connection and payload issues |
| [UI design](docs/DESIGN.md) | Layout, controls and themes |
| [Verification](docs/VERIFICATION.md) | Recorded checks and validation limits |
