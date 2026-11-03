# Project Memory & Architecture Context

## 1. Project Overview & Core Stack
* **Project Name:** MAVLinkSimulator (Ubuntu/Linux desktop serial workbench).
* **Language & Standard:** C++17, CMake ≥ 3.21.
* **GUI & I/O:** Qt ≥ 6.2 Widgets, Qt SerialPort (asynchronous buffered I/O), Qt Test.
* **MAVLink:** Official C library submodule pinned at `third_party/mavlink` (dialect: `ardupilotmega` including `common`).
* **Architectural Boundaries:**
  * `src/core.*`: Codec/reflection adapter, parser, RX statistics, scheduler primitives, bounded write queue (Strictly no Qt dependencies).
  * `src/worker.*`: Worker thread handling serial I/O, parsing, and TX scheduling with monotonic clocks.
  * `src/window.*`: GUI thread (Qt Widgets), payload editor, dashboards, and theme tokens.
  * `tests/`: Core unit tests, PTY virtual serial integration, and Qt offscreen widget checks.

---

## 2. Key Architectural Decisions (ADR)
* **Threading & Concurrency Model:**
  * Worker thread owns `QSerialPort` and timers; never touch serial I/O or parser directly from the GUI thread.
  * GUI and Worker communicate strictly via queued commands and immutable data snapshots.
  * Monotonic clock used for scheduling to prevent catch-up bursts when deadlines are missed.
* **Code Style & Formatting:**
  * **BSD/Allman style** across all C++ files: opening braces `{` strictly on their own line for functions, control statements, structs, enums, etc.
  * `camelCase` for variables and functions; `PascalCase` for types and classes; `UPPER_SNAKE_CASE` for macros.
  * Doxygen comments in English (`@brief`, `@param`, `@return`) on all new or significantly modified functions.
* **MAVLink Integrity:**
  * Never manually reimplement MAVLink codec or CRC; use the official submodule headers.
  * RX frame identity is tracked by tuple `(sysid, compid, msgid)`.
  * Parser must maintain state across fragmented chunks and noise.

---

## 3. Known Gotchas & Constraints
* **Qt SerialPort Dependency:**
  * Some Ubuntu distributions (e.g. Jammy) omit `qt6-serialport-dev` from system packages.
  * Build using `-DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/gcc_64` when using custom/external Qt installations. Never mix Qt major/minor versions.
  * Core-only build is always available without Qt via `-DBUILD_GUI=OFF`.
* **Hardware & Serial Limits:**
  * Disconnect must immediately stop periodic TX and discard pending output.
  * Do not toggle DTR/RTS aggressively to avoid unintended device resets.
  * DMA depends on hardware kernel drivers; never infer DMA support from PTY or asynchronous I/O.
* **Environment Safety:**
  * Do not introduce Python runtime, TCP/UDP networking, or database dependencies unless explicitly requested.

---

## 4. Current State & Testing Workflow
* **Full Build & Test:**
  ```bash
  git submodule update --init --recursive
  cmake -S . -B build
  cmake --build build -j
  ctest --test-dir build --output-on-failure
  ```
* **Offscreen QA Tests:** Qt tests generate offscreen verification images in `build/qa/`.

