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
* **TX Stream Mutual Exclusion (Single Active Stream per 3-Tuple):**
  * An outgoing MAVLink stream is uniquely identified by `sysid + compid + msgid`. Multiple TX cards sharing this identity can be configured as presets, but running them simultaneously causes rate multiplication, bandwidth waste, and receiver state jitter.
  * `SerialWorker` enforces mutual exclusion when `singleActiveStream` is enabled (default: `true`):
    * **Single run:** Activating an entry (`setActive(key, true)`) automatically stops any other running entry sharing the same `(sysid, compid, id)`.
    * **Run all:** `runAll()` starts only the first occurrence of each stream identity group and keeps duplicates stopped.
  * Managed via `WorkbenchCheckBox` in `SettingsDialog` (`Workspace & Session` card, `workspace/singleActiveStream`), persisted in `QSettings` and synced asynchronously to the worker thread.
* **Comprehensive UI Animation Subsystem (120 FPS & `Qt::PreciseTimer`):**
  * All transient interactive UI animations are standardized to high-resolution `Qt::PreciseTimer` (8 ms intervals, ~120 FPS) and smooth quadratic easing curves ($1 - (1-t)^2$) to eliminate Linux kernel event loop coarse timer jitter and stutter:
    1. **TX Duplicate Slide-Down (`startTxDuplicateAnimation`):** 220 ms slide-out from under source card with vertical offset $-rowHeight \to 0$ and alpha fade-in $0.0 \to 1.0$.
    2. **TX Card Keyboard Swap (`startTxSwapAnimation`, `Ctrl+Up`/`Ctrl+Down`):** 180 ms isochronous vertical translation swap between rows with continuous quadratic deceleration.
    3. **TX Undo Toast Entrance/Exit (`TxUndoToast`):** 180 ms slide-up entrance with quadratic ease-out; 150 ms slide-down fade exit with quadratic ease-in ($t^2$).
    4. **Trash Drop Zone (`TrashDropCard`):** Frosted glass acrylic plate starting from `deleteAllButton` and smoothly sliding left by $\text{initialGap}/2$ towards `stopAllButton` while expanding rightward towards `addButton`, maintaining equal rhythm spacing on both sides ($\text{gapRight} = \text{gapLeft} = \text{finalGap} \approx 10\text{--}12\text{px}$); height expands $28\text{px} \to 32\text{px}$ in 220 ms with translucent burgundy/rose glass tint, 1.0px border and frosted reflection highlight; collapses in 180 ms; 120 FPS `Qt::PreciseTimer`.
    5. **Drag-and-Drop Slot Displacement (`dragAnimTimer`):** 8 ms decay step `offset *= 0.74` (derived from $\sqrt{0.55}$ at 16 ms) preserving natural visual pace while doubling smoothness.
    6. **Trash Ghost Card Shrink (`dragDeleteShrinkTimer`):** 220 ms 120 FPS scale and translation into trash drop target.
    7. **TX Card Removal Animation (`startTxDeleteAnimation`):** 220 ms duration, ~120 FPS (`Qt::PreciseTimer`, 8 ms), quadratic ease-out curve ($1 - (1-t)^2$) matching duplicate animation. Exact inverse visual behavior of Duplicate slide-down: the dying card slides vertically upward ($0 \to -rowHeight$) and is clipped at its original cell top boundary (`option.rect.top()`) while fading out ($1.0 \to 0.0$), seamlessly tucking underneath the card above (or behind the top table boundary/header if row 0). Simultaneously, all rows below smoothly slide up by $0 \to -rowHeight$ in unison to close the gap. Active selection highlight is suppressed on the dying card during animation. At $t = 1.0$, `finalizeTxDeleteAnimation()` immediately removes the row from `txTable` (`removeRow(deletedRow)`), updates heights, selects the new occupying row, and sets `pendingDeleteKey` to prevent 200 ms snapshot latency from re-adding the row, achieving a 100% flicker-free transition with zero visual jump (0 px offset). Successive deletions cleanly finalize any in-progress animation immediately. Triggering Undo (`Ctrl+Z` or clicking toast) during the 220 ms window instantly aborts deletion in-place before physical removal.
* **TX Message Duplication (`Duplicate message` Context Action & `Ctrl+D`):**
  * Right-clicking any TX card offers `"Duplicate message"` (or pressing `Ctrl+D`), creating a silent 100% clone (`id`, `sysid`, `compid`, `version1`, `hz`, `payload`) placed immediately below the selected card (at index $i + 1$).
  * The clone strictly starts in the `Stopped` state with zero counters (`count = 0`), preserving the active transmission state of the original card without triggering mutual exclusion stops.
  * Focus and selection automatically transition to the newly duplicated card for rapid inline inspection and editing.
  * **Slide-down Insert Animation (220 ms, High-FPS Smoothness):** Cloned card smoothly slides out from beneath the original card (vertical offset $-rowHeight \to 0$ with quadratic ease-out $1 - (1-t)^2$) and fades in from $0.0 \to 1.0$ opacity. Paced by a high-resolution `Qt::PreciseTimer` (8 ms intervals) over 220 ms to guarantee stutter-free 120 FPS rendering and eliminate Linux timer jitter. The top boundary of the new cell geometry serves as a clip boundary, preventing overlap with the source card above, while all rows below simultaneously push down by $-rowHeight \to 0$. Rapid successive duplications gracefully restart the animation for the newest card without coordinate desynchronization.
* **TX Card Message Presentation & Single-Line Layout:**
  * Fixed row height strictly at 74px (`defaultSectionSize = 74`).
  * **Title-Adjacent Control Flow (Title → Rate → Payload → [gap] → Status):** Badges on Line 1 are positioned immediately following the message title for maximum context association, sharing an identical height of 22px, radius 5px, font 11px Medium, and an airy 8px inter-pill spacing:
    1. Title text: starts at `card.left() + 24`. If longer than available space, it elides with ellipsis (`...`).
    2. Target Rate pill (`X Hz ⌵`): placed immediately adjacent to the title ($8\text{ px}$ spacing, $22\text{ px}$ height, 5px radius).
    3. Payload length badge (`XX B`): placed immediately adjacent to the Rate pill ($8\text{ px}$ spacing, horizontal padding $+14\text{ px}$, $22\text{ px}$ height, 5px radius).
    4. Status badge (`Running` / `Stopped`): pinned at `card.right() - 88` ($80 \times 22\text{ px}$, 5px radius).
  * **Pill Preservation on Long Titles:** Available title width is calculated to always reserve space for both pills before the status button:
    $$\text{maxTitleWidth} = \text{badge.left}() - 8 - (\text{rateWidth} + 8 + \text{payloadWidth} + 8) - \text{textLeft}$$
    Long titles (e.g. `[89] LOCAL_POSITION_NED_SYSTEM_GLOBAL_OFFSET`) are elided with ellipsis (`Qt::ElideRight`), allowing both pills to remain fully visible without pushing down or overlapping. Hovering over an elided card/title displays a tooltip (`QHelpEvent`) showing the complete `[ID] MESSAGE_NAME`.
  * **Guaranteed Bottom Breathing Room:** Metadata row 2 (`Sys · Comp · Actual · Frames`) is fixed at `card.top() + 36` (height 18px), leaving a generous 13px padding before the card bottom border (`y = 68-69`) and completely eliminating any border clipping or overlap.
* **TX Card Message Presentation & Single-Line Layout:**
  * Fixed row height strictly at 74px (`defaultSectionSize = 74`).
  * **Title-Adjacent Control Flow (Title → Rate → Payload → [gap] → Status):** Badges on Line 1 are positioned immediately following the message title for maximum context association, sharing an identical height of 22px, radius 5px, font 11px Medium, and an airy 8px inter-pill spacing:
    1. Title text: starts at `card.left() + 24`. If longer than available space, it elides with ellipsis (`...`).
    2. Target Rate pill (`X Hz ⌵`): placed immediately adjacent to the title ($8\text{ px}$ spacing, $22\text{ px}$ height, 5px radius).
    3. Payload length badge (`XX B`): placed immediately adjacent to the Rate pill ($8\text{ px}$ spacing, horizontal padding $+14\text{ px}$, $22\text{ px}$ height, 5px radius).
    4. Status badge (`Running` / `Stopped`): pinned at `card.right() - 88` ($80 \times 22\text{ px}$, 5px radius).
  * **Pill Preservation on Long Titles:** Available title width is calculated to always reserve space for both pills before the status button:
    $$\text{maxTitleWidth} = \text{badge.left}() - 8 - (\text{rateWidth} + 8 + \text{payloadWidth} + 8) - \text{textLeft}$$
    Long titles (e.g. `[89] LOCAL_POSITION_NED_SYSTEM_GLOBAL_OFFSET`) are elided with ellipsis (`Qt::ElideRight`), allowing both pills to remain fully visible without pushing down or overlapping. Hovering over an elided card/title displays a tooltip (`QHelpEvent`) showing the complete `[ID] MESSAGE_NAME`.
  * **Guaranteed Bottom Breathing Room:** Metadata row 2 (`Sys · Comp · Actual · Frames`) is fixed at `card.top() + 36` (height 18px), leaving a generous 13px padding before the card bottom border (`y = 68-69`) and completely eliminating any border clipping or overlap.
* **TX Card Message Sorting (`sortMessages` Ghost Button & `sortMenu` Popup):**
  * Dedicated 28x28 px frameless ghost button `sortButton` located at Row 0, Column 4 in a 5-column `txHeading` grid layout, vertically aligned flush to the right edge directly above `addButton` (which spans Columns 3-4 in Row 1).
  * **Dual-Pill Status Display (`TxSummaryWidget`):** Streamlined from 3 pills into 2 elegant rounded chips:
    * **Pill 1 (Combined Count & Running Status):** `X messages  ·  Y running` in a unified 6px rounded chip; `Y running` is highlighted in vivid DemiBold green (`#34d399` dark / `#15803d` light) with a subtle green tint background when transmitting (`running > 0`), and subdued neutral slate when stopped.
    * **Pill 2 (Throughput / Wire Load):** `~XX B/s (X.X%)` in telemetry blue chip, dynamically adapting if constrained to ensure zero text clipping.
  * **Minimum Width Enforcement:** Configured `txGroup->setMinimumWidth(465)` and `rxGroup->setMinimumWidth(480)` on the `dashboards` horizontal splitter, preventing the splitter from squeezing the TX Dashboard narrower than 465px. Combined with `MainWindow::setMinimumSize(1000, 720)`, status pills and controls are guaranteed 100% breathing room without any border clipping or truncation across all resize and splitter dragging interactions.
  * Borderless and translucent (`border: none; background: transparent;`) with muted idle icon color (`#8895ad` dark / `#64748b` light).
  * **Hover Icon Brightening:** Because Qt Style Sheets (`QStyleSheetStyle`) ignore `QIcon::Active` on mouse hover, button hover state is handled via `MainWindow::eventFilter` on `QEvent::Enter`/`QEvent::Leave` calling `updateSortButtonIcon()`, dynamically brightening the vector icon stroke to high-contrast foreground (`#edf0fa` dark / `#20263b` light) while keeping background completely transparent.
  * Dedicated popup menu exclusively launched via the header button (no context menu clutter).
  * Reduced width (`min-width: 130px`) and compact padding for a neat fit around the 5 criteria options: `ID`, `Status`, `Payload Size`, `Target Rate`, and `Sys / Comp ID`.
  * Visual indicators: left-aligned vector arrow icon (`sortArrowUp` / `sortArrowDown`) and soft background tint (`item:checked`, ~12-22% opacity) for the currently active criteria; unselected items maintain transparent spacer icons for exact text alignment.
  * Bidirectional toggle: clicking the same criteria toggles between Ascending and Descending order.
  * Rearranging cards manually via drag-and-drop or `Ctrl+Up`/`Ctrl+Down` automatically resets the active sort indicator back to custom.
  * Direct execution on the worker thread via `SerialWorker::sortEntries(criteria, ascending)` using `std::stable_sort`, preserving active transmission states without interruption, and persisting the updated sequence to `Workspace` settings immediately.
  * Disabled automatically when no TX cards exist (`rowCount == 0`).
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
* **TX Card Drag-and-Drop & Animated Trash Dropzone (`TrashDropCard`):**
  * Reordering TX cards uses mouse drag on the grab handle. During dragging, `deleteAllButton` is smoothly replaced by `TrashDropCard`, an overlay widget parented to `txGroup` positioned at `deleteAllButton->pos()`.
  * `batchActions` layout is strictly kept in `(1, 0)` with width ~127px, ensuring `txSummary` pills in `(0, 1)` experience zero layout shift (0 px jitter).
  * `TrashDropCard` expands smoothly from 28px to 190px upon drag start with a solid thin border (1.0px idle, 1.5px hover). When hovering over trash, the floating dragged card retains crisp 1:1 text/badges with an overlaid dense milky frosted glass haze (~51% opacity, alpha 130-135) and thin red border, keeping content fully legible while visually indicating the danger zone.
  * Releasing over trash triggers an interactive shrink-and-fade animation sucking the floating ghost card into the trash can (220ms ease-in), stops active transmissions if running, removes the message, and collapses the dropzone back to 28px (180ms ease-out) before revealing `deleteAllButton`.
* **TX Undo Toast Notification & Smart Keyboard Navigation (`WorkbenchToast`):**
  * When any TX card is removed (via trash drop, context menu "Remove message", or `Delete`/`Backspace` shortcut), an ultra-minimalist floating capsule toast (`WorkbenchToast`, 72x28 px, fully rounded capsule with radius 14 px, text-only "Undo") appears at the bottom-left (`x = 36px`, 24px above status bar) with an airy suspended feel.
  * The toast features a 4.0s perimeter countdown contour running clockwise around the rounded capsule path from top-center, rendered via exact analytical subpaths with `Qt::SolidLine` and round caps to prevent arc chord aliasing. The timer automatically pauses when the mouse hovers over the capsule and resumes on leave.
  * Multi-step LIFO Undo Stack: When any TX card is deleted, it is pushed to an authoritative multi-step stack (`std::vector<DeletedTxItem> txUndoStack`, capped at 64 entries). Pressing `Ctrl+Z` pops and restores the latest deleted card sequentially at its exact row index, completely independent of whether the transient 4.0s toast is visible or expired. Restored cards are highlighted and centered in viewport; `txUndoStack` is cleared safely on `deleteAll` and `importProfile`.
  * The toast pill serves as a tactile button (`PointingHandCursor`): clicking the capsule or pressing `Ctrl+Z` invokes `SerialWorker::restoreEntry(originalRowIndex, entry)`, restoring the entry with 100% configuration preserved, advancing selection to the restored card and dismissing the toast with slide-down animation.
  * Smart Keyboard Shortcuts: `Delete`/`Backspace`/`Ctrl+X` deletes card; `Ctrl+D` duplicates card and selects the clone; `Ctrl+Up`/`Ctrl+Down` reorders the card with an ease-out 140ms swap slide animation; `Space` toggles transmitting state (safely disabled when disconnected); `Ctrl+Z` triggers undo. All shortcuts strictly bypass text inputs (`QLineEdit`, `QTextEdit`, `QSpinBox`) so search filters are never interrupted; `Ctrl+X` in text inputs cuts text normally without deleting cards.
  * Keyboard Shortcuts Reference Dialog (`keyboardShortcutsDialog`): Accessible via `Help -> Keyboard Shortcuts` menu (`actionKeyboardShortcuts`) or global `F1` key (`QKeySequence::HelpContents`). Renders an elegant theme-adaptive cheatsheet displaying keycaps (`Space`, `Ctrl+D`, `Delete`/`Backspace`/`Ctrl+X`, `Ctrl+Z`, `Ctrl+Up`/`Ctrl+Down`, `Esc`, `F1`) and action descriptions inside a frameless `addDialogTitleBar` window with fixed non-resizable geometry (`QLayout::SetFixedSize`, size grip disabled).
  * Menu Shortcut Keycap Pill Badges (`WorkbenchMenu`): Custom `QMenu` subclass across all top-level (`File`, `View`, `Tool`, `Help`) and popup context menus. Overrides virtual `initStyleOption` to strip raw `\t` accelerator text from labels, enforces `min-width: 190px` for ample breathing room, and renders right-aligned keycap pill badges (`r = 3.5px`, $9\text{ pt}$ Normal weight) with subtle hairline semi-transparent border/fill and soft muted text in normal state, switching to translucent white on hover over `#4f46e5` selection.
* **Dashboard Empty & Waiting States (`TxTableWidget` & `RxTreeWidget`):**
  * When `txTable` has 0 rows, `TxTableWidget` renders a minimalist Swiss style empty state directly onto the viewport: an outgoing plane vector glyph, title *"No messages configured"*, and subtitle *"Click '+ Add message' to create your first transmission"*.
  * When `rxTree` has 0 top-level streams, `RxTreeWidget` renders a state-aware waiting state:
    * **Disconnected:** Broken cable/plug vector glyph with *"Serial Disconnected"* and instructions to select a port and connect.
    * **Connected:** Emerald digital UART square waveform vector glyph with *"Connected to <port> (<baud>)"* and *"Waiting for incoming MAVLink packets on serial port..."*.
  * Both states automatically dismiss with zero visual artifacts as soon as items or streams are populated, incurring zero CPU/rendering overhead during active streaming.
* **TX Card Inset Capsule Accent Pill & Payload Wire Size Badge (`TxCardDelegate`):**
  * Refined slim 3px state capsule pill: inset at `card.left() + 8`, vertically centered (`card.top() + 10`, `card.height() - 20`), fully rounded ($r = 1.5\text{px}$), clearly reflecting `Running` (Emerald `#57c99a`), `Error` (Rose `#ef8799`), and `Stopped` (Muted Slate `#3d506e` / `#b8c8dc`) states without breaking the continuous card rounded border ($r = 8\text{px}$).
  * Payload wire size badge: schema payload length (`mavlink_get_msg_entry(msgId)->max_msg_len`) formatted as `X B` (e.g. `9 B`, `28 B`), rendered as a crisp 10px pill badge adjacent to the Target Hz pill.
* **TX Card Geometry Centralization & Minimalist Scrollbar Harmonization:**
  * TX Card bounding geometry is centralized in authoritative static helper `MainWindow::txCardRect(cellRect, animDy)` with 6px left margin and 8px right margin (`adjusted(6, 5 + animDy, -8, -5 + animDy)`), replacing hardcoded offsets across painting, drag-and-drop, and mouse event dispatching.
  * Provides an 8px breathing gap between the card right perimeter and the vertical scrollbar, eliminating cramped visual collision.
  * Global scrollbar styling refined to Swiss / macOS minimalist specifications: slim 6px width/height, fully transparent track (`background: transparent;`), 4px top/bottom margin preventing end-cap truncation, and capsule handle with 3px radius using soft neutral translucency (Dark: `rgba(255, 255, 255, 0.18)` / Hover `0.36` / Pressed `0.52`; Light: `rgba(0, 0, 0, 0.18)` / Hover `0.36` / Pressed `0.52`).
* **Real-time Throughput Monitoring & Link Load Estimation:**
  * Core provides `Mav::ByteRate` (1.0s sliding window), `Mav::estimateTxThroughput()`, `Mav::estimateEntryThroughput()`, and `Mav::formatByteRate()`.
  * Physical framing overhead bit calculation: $\text{bitsPerByte} = 1\text{ (Start)} + N_{\text{data}} + N_{\text{parity}} + N_{\text{stop}}$. Link utilization: $\text{Utilization (\%)} = \frac{\text{Bytes/sec} \times \text{bitsPerByte}}{\text{Baudrate}} \times 100$.
  * TX Dashboard header features a dynamic 3rd pill in `TxSummaryWidget` (`~X B/s (Y.Y%)`) when transmitting, showing pre-flight estimated link load with adaptive saturation theme colors (Blue ≤ 70%, Amber > 70%, Red > 90%).
  * RX Dashboard header features a real-time measured throughput pill (`rxThroughput` badge) right next to `rxSummary` (`X streams`), displaying actual received wire throughput and physical link capacity percentage (`X B/s (Y.Y%)` with 1 decimal place). Hidden when disconnected, visible when connected, with no tooltip, adapting dynamically to theme and link saturation.
  * Status bar displays live measured byte rates (`RX ↓` and `TX ↑`) with tooltips indicating channel capacity utilization and active baudrate.
  * Message Edit Dialog shows pre-flight load estimation badge (`Est. load: ~X B/s (Y.YY% at <baud> <framing>)`) with 2 decimal places and detailed framing tooltip.
  * Message Edit Dialog displays a dedicated schema payload length badge (`X Bytes`) right next to the selected message title (`[ID] NAME`).
* **Comprehensive Performance & Resource Optimization (Zero-Churn & Dynamic Event Loops):**
  * **Dynamic 1ms Pacing Timer:** The worker thread 1ms `Qt::PreciseTimer` sleeps automatically when disconnected or when no periodic messages are active (`hasActiveTransmissions() == false`), eliminating 1,000 idle wakeups/second and saving substantial CPU on Linux.
  * **Zero-Allocation Parser Hot Path:** `Mav::Parser::feed(const uint8_t *data, size_t length)` directly processes raw serial memory slices without constructing intermediate `std::vector<uint8_t>` heap buffers, while `readAvailable()` drains serial chunks in a bounded synchronous loop up to 64 KiB.
  * **Copy-on-Write Parameter Snapshots (`SnapshotParams`):** Snapshot parameters are encapsulated in a copy-on-write wrapper (`SnapshotParams`) referencing a shared immutable map (`std::shared_ptr<const ParamMap>`). Parameter collections are version-tracked (`receiver.paramsVersion`) and only deep-copied when actual `PARAM_VALUE` frames arrive, eliminating continuous heap allocation churn (thousands of map nodes) during 200 ms snapshot broadcasts.
  * **Decoupled Card Height & Font Query Caching:** `updateTxHeights()` layout calculations are decoupled from the 200 ms snapshot loop, triggering strictly when card count or titles change (`titleChanged`), eliminating repetitive `QFontMetrics` wrapping calculations. Fixed-pitch fonts are cached via `cachedFixedFont()` to avoid continuous OS font database queries.
  * **In-Place Telemetry Plotter Channel Tag Updates:** `TelemetryPlotWidget` maintains persistent channel tag components (`tagWidgets_`) and updates numeric value text directly in-place (`updateChannelTagValues()`), entirely eliminating widget destruction and recreation overhead on every 200 ms snapshot.
* **Telemetry Plotter Visual Upgrades & Interactive Visibility:**
  * Soft Area Gradient Fill: Each active channel renders a vertical linear gradient from curve down to baseline (`plotRect.bottom()`, 35–50% alpha down to 0%), giving modern dashboard depth while preserving a crisp 2.0px foreground polyline.
  * Interactive Channel Legend Pills: Clicking any channel tag pill toggles visibility (`channel.visible`). Hidden channels show a hollow dot `○`, 40% dimmed opacity and dashed border, their curves disappear, and auto-scale excludes them dynamically.
  * Dual-Axis Crosshair: Inspection cursor draws both vertical (time) and horizontal (value) dashed hairlines intersecting at cursor position, forming an exact crosshair with inspection tooltip.
* **Modularization of MainWindow & Domain-Specific Translation Units (Phase 1 & 2):**
  * Refactored monolithic `src/window.cpp` (formerly 10,331 lines) into dedicated domain compilation units and 6 decoupled Workbench UI modules housed under `src/window/`:
    1. `src/window.h` & `src/window.cpp` (~1,830 lines): `MainWindow` public API and core lifecycle, central layout, toolbar actions, splitters, window events, and event filter.
    2. `src/window/window_private.h` (~15 lines): Clean Master Header aggregating the 6 Workbench UI modules.
    3. `src/window/workbench_common.h` (131 lines) & `workbench_common.cpp` (485 lines): Common helper utilities (`cachedFixedFont`, `cell`, `messageName`, `isPluggedPort`, `txRatePillRect`, `actionIcon`).
    4. `src/window/workbench_controls.h` (146 lines) & `workbench_controls.cpp` (630 lines): Form controls (`WorkbenchComboBox`, `WorkbenchCheckBox`, `WorkbenchSpinBox`, `WorkbenchLineEdit`, `WorkbenchMenu`).
    5. `src/window/workbench_status_widgets.h` (126 lines) & `workbench_status_widgets.cpp` (760 lines): Status & metric indicators (`TxSummaryWidget`, `ConnectionStatusWidget`, `StatusBarMetricsWidget`, `StatusBarMessageWidget`).
    6. `src/window/workbench_delegates.h` (116 lines) & `workbench_delegates.cpp` (745 lines): Item delegates (`TxCardDelegate`, `ValueDelegate`, `CatalogDelegate`, `RxItemDelegate`, `PayloadIdentityDelegate`).
    7. `src/window/workbench_dialogs.h` (94 lines) & `workbench_dialogs.cpp` (285 lines): Frameless modal dialogs (`DialogTitleBar`, `ConnectionErrorDialog`, `ProfileImportDialog`).
    8. `src/window/workbench_panels.h` (180 lines) & `workbench_panels.cpp` (1,085 lines): Interactive widgets (`TxTableWidget`, `RxTreeWidget`, `TrashDropCard`, `WorkbenchToast`, `CatalogSearch`).
    9. `src/window/window_tx.cpp` (~1,225 lines): TX message dashboard, drag-and-drop, animations, and multi-step undo system.
    10. `src/window/window_rx.cpp` (~625 lines): RX stream tree, snapshot processing, inspector, and parameter editor.
    11. `src/window/window_editor.cpp` (~1,150 lines): MAVLink message catalog search dialog and payload field editor.
    12. `src/window/window_serial.cpp` (~220 lines): Serial port detection, connection lifecycle, and hardware alert dialogs.
    13. `src/window/window_settings.cpp` (~910 lines): Persistent settings, profile import/export, updates check, and keyboard shortcuts.
    14. `src/window/window_theme.cpp` (~350 lines): Dark/Light semantic theme application and QSS stylesheet engine.
  * Preserves 100% public API compatibility of `MainWindow`, enables rapid multi-threaded parallel compilation, and passes all 88 automated tests with zero regressions.
* **Code Style & Formatting:**
  * **BSD/Allman style** across all C++ files: opening braces `{` strictly on their own line for functions, control statements, structs, enums, etc.
  * `camelCase` for variables and functions; `PascalCase` for types and classes; `UPPER_SNAKE_CASE` for macros.
  * Concise Doxygen comments in English (`@brief`, `@param`, `@return`) on all new or significantly modified functions.
* **Commit Message Conventions:**
  * Subject line must be as concise as possible (`<type>(<scope>): <summary>`, ≤ 50–72 characters).
  * All detailed analysis, context, rationale, and specific lists of changes must be written in the **body** of the commit (separated from the subject line by a blank line), never crammed into the subject line.
* **Clarification & Approval Gate Workflow:**
  * Before making any code or project changes requested in user prompts: Must ensure complete and unambiguous understanding of all requirements. Assumptions are strictly forbidden; if any point is unclear or if there are questions/doubts, actively interview and clarify with the user using the `/grill-me` skill until 100% clarity is achieved.
  * Once requirements are understood, **do NOT jump straight into implementation**. Instead, summarize the understood requirements and outline the planned execution steps.
  * Proceed with implementation **only after receiving explicit user approval/confirmation**.

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
  * Top-level popups (`QMenu`, `QComboBoxPrivateContainer`) have opaque rectangular native surfaces on Linux X11/Wayland unless configured with `setWindowFlags(menu->windowFlags() | Qt::FramelessWindowHint)` and `setAttribute(Qt::WA_TranslucentBackground, true)` prior to show/popup.
  * Avoid applying 1-bit raster masks via `setMask(roundedRect)` on translucent popups: XShape 1-bit polygon masks lack antialiasing, and integer polygon quantization errors leave dark/black pixel artifacts at the sharp outer vertices outside the stylesheet's antialiased curved border.
  * Proper architecture: configure `FramelessWindowHint` + `WA_TranslucentBackground` upfront and invoke `clearMask()`, allowing the compositor to render the stylesheet's `border-radius` with true 32-bit ARGB alpha blending and zero corner artifacts.
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
* **TX Card Double-Click Exclusion for Running/Stopped Badge & Rate Pill:**
  * Double-clicking a TX message card in `txTable` opens `editMessage(true)` by default.
  * To prevent accidental modal dialog popups when rapidly clicking or double-clicking interactive buttons on the card, `MainWindow::eventFilter` intercepts `QEvent::MouseButtonDblClick` on `txTable->viewport()`:
    * If cursor coordinates reside within the status badge (`card.right() - 88` to `card.right() - 8`) or the frequency rate pill (`txRatePillRect`), `eventFilter` swallows the double-click event (`return true`), preventing `cellDoubleClicked` from firing.
    * Multiple rapid clicks or double-clicks on the status badge smoothly toggle transmission state via `MouseButtonRelease` without interruption, both when connected and disconnected.
* **IME Bypass for Technical Search and Code Inputs:**
  * Technical inputs and filter fields (`CatalogSearch` used for `rxFilter`, catalog message search, parameter catalog filter; `paramFilter`; `inputEdit` parameter value editor) explicitly set `setAttribute(Qt::WA_InputMethodEnabled, false)` and `setInputMethodHints(Qt::ImhLatinOnly | Qt::ImhNoPredictiveText)`.
  * This bypasses OS-level IME composition (e.g. Vietnamese IBus/Fcitx Telex/VNI pre-edit buffers), ensuring instantaneous single-character key dispatch and immediate `textChanged` emission without requiring trailing space or underscore keys.
* **Search Clear Button Vector '✕' Override:**
  * When `setClearButtonEnabled(true)` is used, Qt attempts to resolve system icons (`SP_LineEditClearButton` / `edit-clear`). On newer OS releases (such as Ubuntu 26.04) or customized desktop icon themes, the system theme may map `edit-clear` to a broom/sweep icon.
  * `CatalogSearch` and search input filters (`rxFilter`, `paramFilter`) explicitly override the embedded `QToolButton` icon with a dedicated vector `✕` (`clearButtonIcon`) featuring smooth antialiasing, balanced 7x7 px geometry (`(4.5, 4.5)` to `(11.5, 11.5)`), and theme-aware colors for both normal and hover states (`#7e899f` / `#8992a6` normal, `#0f172a` / `#f1f5f9` active), guaranteed across both Light and Dark themes.

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
* **RX Dashboard Compact Action Toolbar (`pauseDisplay` & `clearStatistics`):**
  * Modernized from bulky text buttons to compact 34x34 px icon-only controls aligned with `rxFilter` (34 px height matching `rxTree` item rows, `padding: 4px 8px` preventing descender clipping on letters like 'g', 4 px spacing, border-radius 6 px) to maximize search input width.
  * Vector Icons via `actionIcon()` (scaled to 18x18 px):
    * `pauseDisplay`: Dynamic `❚❚` (Pause) ↔ `▶` (Resume). When paused, applies an Amber alert badge style (`#f59e0b` / theme tokens) with instant visual feedback.
    * `clearStatistics`: Backspace erase tag vector icon (left-pointing polygon with `✕` inside) with subtle danger hover highlight (`#ef4444` / `#dc2626`).
  * Keyboard Shortcuts: `Ctrl+P` for Pause/Resume Display and `Ctrl+L` for Clear Received Messages, bound to View menu actions and listed in the F1 Keyboard Shortcuts cheatsheet dialog.
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

* **Real-time Telemetry Plotter & High-Performance Strip-Chart Canvas:**
  * Zero external dependencies: Built a native real-time strip-chart canvas using antialiased vector `QPainter`, eliminating third-party dependencies such as QCustomPlot or QtCharts.
  * Modular Architecture (`src/plotter.h`, `src/plotter.cpp`): Cleanly decoupled from `window.h`/`window.cpp`. Contains `PlotChannel`, `PlotCanvasWidget`, and `TelemetryPlotWidget`. Encapsulates sample ingestion via `TelemetryPlotWidget::processSnapshot(snapshot)`, handling both standard numeric MAVLink streams and Parameter Protocol type-punning (`mavlink_param_union_t`).
  * Pop-out / Floating Window Capability:
    - Provides smooth detachment of `TelemetryPlotWidget` into an independent top-level desktop window (`Qt::Window`) via dedicated pop-out button (`⧉` / `⤓`), enabling dual-monitor setups and full-screen data telemetry inspection.
    - Floating window inherits the application icon (`setWindowIcon`), displays clear title `MAVLinkLab — Telemetry Plotter`, and defaults to an ergonomic 850x550 px layout.
    - Bi-directional synchronization: Docking or closing the floating window seamlessly returns it to `inspectorContentSplitter`, maintaining channel buffers, pause state, and active time-series curves.
    - Application Lifecycle Safe: `MainWindow::closeEvent()` guarantees any active floating plotter window is gracefully closed and cleaned up before serial link teardown.
  * Dedicated Automated Test Suite (`tests/plotter_tests.cpp`): Independently tests channel bounds (max 6, duplicate rejection, index/key removal), snapshot processing, parameter union conversions, canvas controls (time window, auto-scale, pause), hover readout hairline, theme switching, CSV export, and pop-out/docking transitions (`testPopoutAndDocking`).
  * Zero-Allocation Telemetry Rendering: Eliminates all heap allocations in `PlotCanvasWidget::paintEvent()` by querying `Mav::PlotBuffer::rangeSince(startUs)` directly on the ring buffer for AutoScale, and iterating `channel.buffer.at(i)` for polyline curves and hover readouts.
  * Bounded Ring Buffer (`Mav::PlotBuffer`): Fixed capacity of 2048 samples/channel, preallocated once (zero runtime allocations during telemetry streaming), providing query methods `range()`, `rangeSince()`, and `samples()`.
  * Safe numeric field extraction (`Mav::readFieldNumeric`): Supports all primitive integer types (`uint8`..`uint64`, `int8`..`int64`) and floating-point types (`float`, `double`), strictly filtering out character strings (`char[]`) and non-finite values (`NaN`, `Inf`).
  * Intelligent UI integration: Positioned within a `QSplitter` on the right side of `inspectorPane` (hidden by default to conserve resources). Automatically activated when right-clicking any numeric field in `Decoded Fields` or `Parameter Protocol View` and choosing "Plot this field". Supports up to 6 simultaneous channels with high-contrast palette colors.
  * Advanced controls: Dynamic Y-axis auto-scaling (with divide-by-zero protection for flatlines), configurable time windows (10s, 30s, 60s, 120s), Pause/Freeze to inspect historical values, Hairline Cursor hover to inspect timestamped values, and CSV export for offline analysis.
  * Theme Re-entrancy & Stylesheet: Avoid invoking `setStyleSheet()` locally on child widgets during theme transitions; manage CSS centrally in `dataStyle` and protect `applyTheme()` with a re-entrancy guard `static bool applying = false;` to prevent infinite recursion when Qt emits `ApplicationPaletteChange`.
  * Floating-point & Test Context Menus: When verifying floating-point values extracted from single-precision MAVLink messages (`float`), use `std::abs(val - target) < 1e-4` rather than exact equality to prevent IEEE-754 rounding discrepancies. For context menus in `QTableWidget`, trigger via `QPoint(-1, -1)` (keyboard simulation) combined with `setCurrentCell()` to target the selected row accurately without depending on viewport pixel coordinates.
* **Unified Documentation Language Standard:**
  * All documentation files across the project (`README.md`, `AGENTS.md`, `MEMORY.md`, and all documents under `docs/`) must strictly and consistently be written in **English** (with operational agent guidelines provided in `AGENTS.md`). Direct assistant conversational explanations remain in Vietnamese per user preference.

* **Application Icon & Visual Brand Identity Integration:**
  * Official Logo Identity: **Concept 3: Isometric Monogram M-L** (3D isometric block representing MAVLink GCS and laboratory workbench).
  * Asset Location: Embedded via Qt Resource System (`resources/resources.qrc` mapped to `:/icons/`).
  * Vector master files: `resources/icons/mavlink-lab.svg` (full squircle app icon) and `resources/icons/mavlink-lab-mark.svg` (transparent mark).
  * Multi-resolution raster icons: Bundled from 16x16 to 512x512 PNGs and multi-layer ICO (`resources/icons/mavlink-lab.ico`).
  * Application-wide icon: Instantiated via `MainWindow::appIcon()` and attached to `QApplication::setWindowIcon()` and `MainWindow::setWindowIcon()`.
  * Linux Desktop Integration: `resources/mavlink-lab.desktop` installed to `share/applications`, icons installed to `share/icons/hicolor/` for Ubuntu Dash/Launcher.
  * UI Alignment Invariant: Window title bar heading row begins strictly with `QLabel#heading` at $x=20$ px to maintain exact pixel-level alignment with `txPanel` left boundary (`interactionStates` test check).

* **TX Message Card Drag & Drop Reordering (Dynamic Placeholder Slot):**
  * Architecture & Performance: Virtual row remapping via `TxCardDelegate` with `txVisualToSourceRow(visualRow, sourceRow, targetRow)` ensures underlying `QTableWidget` items are not mutated during the drag, delivering flicker-free 60 FPS repaints.
  * Dedicated Drag Handle: 6-dot vector grip (`txDragHandleRect`, $x \in [\text{left}+3, \text{left}+18]$) completely isolates drag gestures from inline rate switcher pills, status toggle badges, and card double-click edit triggers.
  * Snapshot Suspension: While `isDraggingTx == true`, `MainWindow::applySnapshot()` suspends table row count resets to prevent 200 ms snapshot refreshes from interrupting active drag gestures.
  * Concurrency & Persistence: Drop release invokes `SerialWorker::moveEntry(from, to)` via queued invocation, updating `entries` vector in the worker thread and immediately syncing to `QSettings` workspace storage.
  * Safety & Cancel: Releasing outside or pressing `Escape` cleanly cancels the drag operation, resetting visual state and clearing selection.

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
* **CodeGraph & Knowledge Graph Synchronization:**
  ```bash
  codegraph sync && codegraph status
  $(cat graphify-out/.graphify_python) -m graphify.cli --update
  ```
