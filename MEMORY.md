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
* **State Persistence & Decoupled Scopes:**
  * Application preferences (`MAVLinkSimulator/Preferences`), layout geometry (`MAVLinkSimulator/Layout`), and workspace TX messages (`MAVLinkSimulator/Workspace`) are kept in separate `QSettings` scopes.
  * `QSignalBlocker` must be used during `restoreSettings()` to prevent widget signals (e.g., `valueChanged`) from triggering premature `saveSettings()`. Always invoke `settings.sync()` immediately after writing.
* **Safe Session Restoration:**
  * Restored TX messages are strictly initialized with `active = false` (Stopped) and zero counters to prevent unintended transmissions on startup.
  * Restoration options (such as `workspace/restoreTx`) must always be **disabled by default (`false`)** for safety.
* **Custom Vector Form Controls:**
  * Indicators for controls (`WorkbenchCheckBox`, `WorkbenchComboBox`, `WorkbenchSpinBox`) are painted via vector paths with antialiased `QPainter` instead of crude QSS solid block colors, ensuring crisp rendering across themes.
* **Code Style & Formatting:**
  * **BSD/Allman style** across all C++ files: opening braces `{` strictly on their own line for functions, control statements, structs, enums, etc.
  * `camelCase` for variables and functions; `PascalCase` for types and classes; `UPPER_SNAKE_CASE` for macros.
  * Concise Doxygen comments in English (`@brief`, `@param`, `@return`) on all new or significantly modified functions.

---

## 3. Known Gotchas & Constraints
* **Custom Qt Prefix & CTest Offscreen Platform:**
  * When running `ctest` in environments where Qt resides in a non-standard prefix (e.g. `/tmp/mavlink-qt-prefix`), set:
    `QT_PLUGIN_PATH=/path/to/qt6/plugins LD_LIBRARY_PATH=/path/to/qt6/lib ctest --test-dir build --output-on-failure`
  * Missing these variables results in `Could not find the Qt platform plugin "offscreen"`.
* **Hardware & Serial Limits:**
  * Disconnect must immediately stop periodic TX and discard pending output.
  * Do not toggle DTR/RTS aggressively to avoid unintended device resets.
  * DMA depends on hardware kernel drivers; never infer DMA support from PTY or asynchronous I/O.
* **Multi-Agent Collaboration:**
  * Divide complex or multi-step tasks among specialized Subagents (Research, Developer, Review, QA) for parallel execution and cross-verification.
* **Linux X11 Popups & Rounded Window Masks:**
  * Top-level popups (`QMenu`, `QComboBoxPrivateContainer`) have opaque rectangular native surfaces on Linux X11. QSS `border-radius` leaves raw square corners unless `Qt::WA_TranslucentBackground` and `setMask(roundedRect)` are explicitly applied.
* **Editable ComboBox Text Clipping & Inner Focus Outlines:**
  * Child `QLineEdit` inside editable `QComboBox` inherits generic `QLineEdit:focus` styling; avoid QSS `outline` which ignores border-radius, and explicitly clear child line-edit borders/outlines (`QComboBox QLineEdit { border: none; outline: none; }`).
  * Child text width is reduced by drop-down width and padding; maintain sufficient fixed width (≥140px for baud rates) and zero text margins to prevent horizontal scroll clipping on high rates.

---

## 4. Roadmap & Status
* [x] **Phase 1: Foundation & MAVLink Defaults** (Settings dialog cards, default SysID/CompID/Version, QSettings persistence).
* [x] **Phase 3: Workspace & TX Persistence** (Restore TX messages in Stopped state [default: OFF], Reset Layout to defaults, WorkbenchCheckBox).
* [ ] **Phase 2: RX Display & Telemetry** (GUI refresh rate: 1/5/10 Hz, RX stream stale timeout, timestamp display format).
* [ ] **Phase 4: Serial Port Management** (Port filtering, auto-reconnect handling on USB disconnect).
* [ ] **Phase 5: Telemetry Logging** (Non-blocking `.tlog` raw bytes export).

---

## 5. Verification Workflow
* **Full Build & Test:**
  ```bash
  git submodule update --init --recursive
  cmake -S . -B build
  cmake --build build -j
  QT_PLUGIN_PATH=/tmp/mavlink-qt-prefix/usr/lib/x86_64-linux-gnu/qt6/plugins \
  LD_LIBRARY_PATH=/tmp/mavlink-qt-prefix/usr/lib/x86_64-linux-gnu \
  ctest --test-dir build --output-on-failure
  ```
* **CodeGraph Synchronization:**
  ```bash
  codegraph sync && codegraph status
  ```
