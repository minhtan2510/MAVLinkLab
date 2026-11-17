<p align="center">
  <img src="resources/icons/mavlink-lab.svg" alt="MAVLinkLab Logo" width="128" height="128">
</p>

# MAVLinkLab

**Rugged serial MAVLink workbench for Ubuntu/Linux.**

MAVLinkLab is a high-performance desktop workbench built with C++17 and Qt 6 for testing, inspecting, and simulating **MAVLink 1 & 2** communication over serial UART. It links against the official MAVLink C library using the **ardupilotmega** dialect (including **common**).

Previously named MAVLinkSimulator. Existing configurations, layouts, and workspace TX message lists remain fully compatible via the legacy `MAVLinkSimulator` QSettings namespace.

---

## Overview

MAVLinkLab provides a robust tool for firmware engineers, roboticists, and flight software developers to interact directly with MAVLink-enabled flight controllers, gimbals, companion computers, and custom embedded nodes over physical or virtual serial links.

Key design principles:
- **No Automatic Transmission on Connect:** Link activation never sends unprompted frames or heartbeats.
- **Strict Thread Affinity:** Serial I/O, frame parsing, and periodic scheduling are confined to a dedicated background worker thread; the GUI thread remains responsive under heavy traffic.
- **Zero-Allocation Core Engine:** Telemetry parsing, sliding-window rate tracking, and real-time waveform plotting operate without dynamic heap reallocations during streaming.
- **Defensive Hardware Protection:** Linux modem control lines (DTR/RTS) are deasserted by default on connection to prevent unintended target resets.

---

## Features

| Capability | Description |
| --- | --- |
| **Serial Management** | Configurable device path, baud rate, data bits, parity, stop bits, and flow control. Features clean plugged-device enumeration, auto-reconnect, and modem control-line deassertion. |
| **TX Dashboard** | Message search catalog, interactive payload editor, batch actions (Run all, Stop all, Delete all), and isochronous stream scheduling from 0.1 to 1000 Hz. |
| **RX Dashboard** | 2-level grouped tree (`System · Component → Leaf Message`), inline freshness status dots, real-time message rates (Hz), frame counters, and throughput monitoring. |
| **Decoded Fields & Inspector** | Live inspection of primitive types, 64-bit integers, IEEE-754 floats, and character arrays with right-click "Copy value" and transient visual confirmation. |
| **Parameter Protocol** | Dual-mode inspector for `PARAM_VALUE` (msgid 22). Aggregates parameters per component, provides real-time search, type-safe editing, and `PARAM_SET` (msgid 23) transmission. |
| **Real-time Telemetry Plotter** | Built-in rolling strip-chart canvas using native antialiased `QPainter` vectors. Supports up to 6 simultaneous channels, auto-scaling Y-axis, time windows (10s–120s), inspection cursor, and CSV export. |
| **Throughput & Load Estimation** | Pre-flight link load estimation (`~X B/s (Y%)`) based on physical UART framing overhead (e.g. 10 bits/byte for 8N1). Real-time RX/TX byte rate tracking via 1.0s monotonic sliding windows. |
| **Portable TX Profiles** | Export and import portable CBOR configuration files (`.mavprofile`) with Replace and Append modes, validated against a 1 MiB schema limit. |
| **Workspace & Themes** | Cohesive Light and Dark themes, customizable debug status bar, adjustable splitters, and persistent window geometry. |

---

## Requirements

- **Operating System:** Ubuntu / Linux (x86_64).
- **Compiler:** C++17 capable compiler (GCC ≥ 9, Clang ≥ 10).
- **Build System:** CMake ≥ 3.21.
- **Framework:** Qt ≥ 6.2 with `Widgets`, `SerialPort`, and `Test` (when building tests).
- **Submodules:** Official MAVLink C headers (`third_party/mavlink`).
- **Python (Optional):** Python 3 for regenerating enum lookups from XML definitions.

---

## Hardware

- **Supported Transports:** Any Linux TTY serial device, including:
  - Standard onboard UARTs (`/dev/ttyS*`).
  - USB-to-UART bridge converters (FTDI, CP210x, CH340, PL2303 via `/dev/ttyUSB*`).
  - USB CDC-ACM virtual serial ports (STM32 VCP, Pixhawk, Arduino via `/dev/ttyACM*`).
  - Virtual pseudo-terminals (`/dev/pts/*` PTY pairs created via `socat`).
- **Physical Framing:** 5 to 8 data bits, None/Even/Odd/Mark/Space parity, 1, 1.5, or 2 stop bits, and None/Hardware (RTS/CTS)/Software (XON/XOFF) flow control.

---

## Software Dependencies

All primary dependencies are resolved via system packages or git submodules:

| Dependency | Location / Package | Purpose |
| --- | --- | --- |
| **MAVLink C Library** | `third_party/mavlink` (Git submodule) | Protocol definitions, wire serialization, CRC calculation |
| **Qt6 Widgets** | `qt6-base-dev` | Desktop user interface and dialogs |
| **Qt6 SerialPort** | `libqt6serialport6-dev` | Asynchronous non-blocking serial device I/O |
| **Qt6 Test** | `qt6-base-dev` | Unit and integration testing harnesses |
| **Local Dependencies** | `.deps/usr` (Auto-extracted) | Local userspace dependency prefix when root access is unavailable |

---

## Project Structure

```text
MAVLinkLab/
├── CMakeLists.txt          # CMake build definitions (mavcore, mavdesktop, mavlink-lab, tests)
├── README.md               # Project overview and quick start guide
├── AGENTS.md               # Coding agent instructions and technical knowledge base
├── MEMORY.md               # Project memory, ADRs, conventions, and gotchas
├── cli.sh                  # Interactive command-line workbench helper
├── requirement.txt         # APT packages required for local userspace extraction
├── docs/                   # Developer documentation
│   ├── ARCHITECTURE.md     # System architecture, threading, and ownership models
│   ├── API.md              # Public C++ interfaces and data structures
│   ├── TROUBLESHOOTING.md  # Common build, connection, and runtime troubleshooting
│   ├── DESIGN.md           # Desktop UI design tokens, layout, and ergonomics
│   ├── PROFILES.md         # CBOR TX profile schema specification
│   ├── VERIFICATION.md     # Test verification evidence and QA procedures
│   └── PROPOSAL_ADVANCED_FEATURES.md # Advanced feature roadmap and proposals
├── src/                    # Source code
│   ├── core.h, core.cpp    # Core codec, metadata, parser, rate estimators, bounded queue (No Qt)
│   ├── mavlink_enums.h, cpp# Generated MAVLink enum lookup tables
│   ├── worker.h, worker.cpp# SerialWorker background thread (QSerialPort, scheduling, I/O)
│   ├── window.h, window.cpp# MainWindow GUI, dashboards, payload editor, plot canvas, themes
│   ├── profile.h, profile.cpp# CBOR serialization for .mavprofile files
│   ├── version.h           # Semantic version parsing and update checking
│   └── main.cpp            # Application entry point
├── tests/                  # Automated test suites
│   ├── core_tests.cpp      # Pure C++ unit tests for codec, parser, rates, and ring buffer
│   ├── desktop_tests.cpp   # GUI, PTY serial, and offscreen widget validation
│   ├── control_lines_tests.cpp # Linux modem control lines and ioctl wrapping tests
│   ├── profile_tests.cpp   # Profile CBOR codec unit tests
│   ├── profile_worker_tests.cpp # Worker profile import/export tests
│   └── profile_ui_tests.cpp# Profile dialog and UI flow tests
├── resources/              # Vector icons, desktop launcher, and Qt resources (.qrc)
└── tools/                  # Code generation utilities (generate_mavlink_enums.py)
```

---

## Build

### Full Build (GUI & Tests)

```bash
git submodule update --init --recursive
cmake -S . -B build
cmake --build build -j
./build/mavlink-lab
```

### Custom Qt Installation Prefix

If Qt 6 is installed outside the standard system path:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/gcc_64
cmake --build build -j
```

### Core-Only Build (No Qt Required)

To build and verify the protocol codec, parser, and ring buffers on headless servers without Qt:

```bash
cmake -S . -B build-core -DBUILD_GUI=OFF
cmake --build build-core -j
ctest --test-dir build-core --output-on-failure
```

---

## Configuration

Settings are organized cleanly in **Tool → Settings…** and persisted in `~/.config/MAVLinkSimulator/`:

- **Appearance:** Select System, Light, or Dark theme tokens.
- **MAVLink Defaults:** Default System ID (default: 1), Component ID (default: 1), and wire protocol version (MAVLink 1 vs 2).
- **Serial Management:** Auto-reconnect toggle (reconnects every 1000 ms on unexpected link drop) and Deassert DTR/RTS on connect.
- **Workspace & Session:** Optional restoration of configured TX messages on startup (disabled by default for safety). Reset Layout restores default window dimensions and splitters.
- **Debug Status Bar:** Toggle metrics visibility and customize throughput display units (`B/s`, `KB/s`, `MB/s`).

---

## Usage

You can launch the interactive workbench helper `./cli.sh` or run the binary directly:

1. **Connect:** Select a detected serial port from the dropdown, choose the baud rate, and click **Connect**. Click the sliders icon to adjust data bits, parity, stop bits, or flow control.
2. **Add a Message:** Click **+ Add message** in the TX Dashboard. Search by message ID or name, configure sender IDs, target rate (Hz), and fill in payload fields. Click **Add & start** to transmit immediately if connected, or **Add message** to keep it stopped.
3. **Control Transmission:** Click the inline play/stop button on any message card to toggle periodic transmission. Right-click for context actions: **Send once**, **Start/Stop**, **Edit message…**, or **Remove message**. Use batch action buttons (`Run all`, `Stop all`, `Delete all`) beneath the header.
4. **Inspect Received Messages:** Browse streams in the RX Dashboard grouped by `System · Component`. Click any message to view live decoded values in the **Decoded Fields** table.
5. **Inspect Parameters:** Selecting `[22] PARAM_VALUE` switches the inspector to **Parameter Protocol View**, showing parameter names, types, values, and indices. Double-click any row to edit and send `PARAM_SET`.
6. **Real-time Plotting:** Right-click any numeric field in the inspector and select **Plot this field** to activate the real-time strip-chart canvas.
7. **Profiles:** Save your active TX configuration via **File → Export Profile…**, or load a profile via **File → Import Profile…**.

---

## Code Examples

### In-Memory MAVLink Encoding and Parsing

The core library (`src/core.*`) operates independently of Qt:

```cpp
#include "core.h"
#include <iostream>

int main()
{
    // Retrieve official metadata for HEARTBEAT
    const auto *metadata = mavlink_get_msg_entry(MAVLINK_MSG_ID_HEARTBEAT);
    if (!metadata)
    {
        return 1;
    }

    // Allocate zero-initialized payload buffer
    Mav::Bytes payload(metadata->max_msg_len, 0);

    // Set fields using generated metadata and wire offsets
    std::string error;
    const auto *customModeField = mavlink_get_field_info(metadata, "custom_mode");
    if (customModeField)
    {
        Mav::setField(payload, *customModeField, "0", error);
    }

    // Serialize MAVLink 2 frame
    mavlink_status_t status{};
    Mav::Bytes frame = Mav::encode(MAVLINK_MSG_ID_HEARTBEAT, 1, 1, false, payload, status);

    // Parse received wire bytes
    Mav::Parser parser;
    std::vector<mavlink_message_t> decoded = parser.feed(frame);

    if (!decoded.empty())
    {
        std::cout << "Successfully decoded message ID: " << decoded[0].msgid
                  << " from SysID: " << int(decoded[0].sysid) << "\n";
    }

    return 0;
}
```

---

## API Overview

- **`Mav` Namespace (`src/core.h`):**
  - Metadata & Reflection: `catalog()`, `typeName()`, `typeSize()`, `setField()`, `readField()`, `readFieldNumeric()`.
  - Wire Codec: `encode()`, `frameSize()`.
  - Rate Tracking & Scheduling: `Rate`, `ByteRate`, `PlotBuffer`, `validTx()`, `fitsLink()`, `due()`.
  - Bandwidth Estimation: `estimateEntryThroughput()`, `estimateTxThroughput()`, `formatByteRate()`.
  - Bounded Output Queue: `WriteQueue` (capped at 16,384 bytes).
- **`SerialWorker` Class (`src/worker.h`):**
  - Manages `QSerialPort` and 1 ms scheduling timers strictly in a background `QThread`.
  - Public slots: `openPort()`, `closePort()`, `saveEntry()`, `removeEntry()`, `setActive()`, `setRate()`, `sendOnce()`, `runAll()`, `stopAll()`, `deleteAll()`, `importProfile()`, `exportProfile()`, `requestSnapshot()`.
  - Signals: `stateChanged()`, `failure()`, `snapshotReady()`.
- **`Profile` Namespace (`src/profile.h`):**
  - `encode()` and `decode()` CBOR schema 1 profiles (`.mavprofile`).

---

## Documentation

Comprehensive developer-facing guides are available under `docs/`:

- [System Architecture](docs/ARCHITECTURE.md) - Threading model, ownership boundaries, and runtime data flows.
- [API Reference](docs/API.md) - Detailed public interfaces, error contracts, and parameters.
- [Troubleshooting Guide](docs/TROUBLESHOOTING.md) - Diagnostic steps for build, connection, TX, and RX issues.
- [UI/UX Design](docs/DESIGN.md) - Desktop layout, token typography, and ergonomic principles.
- [TX Profile Format](docs/PROFILES.md) - CBOR schema specification and validation rules.
- [Verification Evidence](docs/VERIFICATION.md) - Test suites, offscreen rendering, and verification constraints.

---

## Testing

Run all 72 automated tests (unit tests, PTY virtual serial checks, and offscreen widget rendering):

```bash
ctest --test-dir build --output-on-failure
```

Or run via the workbench CLI:

```bash
./cli.sh test
```

Offscreen test artifacts and visual regression screenshots are automatically saved to `build/qa/` and `qa/`.
