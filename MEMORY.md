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
  * Default splitter sizes for `dashboards` are `{467, 929}` px (TX: 467 px, RX: 929 px), calibrated to fit all three status pills perfectly with standard 16 px panel margins.
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
* **Serial Port Buffer Purge & Clean Session Transitions:**
  * Upon `openPort()`, both kernel TTY queues (`::tcflush(fd, TCIOFLUSH)`) and Qt buffers (`port->clear(AllDirections)`, `port->readAll()`) are rigorously drained before transitioning to `Connected`, eliminating residual backlog/stale frames from prior high-throughput stress tests.
  * Upon `closePort()`, all pending TX is stopped and buffers are purged before closing the OS file descriptor.
  * In `MainWindow::closeEvent()`, `worker->closePort()` is invoked synchronously to halt periodic transmissions and release the serial device immediately when a window is closed.
* **Custom Vector Form Controls & Batch Action Icons:**
  * Indicators for controls (`WorkbenchCheckBox`, `WorkbenchComboBox`, `WorkbenchSpinBox`) and batch TX action buttons (`runAll`, `stopAll`, `deleteAll`) are painted via vector paths with antialiased `QPainter` instead of crude QSS or text labels.
  * Batch action buttons use 28x28 px compact icon buttons with semantic tooltips (`Run all`, `Stop all`, `Delete all`).
  * Vector icons (`runAll`: double play `▶▶`, `stopAll`: solid stop `■`, `deleteAll`: 3-rib trash bin) adapt dynamically to light/dark themes and disabled states.
  * Batch action buttons are located in the TX Dashboard header row 1 in column 0 directly beneath `TX Dashboard` title: `runAll` (left-aligned with 'T') and `stopAll` are clustered together, while `deleteAll` is pushed via `addStretch()` so its right edge aligns pixel-perfect with the trailing letter 'd' of `TX Dashboard`. `+ Add message` is on row 1 on the far right. Status pills (`TxSummaryWidget`) are located on row 0 directly next to the `TX Dashboard` title (matching the `RX Dashboard` header style). This creates a cohesive, symmetric design language across both dashboards.
  * TX Status indicator (`TxSummaryWidget`) uses Dual/Triple Pills: Pill 1 displays total configured messages with neutral background, and Pill 2 displays dynamic transmitting state with clean centered text (`X running` / `0 running`) without LED dot icon for a unified minimal aesthetic matching Pill 1. Text measurement uses the matching DemiBold font metrics.
* **Real-time Throughput Monitoring & Link Load Estimation:**
  * Core provides `Mav::ByteRate` (1.0s sliding window), `Mav::estimateTxThroughput()`, `Mav::estimateEntryThroughput()`, and `Mav::formatByteRate()`.
  * Physical framing overhead bit calculation: $\text{bitsPerByte} = 1\text{ (Start)} + N_{\text{data}} + N_{\text{parity}} + N_{\text{stop}}$. Link utilization: $\text{Utilization (\%)} = \frac{\text{Bytes/sec} \times \text{bitsPerByte}}{\text{Baudrate}} \times 100$.
  * TX Dashboard header features a dynamic 3rd pill in `TxSummaryWidget` (`~X B/s (Y.Y%)`) when transmitting, showing pre-flight estimated link load with adaptive saturation theme colors (Blue ≤ 70%, Amber > 70%, Red > 90%).
  * RX Dashboard header features a real-time measured throughput pill (`rxThroughput` badge) right next to `rxSummary` (`X streams`), displaying actual received wire throughput and physical link capacity percentage (`X B/s (Y.Y%)` with 1 decimal place). Hidden when disconnected, visible when connected, with no tooltip, adapting dynamically to theme and link saturation.
  * Status bar displays live measured byte rates (`RX ↓` and `TX ↑`) with tooltips indicating channel capacity utilization and active baudrate.
  * Message Edit Dialog shows pre-flight load estimation badge (`Est. load: ~X B/s (Y.YY% at <baud> <framing>)`) with 2 decimal places and detailed framing tooltip.
  * Message Edit Dialog displays a dedicated schema payload length badge (`X Bytes`) right next to the selected message title (`[ID] NAME`).
* **Code Style & Formatting:**
  * **BSD/Allman style** across all C++ files: opening braces `{` strictly on their own line for functions, control statements, structs, enums, etc.
  * `camelCase` for variables and functions; `PascalCase` for types and classes; `UPPER_SNAKE_CASE` for macros.
  * Concise Doxygen comments in English (`@brief`, `@param`, `@return`) on all new or significantly modified functions.
* **Commit Message Conventions:**
  * Subject line must be as concise as possible (`<type>(<scope>): <summary>`, ≤ 50–72 characters).
  * All detailed analysis, context, rationale, and specific lists of changes must be written in the **body** of the commit (separated from the subject line by a blank line), never crammed into the subject line.

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
  * Dialog dropdown enums (like Component ID via `MAV_COMPONENT`) provide `MatchContains` autocomplete, dynamic hover tooltips (`NAME = VALUE`), and bidirectional conversion between integer IDs and symbolic enum names.
  * In `messageDialog` form row, `System ID` and `Rate (Hz)` are assigned fixed 110px widths while `Component ID` receives all remaining expanding horizontal space.
  * `CompIdLineFilter` intercepts `FocusOut` and `Show` to ensure `cursorPosition(0)` is always enforced, anchoring long enum names from the beginning (e.g. `MAV_COMP_ID_VISUAL_INERTIAL_O`) rather than scrolling to the trailing suffix when text exceeds visible width.
* **Message Dialog Key Handling (Suppressed Enter on Inputs & Focus Clearing):**
  * In `editMessage` dialog, pressing `Enter` while focused on an input field (`QLineEdit`, `QSpinBox`, `QComboBox`, or table cell editor) commits/finishes the active input and clears focus from the field without triggering `Add message` / `Save changes`.
  * Implemented via `MessageDialogKeyFilter` and disabling `autoDefault(false)` & `default(false)` on all buttons in the dialog. The Save button is only activated via explicit mouse click or pressing Enter/Space when keyboard focus is directly on the button.
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

* **TX Card Rate Badge Alignment for Long Titles:**
  * When long message titles wrap across 2 lines (`lines.size() > 1`), `txRatePillRect` vertically centers the interactive rate badge across the full 40px two-line title block (`Y = badge.top() + 11`, center Y = `badge.top() + 20`).
  * Horizontally, `pillLeft` is positioned 8 px after the maximum horizontal advance of both lines (`max(advance[0], advance[1])`), ensuring the badge never collides with either line's text or the top-right status badge button.

* **Permissive Rate Transmission Model (No Pre-Flight Capacity Gating):**
  * `setActive()`, `setRate()`, and `runAll()` allow execution at any user-configured frequency without pre-flight `fitsLink()` rejection or artificial "Error" status transitions.
  * When offered load exceeds physical UART baudrate capacity, transmissions are scheduled normally according to monotonic clocks; hardware/link throughput limits are naturally and transparently reflected in the live **Actual Rate** indicator (`Actual: X Hz`) on each TX card.
  * Error status is strictly reserved for physical/runtime faults (e.g. `Port closed`, `Serial write failure`), never for theoretical bandwidth saturation.

* **TX Stream Isochronous Pacing & Anti-Drift Scheduling:**
  * In `Mav::due(entry, now)`, progression advances deadlines isochronously (`entry.deadline += interval`) to preserve exact transmission frequency, eliminating cumulative phase drift caused by OS scheduler jitter.
  * To prevent catch-up bursts without dropping packets under normal OS multi-tasking jitter (which can delay ticks by 2-3ms), a generous jitter tolerance threshold is enforced (`maxJitterLag = max(20ms, 2 * interval)`). Only true stalls/sleeps exceeding this threshold trigger deadline resynchronization (`entry.deadline = now + interval`).
  * In `SerialWorker::tick()`, bounded micro-catchup (`bursts < 2`) allows recovering up to 2 due frames per tick when a scheduler tick is delayed, keeping high-frequency streams (such as 500 Hz) firmly locked at their exact target rate without jitter fluctuation.

* **Comprehensive Desktop Testing & Modal Dialog Automation:**
  * When testing blocking modal dialogs (`QDialog::exec()`) in QtTest offscreen platform, schedule dismiss handlers via `QTimer::singleShot` with `&window` as context object *before* invoking the triggering action or button.
  * In offscreen QPA, fallback to `QApplication::activeModalWidget()` if `window.findChild<QDialog *>()` returns `nullptr`.
  * Always initialize empty payload fields with valid defaults (`"0"`) before saving in `messageDialog`, preventing validation failures from blocking dialog acceptance and hanging the test suite.
  * Rate formatting in `txTable`: frequencies $\ge 10.0$ Hz format as integers with 0 decimals (`"16"` for `15.5 Hz`), while frequencies $< 10.0$ Hz with fractional parts retain 1 decimal (`"5.5"` for `5.5 Hz`).
  * Serial port baud validation: `currentPortConfig()` strictly assigns `config.baud = 0` for non-numeric text (`"abc"`), allowing `connectSerial()` to reject invalid rates with an immediate modal error dialog instead of silently falling back to 115200.

* **Exhaustive Core Unit Testing & Protocol Verification:**
  * The core telemetry engine (`Mav` namespace in `src/core.*`) is validated by 12 independent, strict sub-suites in `tests/core_tests.cpp` without Qt dependencies:
    1. Type metadata & byte width lookup (`typeName`, `typeSize`, out-of-bounds guards).
    2. Parser noise rejection, single-byte fragmented feeds, and recovery after line corruption.
    3. MAVLink 1 & 2 mixed frame parsing, bad CRC counting, and signature error accounting.
    4. Field reflection for all primitive integers (`int8` through `uint64`), preserving full 64-bit precision without float casting.
    5. IEEE-754 floats and doubles, scientific notation (`1.5e-3`), and strict non-finite (`nan`, `inf`) and overflow rejection.
    6. String/char array bounds, null-termination, numeric array length validation, and enum symbol mapping.
    7. Defensive metadata wire offset validation against buffer overrun.
    8. Full dialect catalog integrity, zero-initialization, and serialization across all messages.
    9. Parameter protocol type-punning via `mavlink_param_union_t` preserving negative signed integers and boundary parsing.
    10. Receiver stream bounding (`MaxEntries = 2048`, `overflow` accounting) and per-device parameter collection isolation.
    11. WriteQueue multi-step partial writes, head frame transitions, no-mutation consumption guarantee, and exact `16384` byte capacity limit.
    12. Monotonic sliding window rates (`ByteRate`), deadline progression without catch-up bursts, link throughput estimation, and bandwidth overload detection.

* **Application Icon & Visual Brand Identity Integration:**
  * Official Logo Identity: **Concept 3: Isometric Monogram M-L** (3D isometric block representing MAVLink GCS and laboratory workbench).
  * Asset Location: Embedded via Qt Resource System (`resources/resources.qrc` mapped to `:/icons/`).
  * Vector master files: `resources/icons/mavlink-lab.svg` (full squircle app icon) and `resources/icons/mavlink-lab-mark.svg` (transparent mark).
  * Multi-resolution raster icons: Bundled from 16x16 to 512x512 PNGs and multi-layer ICO (`resources/icons/mavlink-lab.ico`).
  * Application-wide icon: Instantiated via `MainWindow::appIcon()` and attached to `QApplication::setWindowIcon()` and `MainWindow::setWindowIcon()`.
  * Linux Desktop Integration: `resources/mavlink-lab.desktop` installed to `share/applications`, icons installed to `share/icons/hicolor/` for Ubuntu Dash/Launcher.
  * UI Alignment Invariant: Window title bar heading row begins strictly with `QLabel#heading` at $x=20$ px to maintain exact pixel-level alignment with `txPanel` left boundary (`interactionStates` test check).

---

## 4. Roadmap & Status
* [x] **Phase 1: Foundation & MAVLink Defaults** (Settings dialog cards, default SysID/CompID/Version, QSettings persistence).
* [x] **Phase 3: Workspace & TX Persistence** (Restore TX messages in Stopped state [default: OFF], Reset Layout to defaults, WorkbenchCheckBox).
* [x] **Phase 4: Serial Port Management** (Auto-reconnect handling on unexpected link drop, clean device enumeration).
* [ ] **Phase 2: RX Display & Telemetry** (GUI refresh rate: 1/5/10 Hz, RX stream stale timeout, timestamp display format).
* [ ] **Phase 5: Telemetry Logging** (Non-blocking `.tlog` raw bytes export).

---

## 5. Verification Workflow
* **Mandatory Full Build After Changes:**
  - After any code modification or fix, **always execute a full build (`cmake --build build -j` or `./cli.sh build all`)** to compile all targets, ensuring the main application executable (`build/mavlink-lab`) is updated to the latest revision alongside all test binaries.
  - Never compile only specific test targets (such as `--target desktop-tests`) and conclude, preventing situations where running the application from CLI launches an outdated binary.

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
