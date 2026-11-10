# Project Memory & Architecture Context

## 1. Project Overview & Core Stack
* **Project Name:** MAVLinkLab (Ubuntu/Linux desktop serial workbench).
* **Rename compatibility:** UI/application identity and CMake project are `MAVLinkLab`; executable is `mavlink-lab`. Explicit QSettings namespaces intentionally remain `MAVLinkSimulator` to retain existing user data.
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
  * Serial Port (`serial/port`), Baudrate (`serial/baud`) and advanced UART parameters (`serial/dataBits`, `serial/parity`, `serial/stopBits`, `serial/flowControl`) are persisted upon user selection and connection, and restored on launch.
  * Port is selection-only, with no text editor, placeholder or startup focus. Startup/Refresh retains the previous port only if enumerated. With no available ports, Port is empty. PTY UI tests inject their fixture path into the dropdown model; production does not offer manual paths.
* **Safe Session Restoration:**
  * Restored TX messages are strictly initialized with `active = false` (Stopped) and zero counters to prevent unintended transmissions on startup.
  * Restoration options (such as `workspace/restoreTx`) must always be **disabled by default (`false`)** for safety.
* **Serial Port Management & Auto-Reconnect Strategy:**
  * Auto-reconnect uses a 1000ms timer confined entirely to the worker thread.
  * Unexpected disconnects (e.g. `ResourceError`, write failure < 0) immediately stop TX and discard buffers, transitioning the link to `Reconnecting` if enabled.
  * Manual disconnect (`closePort()`) immediately cancels any active reconnection loop and transitions directly to `Disconnected`.
  * Reconnection strictly maintains safety: no frames or heartbeats are sent automatically upon re-establishing the link.
  * Serial port enumeration automatically focuses on active plugged devices (USB converters, CDC-ACM, active onboard UARTs) to keep the toolbar dropdown clean.
* **Custom Vector Form Controls & Batch Action Icons:**
  * Indicators for controls (`WorkbenchCheckBox`, `WorkbenchComboBox`, `WorkbenchSpinBox`) and batch TX action buttons (`runAll`, `stopAll`, `deleteAll`) are painted via vector paths with antialiased `QPainter` instead of crude QSS or text labels.
  * Batch action buttons use 28x28 px compact icon buttons with semantic tooltips (`Run all`, `Stop all`, `Delete all`).
  * Vector icons (`runAll`: double play `▶▶`, `stopAll`: solid stop `■`, `deleteAll`: 3-rib trash bin) adapt dynamically to light/dark themes and disabled states.
  * Batch action buttons are located in the TX Dashboard header overview row (`txOverview`), vertically aligned with `txSummary` (`X messages · Y running`) and right-aligned beneath `+ Add message`. This consolidates action triggers into a natural top-down flow and removes redundant footer dividers (`txBatchDivider`), reclaiming full vertical space for the TX cards list.
  * TX Status indicator (`TxSummaryWidget`) uses Dual Pills: Pill 1 displays total configured messages with neutral background, and Pill 2 displays dynamic transmitting state with an antialiased vector LED dot (glowing green when running, neutral hollow when idle). Text measurement uses the matching DemiBold font metrics to prevent text clipping on `running`.
* **Real-time Throughput Monitoring & Link Load Estimation:**
  * Core provides `Mav::ByteRate` (1.0s sliding window), `Mav::estimateTxThroughput()`, `Mav::estimateEntryThroughput()`, and `Mav::formatByteRate()`.
  * Physical framing overhead bit calculation: $\text{bitsPerByte} = 1\text{ (Start)} + N_{\text{data}} + N_{\text{parity}} + N_{\text{stop}}$. Link utilization: $\text{Utilization (\%)} = \frac{\text{Bytes/sec} \times \text{bitsPerByte}}{\text{Baudrate}} \times 100$.
  * TX Dashboard header features a dynamic 3rd pill in `TxSummaryWidget` (`~X B/s (Y.Y%)`) when transmitting, showing pre-flight estimated link load with adaptive saturation theme colors (Blue ≤ 70%, Amber > 70%, Red > 90%).
  * RX Dashboard header features a real-time measured throughput pill (`rxThroughput` badge) right next to `rxSummary` (`X streams`), displaying actual received wire throughput and physical link capacity percentage (`X B/s (Y.Y%)` with 1 decimal place). Hidden when disconnected, visible when connected, with no tooltip, adapting dynamically to theme and link saturation.
  * Status bar displays live measured byte rates (`RX ↓` and `TX ↑`) with tooltips indicating channel capacity utilization and active baudrate.
  * Message Edit Dialog shows pre-flight load estimation badge (`Est. load: ~X B/s (Y.YY% at <baud> <framing>)`) with 2 decimal places and detailed framing tooltip.
* **Code Style & Formatting:**
  * **BSD/Allman style** across all C++ files: opening braces `{` strictly on their own line for functions, control statements, structs, enums, etc.
  * `camelCase` for variables and functions; `PascalCase` for types and classes; `UPPER_SNAKE_CASE` for macros.
  * Concise Doxygen comments in English (`@brief`, `@param`, `@return`) on all new or significantly modified functions.
* **Commit Message Conventions:**
  * Tiêu đề (subject line) phải ngắn gọn, súc tích nhất có thể (dạng `<type>(<scope>): <summary>`, ≤ 50–72 ký tự).
  * Toàn bộ nội dung phân tích chi tiết, bối cảnh, lý do và danh sách thay đổi cụ thể phải được ghi vào phần **body** của commit (cách tiêu đề 1 dòng trống), không viết dồn vào tiêu đề.

---

## 3. Known Gotchas & Constraints
* **Custom Qt Prefix & Local Dependencies (`requirement.txt`):**
  * Previously, Qt packages were unpacked into `/tmp/mavlink-qt-prefix/usr`, which gets wiped on system reboots or periodic `/tmp` cleanup, causing `FindQt6.cmake` missing errors.
  * The workbench now uses [requirement.txt](file:///home/minhtan/Desktop/dev/Tool/MAVLinkLab/requirement.txt) containing the required APT packages (`qt6-base-dev`, `libqt6serialport6-dev`, etc.).
  * Running `build_full` in [cli.sh](file:///home/minhtan/Desktop/dev/Tool/MAVLinkLab/cli.sh) automatically invokes `install_requirements()`, which downloads and extracts packages directly into `$project_dir/.deps` using `apt-get download` and `dpkg-deb -x` without requiring `sudo`.
  * [CMakeLists.txt](file:///home/minhtan/Desktop/dev/Tool/MAVLinkLab/CMakeLists.txt) and `detect_qt_prefix()` automatically detect `.deps/usr`, ensuring builds are persistent across system reboots.
  * When running `ctest`, `setup_qt_env()` in `cli.sh` configures `QT_PLUGIN_PATH` and `LD_LIBRARY_PATH` automatically based on CMakeCache or `.deps/usr`.
* **Hardware & Serial Limits:**
  * Disconnect must immediately stop periodic TX and discard pending output.
  * Do not toggle DTR/RTS aggressively to avoid unintended device resets.
  * DMA depends on hardware kernel drivers; never infer DMA support from PTY or asynchronous I/O.
* **Multi-Agent Collaboration:**
  * Proactively distribute multi-step or technically involved tasks across specialized **Subagents** (Research, Developer, Review, QA) for parallel execution, cross-checking, and time optimization.
* **QA Artifacts Retention:**
  * Do NOT clean up or delete generated verification images/screenshots in `qa/` or `build/qa/`; retain them for user inspection and visual regression review.
* **Unit Test Snapshot Injection vs. Worker Polling Timer:**
  * `MainWindow` maintains a 200ms `refresh` timer triggering `worker->requestSnapshot()`. In unit tests injecting synthetic snapshots directly via `window.applySnapshot(snapshot)`, call `window.stopRefreshTimer()` to prevent worker's empty periodic snapshots from overwriting test state.
  * Conversely, in integration tests relying on worker thread events (e.g. TX queue additions, PTY real transmission), keep the refresh timer active so worker snapshots flow naturally to the GUI.
* **Linux X11 Popups & Rounded Window Masks:**
  * Top-level popups (`QMenu`, `QComboBoxPrivateContainer`) have opaque rectangular native surfaces on Linux X11. QSS `border-radius` leaves raw square corners unless `Qt::WA_TranslucentBackground` and `setMask(roundedRect)` are explicitly applied.
* **Editable ComboBox Text Clipping, Alignment & Inner Padding:**
  * Child `QLineEdit` inside editable `QComboBox` inherits generic `QLineEdit:focus` styling; avoid QSS `outline` which ignores border-radius, and explicitly clear child line-edit borders/outlines (`QComboBox QLineEdit { border: none; outline: none; }`).
  * Qt6's `QStyleSheetStyle` calculates the geometry of the embedded `QLineEdit` strictly via `subControlRect(SC_ComboBoxEditField)`, which honors `QComboBox` padding but completely ignores CSS `padding-left` applied directly to child `QLineEdit`.
  * Set `QComboBox:editable { padding: 4px 28px 4px 7px; }` with `QComboBox QLineEdit { padding: 0; margin: 0; border: 0; }` and zero left text margins (`setTextMargins(0, 0, 18, 0)`). This aligns the embedded `QLineEdit` geometry (`x = 8`) and its initial text rendering pixel (`X = 12`) pixel-perfect with `QSpinBox` (`padding: 7px; x = 8; X = 12`) without text clipping or chevron overlap.
* **Enum Autocomplete in Dialog Inputs:**
  * Dialog dropdown enums (like Component ID via `MAV_COMPONENT`) provide `MatchContains` autocomplete, dynamic hover tooltips (`NAME = VALUE (DESC)`), and bidirectional conversion between integer IDs and symbolic enum names.
  * Hold `Ctrl` while clicking Save/Add message forces all empty payload fields to zero.
* **Dropdown Popup Selection Persistence & Active Highlight:**
  * In Qt6, editable `QComboBox` does not automatically update `currentIndex()` from typed text, causing popup views to highlight index 0 and lose current selection during mouse hover or scroll.
  * `WorkbenchComboBox` synchronizes `currentIndex()`, `view()->selectionModel()`, and auto-centers the view with `scrollTo()` upon `showPopup()`.
  * `WorkbenchComboDelegate` persists the active item's highlight independently of mouse hover states, rendering a theme-aware background (`#4f46e5` / `#4338ca`), bold text, and a crisp vector checkmark (`✓`) so the selected item remains clearly distinguishable while scrolling.
  * Dropdown items store their underlying numeric value in `Qt::ToolTipRole`. `WorkbenchComboDelegate::helpEvent()` intercepts `QHelpEvent` to reliably display item numeric values (e.g. `154` for `MAV_COMP_ID_GIMBAL`) without jitter via `QToolTip::showText(..., option.rect)`.
* **TX Card Deselection on Clicking Elsewhere:**
  * Only classify mouse presses delivered to `QWidget` receivers. Application event filters also see the popup's `QWindow` event before widget delivery; treating that non-widget receiver as an outside click clears the TX selection and disables context-menu actions before activation.
* **IME Bypass for Technical Search and Code Inputs:**
  * Technical inputs and filter fields (`CatalogSearch` used for `rxFilter`, catalog message search, parameter catalog filter; `paramFilter`; `inputEdit` parameter value editor) explicitly set `setAttribute(Qt::WA_InputMethodEnabled, false)` and `setInputMethodHints(Qt::ImhLatinOnly | Qt::ImhNoPredictiveText)`.
  * This bypasses OS-level IME composition (e.g. Vietnamese IBus/Fcitx Telex/VNI pre-edit buffers), ensuring instantaneous single-character key dispatch and immediate `textChanged` emission without requiring trailing space or underscore keys.
* **QLineEdit Placeholder Text & QSS Palette Override:**
  * In Qt6, QSS `color` on `QLineEdit` automatically resets `QPalette::PlaceholderText` to the primary text color during widget polish and show events, causing placeholder text to render at full foreground brightness instead of muted.
  * `WorkbenchLineEdit` intercepts `showEvent()` and `changeEvent()` (`PaletteChange`, `ApplicationPaletteChange`, `StyleChange`) and explicitly clears resolve masks (`colors.setResolveMask(0)`) to preserve the theme-aware dimmed placeholder color (`#606d88` in Dark, `#8893a7` in Light).
  * In `QTableWidget` (`txTable`), clicking empty space in the viewport does not deselect items by default.
  * Handled via `MainWindow::eventFilter()` on `QEvent::MouseButtonPress`: clicking invalid indices in `txTable->viewport()` or clicking outside `txTable` (e.g. headers, other dashboard panels, or toolbar, excluding scrollbars, modal dialogs, and `txMenu`) calls `txTable->clearSelection()`, `txTable->setCurrentCell(-1, -1)`, and `updateActions()` to dismiss active card focus.
* **Decoded Fields Table (`inspector`) Styling & Consistency:**
  * Synchronized with `payloadFields`: column headers (`Parameter`, `Type`, `Value`, with hidden `Unit`), row height (40px), header height (36px), column widths (190px, 105px, stretch), and QSS borders.
  * Rendered via `ValueDelegate::paint()` as a clean view-only rounded pill (`drawRoundedRect(input, 5, 5)`) with monospace font and dynamic changed highlight (`#58a6ff` / `#0969da`), while strictly enforcing view-only semantics (`createEditor()` returns `nullptr`).
  * Non-interactive display: `setSelectionMode(NoSelection)` and `setFocusPolicy(NoFocus)` prevent cells from being clicked, focused or highlighted with selection borders, while retaining right-click "Copy value" context menu and enum tooltips.
  * Visual feedback on Copy: Clicking "Copy value" triggers transient emerald cell flash/highlight (`#0f2e24` Dark / `#ecfdf5` Light) and a floating `CopyFeedbackBadge` ("✓ Copied") positioned inside the right side of the value pill for 800ms before auto-resetting. Automatically hidden on scrolling to preserve visual integrity.
* **Modern Subdued Debug Status Bar:**
  * Segmented metric chips (`StatusBarMetricsWidget`: `RX ↓`, `TX ↑`, `CRC`, `Pending`, `Overflow`) are aligned to the right via `addStretch(1)` in `statusBarCard`, matching `dashRight` (x = 1420px) pixel-perfect with upper dashboard panels.
  * Omit redundant connection text status from the debug bar; connection lifecycle states are exclusively reported by `ConnectionStatusWidget` on the UART toolbar.
  * Connection Status indicator (`ConnectionStatusWidget`) maintains clean binary/lifecycle states (`Disconnected`, `Connecting`, `Connected`, `Reconnecting`). Omit `Error` state from the badge: when a connection fails, a compact modal alert dialog is displayed and the badge cleanly remains `Disconnected`.
  * Visibility toggle & Throughput Unit: Managed by `WorkbenchCheckBox` (`Show stats bar`, `ui/showDebugBar`, default: `true`) and `WorkbenchComboBox` (`Stats Bar Unit:`, `ui/statsRateUnit`, items: `{"B/s", "KB/s", "MB/s"}`, default: index 1 `KB/s`) in `SettingsDialog` (`Appearance` card). The unit dropdown is disabled/dimmed when `Show stats bar` is unchecked, and live formatting adapts immediately across `StatusBarMetricsWidget`.
* **Compact Floating Alert Dialogs (`ConnectionErrorDialog`):**
  * Do NOT apply `border-radius` directly to a top-level `QDialog` stylesheet without alpha transparency; X11/OS native window rects cause dark/light jagged clipping artifacts at the corners.
  * Correct architecture: `setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint)` + `setAttribute(Qt::WA_TranslucentBackground)` on `QDialog`, enclosing content in an inner `QFrame#connectionErrorCard` with `border-radius: 10px; border: 1px solid %4; background: %2;`.
  * Support titleless window movement via `mousePressEvent`/`mouseMoveEvent` on the card surface, and auto-dismiss via default OK button, `Enter`, `Space`, or `Escape`.
* **RX Dashboard Smart Grouped Tree Architecture:**
  * Clean 2-level hierarchy replacing obsolete 3-level tree: Level 1 is the Device group (`System X · Component Y`), and Level 2 contains the leaf message streams with a compact 16px indentation.
  * Device Group Banner (Level 1): identity-only `System X · Component Y`, spanning the full row. No Rate, Count, status dot, age or auxiliary tooltip on the group. Bold 13 px text distinguishes it from message rows; there is no custom background or text color.
  * Leaf Message Row (Level 2): Displays an inline status dot and `[ID] NAME` (Col 0), stream rate in Hz (Col 1), and frame count (Col 2). Status tooltips/accessibility use Active/Stale/Inactive without an age countdown.
  * RX Column Layout: `RxHeader` uses Fixed resize mode with no dragging, reordering, double-click auto-fit, resize grips or hover indicators. The application alone computes widths: give Message approximately half of the viewport, distribute remaining space across Rate/Count, and fit every viewport without horizontal scrolling (scale normal minima180/90/80 below350 px). Ignore legacy `rxColumns` preferences and remove that key on save. Reset uses the same distribution; decoded-field header preferences remain independent.
  * Centered Metrics & Distinct Headers: `Rate (Hz)` and `Count` values (along with their column headers) are cleanly centered (`Qt::AlignCenter`) on leaf items. Header sections retain subtle `border-right: 1px solid` to separate column labels, while keeping table rows unfragmented by vertical gridlines.
  * Seamless Layout without Jarring Vertical Gridlines: Eliminated rigid vertical separator lines from intermediate columns, relying on horizontal separators (`border-bottom: 1px solid`) and cohesive group banners to maintain modern desktop ergonomics and avoid fragmented visual clutter.
  * Focus Rect & Outline Suppression: `RxItemDelegate` strips `State_HasFocus` while QSS enforces `outline: none; border: none;` on selected and focused items, providing clean full-row solid highlight without distracting dotted frames.
  * Inline Status Dots: `RxItemDelegate` uses `RxStatusIcon` vector decorations before Message text on leaf rows only. Freshness is stored in column0 at `Qt::UserRole + 2`, leaving identity roles untouched. Green Active at age<=1.2s, amber Stale through3.0s, muted Inactive afterward; no visible age or separate Status column. Device groups are identity-only.
  * Direct Two-Tier Filtering: `MainWindow::filterRx()` searches across device header and leaf identities in a clean 2-level loop.
* **MAVLink Parameter Protocol Collection & Dual-Mode Inspector:**
  * Parameter Protocol encodes all distinct parameters under a single stream identity `[22] PARAM_VALUE`. The core `Mav::Receiver` aggregates parameter values into `params[{sysid, compid}]` maps rather than discarding previous entries.
  * Wire format packs numeric parameter values into a 32-bit `float param_value`. Type conversion for integers (`UINT8`, `INT8`, `UINT16`, `INT16`, `UINT32`, `INT32`) must strictly use standard type-punning via `mavlink_param_union_t` to avoid IEEE-754 precision loss and NaN conversion errors.
  * The Inspector pane dynamically toggles between standard **Decoded Fields View** and **Parameter View**:
    * Selecting `[22] PARAM_VALUE` activates Parameter View, displaying full table columns: `Name` (monospace), `Type` (`UINT8`, `FLOAT`, etc.), `Value` (decoded string), and `Index` (`#<index>`).
    * Displays loading progress header: `PARAMETERS (<loaded_count> / <total_count>)`.
    * Provides inline real-time parameter search via `paramFilter` (`QLineEdit`).
    * Parameter modification: Double-clicking any parameter row or right-clicking to select "Edit parameter…" opens a modal dialog validating input against the parameter type, and immediately dispatches `PARAM_SET` (msgid 23) via worker slots.
    * Selecting any other message automatically restores standard `DECODED FIELDS` and hides parameter controls.
* **In-App Update Checker Dialog & Version Single Source of Truth (SSOT):**
  * `CMakeLists.txt` (`project(MAVLinkLab VERSION x.y.z)`) is the strict Single Source of Truth for the application version.
  * CMake injects `MAVLINK_LAB_VERSION="${PROJECT_VERSION}"` globally (`PUBLIC` definition on `mavdesktop`), eliminating duplicate hardcoded strings across headers and tests.
  * Helper `./cli.sh bump <version>` and `./cli.sh version` automate version updates in a single command.
  * Asynchronously fetches latest GitHub release metadata via non-blocking `curl` subprocess, avoiding extra library dependencies and TLS configuration pitfalls.
  * SemVer parsing (`Version::isNewerVersion`) dynamically reveals the "Update" button to launch the browser release page only when a newer version is available.
  * Browser launch (`openUrlQuietly`): Uses detached `xdg-open` with `QProcess::nullDevice()` redirection on Linux to eliminate noisy Chromium/Google Chrome zygote `Broken pipe` terminal stderr output when opening existing browser sessions.
  * Safely terminates any active background fetch process upon dialog close to prevent `QProcess: Destroyed while process is still running` crashes.
* **Throughput Tracking & Link Load Estimation:**
  * Throughput tracking uses `Mav::ByteRate` (1.0-second monotonic sliding window ring buffer) in core logic to eliminate jitter/spikes inherent in periodic interval resets (`bytes / elapsed`).
  * Wire size computation (`Mav::frameSize`) accounts for MAVLink framing overhead: v1 (`payload + 8`), v2 (`payload + 12`).
  * Pre-flight TX load estimation (`Mav::estimateTxThroughput`) calculates planned byte rate and channel capacity utilization percentage based on baudrate and physical UART framing (10 bits per byte for 8N1).
  * `TxSummaryWidget` dynamically displays an antialiased 3rd pill (`~X B/s (Y%)`) when transmitting, color-coded for safety: blue (<70%), amber (70-90%), red (>90% overload).
  * Message Edit Dialog (`messageLoadEstimate`) interactively computes wire throughput when selecting messages, toggling MAVLink versions, or adjusting frequency spinboxes.
  * `StatusBarMetricsWidget` displays real-time throughput with contextual tooltips reporting percentage of link capacity and baudrate.

---

## 4. Roadmap & Status
* [x] **Phase 1: Foundation & MAVLink Defaults** (Settings dialog cards, default SysID/CompID/Version, QSettings persistence).
* [x] **Phase 3: Workspace & TX Persistence** (Restore TX messages in Stopped state [default: OFF], Reset Layout to defaults, WorkbenchCheckBox).
* [x] **Phase 4: Serial Port Management** (Auto-reconnect handling on unexpected link drop, clean device enumeration).
* [ ] **Phase 2: RX Display & Telemetry** (GUI refresh rate: 1/5/10 Hz, RX stream stale timeout, timestamp display format).
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
