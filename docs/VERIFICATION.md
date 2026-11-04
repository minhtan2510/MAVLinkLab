# Verification evidence

## Settings helper text removal (2026-11-04)

Removed explanatory helper lines from Settings while preserving titles, control labels and behavior. Build passed; targeted preferences, control-line Settings rendering and interaction checks passed (6 Qt test entries, 0 failures). Reviewed regenerated Light/Dark compact Settings plus pressed/disabled offscreen renders in `build/qa/`. CodeGraph is current; git diff --check passed. Native desktop integration was not tested.

## Control-line Settings checkbox (2026-11-04)

Added **Deassert DTR/RTS on connect** under Settings → Serial Management, enabled by default and persisted immediately in Preferences. Changes apply on the next open, including automatic reconnect, without modifying an active port's control lines. Disabling skips the worker's explicit modem clear/readback.

**Verification:** desktop build passed; CTest **3/3 passed in 19.92 s**. Added preference default/off/on persistence checks and worker disabled-policy coverage proving no modem ioctls while disabled, no immediate ioctl on preference change, and changed policy on automatic reconnect. Reviewed 10 retained offscreen Settings screenshots in `build/qa/control-lines-*.png` covering Light/Dark, checked/unchecked, focus/hover/pressed, disabled and resize; no clipping observed. Existing dropdown and live-theme interaction checks passed. CodeGraph is current and git diff --check passed. Physical VIO RESET/UART/DMA and native desktop integration remain untested.

## Linux control-line deassertion (2026-11-04)

After open and automatic reconnect, the worker clears DTR/RTS simultaneously and verifies driver readback. Hardware RTS/CTS retains driver ownership of RTS and clears/verifies DTR only; use Flow control None for the VIO policy. Control-line failure closes the port before Connected; reconnect continues retrying. Only ENOTTY from an identified slave PTY bypasses modem-line setup.

**Verification:** full desktop build passed; CTest **3/3 passed in 17.46 s** (core, desktop, control-lines). The dedicated suite uses a real PTY for QSerialPort I/O and linker-wrapped worker modem ioctls for deterministic success/failure, asserted readback, hardware RTS ownership, EINTR retry, PTY-only ENOTTY exemption, and reconnect recovery after readback failure with no automatic TX. Existing desktop/PTY checks passed and their screenshots remain in `build/qa/`; no new visual review was performed for this transport-only change. CodeGraph sync/status and git diff --check passed. No Gimbal was connected: physical RESET, USB bridge behavior, UART, DMA and native desktop integration were not verified. Driver bit readback is not an electrical measurement, and a pulse during kernel open/close remains possible.

**Date:** 2026-11-02. **Host:** Ubuntu 22.04, GCC 11.4.0, CMake 4.3.1.

## Dependencies used

- **MAVLink submodule:** `a2d0ecf011a4aaa7840480e43f4e38d8c9e79d29`; generated headers are unchanged.
- **Qt:** 6.2.4 packages extracted into `/tmp/mavlink-qt-prefix/usr`.
- **Qt SerialPort:** official `qt/qtserialport` tag `v6.2.4`, commit `f41e290b17e68764da0bf88ded99f2180d79b83b`, built into the same temporary prefix.
- System package installation was authorized but could not run because `sudo -n` required a password. No system package installation was performed. The inspected Ubuntu Jammy catalog also lacked `qt6-serialport-dev`.
- The temporary Qt prefix is a local verification environment, not a deployed or portable distribution. It must remain available for the current build to run.

## Completed checks

| Level | Result | Evidence and scope |
| --- | --- | --- |
| Core build | **Pass** | C++17 core-only CMake build, without Qt |
| Full desktop build | **Pass** | Qt Widgets/SerialPort application and tests compiled with `-Wall -Wextra -Wpedantic`; final build had no warnings |
| CTest | **2/2 pass** | Core suite and desktop suite; final run completed in approximately 6.36 seconds |
| Codec/parser | **Pass** | Real generated catalog bounds, serialization, fragmented input, noise, CRC rejection, multiple frames/read, MAVLink 1/2 and distinct sender/component identities |
| Timing/validation | **Pass** | One-second rate expiry, missed-deadline handling, aggregate baud capacity, integer/float/array boundaries and bounded partial-write queue |
| PTY serial | **Pass** | No TX on connect; Send Once; fragmented RX and component isolation; periodic TX; disconnect cancellation; silent reconnect; invalid device; capacity rejection |
| PTY backpressure | **Pass** | Unread PTY master stalls output; total pending output stays bounded; queue overflow stops the periodic entry; disconnect empties pending bytes |
| Qt widget actions | **Pass** | Add/edit validation, Send Once, Start/Stop, RX field inspection, Pause/Resume, Clear, disconnect action state and selection retention |
| Theme changes | **Pass** | Light/Dark controls and simulated application-palette changes in System mode |
| Rendered GUI | **Reviewed offscreen** | Dashboard at 1440×900 and minimum 1000×720; message editor/validation; disabled, focus, hover, pressed and popup states in Light/Dark |
| ASan/UBSan core | **Pass with limitation** | `ASAN_OPTIONS=detect_leaks=0`; no address/undefined-behavior diagnostic. LeakSanitizer cannot operate in the execution environment and is not claimed as passed |
| Physical UART / DMA | **Not tested** | No physical-device or kernel-driver DMA evidence was collected |
| Native desktop integration | **Not tested** | Qt offscreen platform does not exercise the real window manager, keyboard grabbing or desktop theme plugin |

The offscreen plugin emits expected messages about unsupported raising, keyboard grabs and size-hint propagation. The assertions and rendered screenshots still completed; these messages are not evidence of native desktop behavior.

## Reproduce the current temporary-prefix build

```bash
export LD_LIBRARY_PATH=/tmp/mavlink-qt-prefix/usr/lib/x86_64-linux-gnu
export QT_PLUGIN_PATH=/tmp/mavlink-qt-prefix/usr/lib/x86_64-linux-gnu/qt6/plugins
cmake -S . -B build -DCMAKE_PREFIX_PATH=/tmp/mavlink-qt-prefix/usr
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/mavlink-simulator
```

The CTest desktop test selects `QT_QPA_PLATFORM=offscreen`; the regular application uses the available native platform unless the environment overrides it. The temporary prefix may be removed by system cleanup/reboot. For a persistent build, use a complete matching Qt installation and reconfigure CMake.

Core sanitizer run:

```bash
cmake -S . -B build-sanitize -DBUILD_GUI=OFF \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined
cmake --build build-sanitize -j
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-sanitize --output-on-failure
```

Rendered artifacts are generated by the desktop suite in `build/qa/`. They are build artifacts and remain outside version control.

## Source-grounded limitations

- `third_party/mavlink/mavlink_types.h` provides field types, array lengths and wire/structure offsets, but no units.
- `third_party/mavlink/minimal/mavlink_msg_heartbeat.h` owns the `mavlink_version=3` constant. HEARTBEAT uses its generated packer and the editor presents that field read-only.
- Qt SerialPort 6.2.4's Unix backend configures termios and uses descriptor read/write notifications; it does not expose an application DMA configuration API. It restores saved termios settings when closing the port. Those source facts do not establish physical DTR/RTS behavior on a specific device.
- Stop/edit/remove discards shared pending output; already transmitted bytes cannot be recalled and a partially transmitted frame can be interrupted.
- Use **8 data bits** for unescaped binary MAVLink. Other serial configurations depend on driver/peer support and can truncate binary bytes.

## Compact dashboard revision

The UART controls now occupy a **54 px** single-row connection bar at the tested default font/scale. **Settings** reveals the additional framing/flow controls. Widget tests verify the collapsed height, disclosure, preservation of stop-bit selection and disabled configuration while connected. Dashboard cards, action hierarchy, summary badges and field inspector layout were refreshed.

**Verification:** full desktop build and CTest **2/2 passed**. Offscreen renders were reviewed at 1440×900 and 1000×720 in Light/Dark, including expanded Settings, dropdowns, disabled controls, hover, press, focus and application-palette changes. No transport/core behavior was changed; PTY tests remain passing. The same native-desktop and physical-UART limitations above apply.

## Workbench visual refactor

The current UART bar measures **50 px** at the tested default font/scale. RX controls share its dashboard title row. Tables use flat surfaces, lighter selection and TX status badges; empty states distinguish waiting/listening/no filter matches. The message editor now separates its catalog and payload into two columns, with themed numeric steppers and an explicit primary Save button.

**Verification:** full Qt build completed without compiler diagnostics; CTest **2/2 passed in 6.59 s**. Existing core and PTY serial/backpressure checks remain passing. Added widget checks cover no-match filtering with selection retention, numeric step-button hit areas, action bounds at 1000×720 and RX/inspector columns fitting without horizontal scrolling for the HEARTBEAT fixture. Offscreen Light/Dark renders were reviewed for populated and empty dashboards, minimum window size, message validation/editor, advanced serial settings, disabled controls, hover, press, focus and popup selection. CodeGraph was synced and reports up to date.

**Artifacts:** `build/qa/dashboard-{light,dark}.png`, `dashboard-small-dark.png`, `empty-{light,dark}.png`, `empty-small-dark.png`, `message-dark.png`, `message-validation-light.png` and interaction-state captures. Native desktop integration and physical UART/DMA remain untested; this revision changes GUI code only.

## Modern dashboard direction

After the user selected a more spacious Modern dashboard direction, the workspace was rebuilt as a TX card column and a larger RX/Inspector column. Typography, action placement, spacing and both palettes were revised together. The UART bar remains compact: **53 px** at the tested default font/scale.

**Verification:** full Qt build passed without compiler diagnostics; CTest **2/2 passed in 8.28 s**, including existing core/PTY/backpressure checks. A new populated-dashboard widget scenario creates four messages through the editor, verifies mouse/keyboard card selection and retention across refresh, starts a HEARTBEAT and receives three synthetic message types from two components through a PTY. Screenshots cover running/stopped cards, hover, RX selection and decoded fields in both themes. Existing minimum-size, filtering, validation, theme changes, focus, press, popup and disabled-state checks also pass. Reviewed renders remain **offscreen**, not native-desktop or physical-UART validation.

**Current populated previews:** `build/qa/modern-populated-light.png` and `build/qa/modern-populated-dark.png`. Other dashboard/editor/state captures use the same paths listed above and are regenerated by the test suite. CodeGraph was synced and reports up to date.

## Information hierarchy refinement

The accepted two-column layout is retained. Received messages and decoded fields now have separate bordered frames, title bars and stronger header/body tones. TX cards use a distinct list background, stronger outlines, a state accent, emphasized sender values and separated bold rate values. A shared value delegate emphasizes decoded values and editable payload values while retaining native selection and editing. The status bar also has a separate surface and border.

**Verification:** the full desktop build passed without compiler diagnostics; CTest **2/2 passed in 8.45 s**, including core, PTY/backpressure and widget scenarios. Final offscreen renders were reviewed in Light/Dark for populated and empty dashboards, minimum size, message editor/validation, hover, press, focus, disabled controls, popup selection and expanded Settings. Contrast calculations for the principal header text, value text and TX rate text pairs range from **8.49:1 to 14.98:1**; these calculations do not constitute a complete accessibility audit. CodeGraph reports up to date. Native desktop rendering and physical UART/DMA were not retested. Existing application instances are left running and require reopening to use the rebuilt UI.

The current populated previews retain their paths: `build/qa/modern-populated-light.png` and `build/qa/modern-populated-dark.png`.

## Softer tones and button focus correction

Header fills, data borders, card accents and selection were softened while retaining the separate data frames. Sender/rate/value text now uses regular weight; message names retain their hierarchy. The duplicate button focus decoration was removed: mouse press uses ordinary pressed feedback, while keyboard focus uses one explicit border with stable overall geometry. Native button focusability and keyboard activation are retained.

**Verification:** full build passed; CTest **2/2 passed in 8.44 s**. Widget checks now distinguish Tab focus from pointer press, assert unchanged button bounds, and verify that Space re-enables the keyboard indicator and toggles Settings. Offscreen Light/Dark captures were reviewed for pointer press, keyboard focus/activation, hover, checked/disabled states, popup rows, populated dashboards and the editor. New captures are `build/qa/keyboard-settings-{light,dark}.png`; existing screenshots were regenerated. CodeGraph reports up to date. Native desktop focus rendering and physical UART/DMA are not claimed as verified by these checks.

## Quick launcher

`run.sh` launches one instance, or two independent instances with `--two`, using Qt paths discovered from the selected build's CMake cache. No machine-specific Qt prefix is embedded. It does not create virtual ports or start communication.

**Verification:** `bash -n run.sh` and `./run.sh --help` passed. Offscreen smoke launches from `/tmp`, with `LD_LIBRARY_PATH` and `QT_PLUGIN_PATH` initially unset, verified one application process in normal mode and two application children in `--two` mode. Both started with the configured Qt runtime and were stopped after the check; existing desktop instances were untouched. No GUI or transport source was changed. CodeGraph remains up to date.

## UART configuration redesign

UART configuration now groups a framing/flow summary, labeled Serial port and Baud rate inputs, an accessible refresh icon, Advanced disclosure and separated connection actions. The collapsed bar measures **63 px** at the tested font/scale. Expanded parameters use four evenly spaced labeled fields and preserve values when hidden. Empty-state text spacing was tightened to avoid clipping when the advanced section is open at minimum window size.

**Verification:** full build passed; CTest **2/2 passed in 8.48 s**. Widget checks cover typed PTY path preservation through Refresh, framing/flow summary updates, stop-bit retention when collapsed, configuration disabled while connected, keyboard disclosure and all advanced controls staying inside their panel at 1000×720. Offscreen Light/Dark review covered collapsed/expanded configuration, populated and empty dashboards, minimum size, disabled controls, press, hover, keyboard focus and popup rows. Added artifacts: `build/qa/serial-settings-small-{light,dark}.png`; existing dashboard and Settings captures were regenerated. CodeGraph reports up to date. Serial transport behavior was retained; native desktop rendering and physical UART/DMA are not established by these checks.

## Reduced dashboard descriptions

Removed the app subtitle, dialect label and TX/RX footer copy. View tooltips retain delivery/rate/signature details. Debug metrics and connection/error text remain in a transparent, borderless status bar.

**Verification:** full build passed; CTest **2/2 passed in 8.50 s**. Reviewed regenerated offscreen Light/Dark populated dashboards, minimum-size expanded UART and keyboard-focus captures; the existing widget suite also covers hover/press, disabled controls, popup rows and theme changes. CodeGraph reports up to date and `git diff --check` passed. Native desktop and physical UART/DMA were not retested.

## Serial port alignment

The Serial port field now starts at the left content edge of the connection bar. Removed the UART heading and moved the live framing/flow summary beside Baud rate, aligned with the control row. Existing configuration values and summary updates are retained.

**Verification:** full build passed; CTest **2/2 passed in 8.52 s**. Reviewed regenerated offscreen Light/Dark layouts, including expanded Advanced at 1000×720 and connected controls. Existing interaction/state checks passed. CodeGraph is up to date; `git diff --check` passed. Native desktop and physical UART were not retested.

## Serial field spacing

Increased label/control spacing from 2 to 6 px and connection-card vertical padding from 7 to 10 px. The collapsed card is now 73 px at the tested font/scale; the compact-height check allows up to 76 px.

**Verification:** full build passed; CTest **2/2 passed in 8.51 s**. Reviewed regenerated offscreen Light/Dark renders, including Advanced expanded at 1000×720; no clipping was observed. Existing interaction/state checks passed. CodeGraph is up to date and `git diff --check` passed. Native desktop and physical UART were not retested.

## Workspace gap and secondary typography

Reduced the connection-to-dashboard gap from 16 to 12 px while preserving the header gap. Shared serial field labels are now 12 px (previously 11 px). Status labels explicitly use muted 12 px type; the metrics label has an 18 px content inset in addition to the status bar's native 2 px inset, aligning its text with the 20 px dashboard margin in the tested render.

**Verification:** full build passed; CTest **2/2 passed in 8.57 s**. Reviewed regenerated offscreen Light/Dark populated layouts and the minimum-size Advanced layout; status alignment and readability were checked visually. Existing widget interaction/state tests passed. CodeGraph is up to date and `git diff --check` passed. Native desktop and physical UART were not retested.

## Blank message views

Removed placeholder/guidance overlays from TX, RX and decoded-field viewports, including the no-matching-filter message. Empty views now remain blank while titles, table headers and actions stay available. Removed the unused overlay helper/state updater and adjusted widget checks to verify blank TX and actual filter visibility/selection behavior.

**Verification:** full build passed; CTest **2/2 passed in 8.63 s**. Reviewed regenerated offscreen Light/Dark empty dashboards. Existing populated, resize, theme and interaction checks passed. CodeGraph is up to date; `git diff --check` passed. Native desktop and physical UART were not retested.

## Dashboard gaps and passive hover

Reduced the horizontal TX/RX splitter width from 16 to 12 px. Removed general tooltips from the TX/RX/field views and decoded-field heading, and removed splitter hover highlighting. Message-item and action tooltips remain; splitters remain draggable.

**Verification:** full build passed; CTest **2/2 passed in 8.41 s**. Reviewed regenerated offscreen Light/Dark populated dashboards for spacing. Existing widget interaction tests passed; source review confirms no view-wide tooltip or splitter hover-fill rule remains. CodeGraph is up to date and `git diff --check` passed. Native desktop and physical UART were not retested.

## Developer documentation audit

**Baseline:** `main` at `98b3457` after merging `ui_dev`. Added source-backed `ARCHITECTURE.md`, `API.md` and `TROUBLESHOOTING.md`. Reviewed tracked application/core/worker headers and implementations, CMake, launcher, submodule configuration, both test suites and existing documentation. Updated stale rate typography, spacing and inspector-tooltip descriptions in `DESIGN.md`. No application source or dependency revision was changed by this documentation task.

**Current checks:** core-only configure/build and CTest **1/1 passed**. Full configure and clean build with the temporary Qt prefix passed; CTest **2/2 passed in 8.55 s**. Configure reported missing XKB/Vulkan-header discovery, but generation/build completed; this does not establish native-platform feature availability. `bash -n run.sh` and `./run.sh --help` passed. The in-memory HEARTBEAT example from `API.md` was extracted, compiled against `mavcore` with C++17 and warning flags, and exited successfully. Local Markdown links, required document presence, code fences and trailing whitespace were checked.

An earlier full run during the checkout transition failed `desktopFlow` at the Connect/Disconnect assertion and subsequently aborted in `modernDashboard`. The clean rebuild after the merge passed; the failure's precise cause was not isolated, so it is not claimed as a repaired application bug. Previous verification sections are historical runs, not new hardware evidence. Physical UART/DMA and native desktop integration were not retested in this documentation task.

## Shared identity and UART header

Moved the app title and UART controls into one compact header card. Removed the decorative monogram, allowed the port input to shrink to 140 px, and moved the framing/flow badge into Advanced. The header returns to its compact height when Advanced closes, leaving more room for the dashboards.

**Verification:** full build passed; CTest **2/2 passed in 8.86 s**. Added minimum-size checks for header control containment, horizontal ordering and title/control row alignment in Light/Dark. Reviewed offscreen collapsed Dark and expanded Light at 1000×720, alongside the existing interaction checks for hover, press, keyboard focus, popup rows, disabled configuration and theme changes. CodeGraph is up to date. Native desktop and physical UART/DMA were not retested.

## Separate title and UART card

The title now sits directly on the main background at the left content edge. Only the UART configuration has a card on the right, on the same row. Advanced expands within that card while the title stays aligned with its first control row.

**Verification:** full build and CTest **2/2 passed in 8.81 s**. Widget checks verify the title is outside the UART card, precedes it horizontally and aligns with its controls in Light/Dark at 1000×720. Reviewed regenerated offscreen collapsed Dark and expanded Light renders; existing interaction checks passed. CodeGraph is up to date; `git diff --check` passed. Native desktop and physical UART/DMA were not retested.

## Compact right-aligned UART group

Removed the outer UART background/border and the connection-status group background/border. UART controls now form a compact group (maximum 700 px wide) at the right content edge, with a fixed 160 px port input and no internal expanding spacer before connection status. The title stays on the left; Advanced retains its inset settings panel.

**Verification:** full build passed; CTest **2/2 passed in 8.74 s**. Minimum-size Light/Dark widget checks verify the UART width cap and right-edge alignment with the dashboards. Reviewed regenerated offscreen collapsed Dark and expanded Light renders; existing interaction checks passed. CodeGraph is up to date; `git diff --check` passed. Native desktop and physical UART/DMA were not retested.

## Grouped UART actions and settings popup

Grouped Port/Refresh with a 4 px gap and separated Baud by 12 px. Replaced the Advanced text button with a theme-aware Settings vector icon. Connect/Disconnect is now 32 px high; the fixed-width 11 px connection status and LED sit beneath it. Removed the status divider. UART settings use a two-column QWidgetAction/QMenu popup, with focus entering Data bits and Escape dismissal, without expanding the header or moving the dashboards.

**Verification:** full build passed; CTest **2/2 passed in 9.00 s**. Updated widget checks for unchanged header height on popup opening, retained values, disabled configuration while connected, keyboard activation and Escape dismissal. Reviewed regenerated offscreen Light/Dark empty/populated dashboards and settings popups, including minimum-window layout and disabled controls. Existing hover/press, focus, dropdown popup and theme-change checks passed. Earlier runs failed an obsolete assertion expecting focus to stay on Settings while its popup was open; the check now verifies keyboard activation and popup dismissal. CodeGraph is up to date; `git diff --check` passed. Native desktop popup integration and physical UART/DMA were not retested.

## Remove UART status line

Removed the Connected/Disconnected label and connection LED beneath Connect, including their unused helper/state/style definitions. The UART header returns to a single 32 px control row with its existing margins. The Connect/Disconnect action retains its connection-specific styling.

**Verification:** full build and CTest **2/2 passed in 8.87 s**. Reviewed regenerated offscreen Dark empty and Light connected/populated layouts. Existing popup, interaction, minimum-size and PTY checks passed. CodeGraph is up to date; `git diff --check` passed. Native desktop and physical UART/DMA were not retested.

## Raise dashboards with tighter header spacing

Reduced the top content inset from 16 to 10 px, the title/UART header height from 52 to 40 px (UART vertical margins from 10 to 4 px), and the header-to-dashboard gap from 12 to 8 px. The dashboards start 22 px higher at the tested font/scale, gaining the same amount of vertical content space.

**Verification:** full build and CTest **2/2 passed in 8.83 s**. Reviewed regenerated offscreen Dark and minimum-size Light layouts; existing interaction, popup, resize and PTY checks passed. CodeGraph is up to date; `git diff --check` passed. Native desktop and physical UART/DMA were not retested.

## UART Settings sliders icon

Replaced the UART Settings gear with a local vector icon showing three vertical sliders. Existing theme colors, button size, accessible name, tooltip and popup behavior remain in use.

**Verification:** full build and CTest **2/2 passed in 9.00 s**. Reviewed regenerated offscreen Light/Dark layouts for icon visibility; existing interaction and popup checks passed. CodeGraph is up to date; `git diff --check` passed. Native desktop rendering was not retested.

## Continuous UART popup background

Moved the UART popup background and single border onto QMenu#serialMenu, removed its outer padding, and made the embedded serialAdvanced frame transparent/borderless. This removes the different-colored menu gutter around the settings panel.

**Verification:** full build and CTest **2/2 passed in 8.88 s**. Reviewed regenerated offscreen UART popup captures in Light/Dark; the background now fills the popup with one outer border. Existing interaction checks passed. CodeGraph is up to date; `git diff --check` passed. Native desktop rendering was not retested.

## Rounded UART popup window corners

Enabled a translucent background for the UART QMenu and applied a matching 7 px rounded window mask on Show/Resize. This clips the actual popup window beyond the rounded stylesheet border instead of leaving rectangular corner pixels.

**Verification:** full build and CTest **2/2 passed in 8.82 s**. Reviewed offscreen Light/Dark popup captures and checked that all four outer corner pixels have alpha 0 in both themes. Existing interaction checks passed. CodeGraph is up to date; `git diff --check` passed. Native compositor behavior was not retested.

## Compact TX and clearer RX/Inspector

Reduced TX rows from 132 to 108 px, moved message names to the first line, combined ID/Sys/Comp, and grouped Target/Actual/Frames below a subtle divider. Kept Add in the title row and Stop all beside the summary. The selected-message toolbar now groups Send once, mutually exclusive Start/Stop and accessible Edit/Remove icon buttons, with a selected-message caption. Inline card toggles remain available.

Reduced RX row padding from 8 to 5 px and muted System/Component text, including immediate theme-change updates. Softened RX/Inspector borders and title-bar backgrounds. Inspector headings now identify message name/ID/System/Component. A field-row context menu offers Copy value; its displayed value is captured when the menu opens so later refreshes do not change the copied text. Empty message views remain blank.

**Verification:** full build and CTest **2/2 passed in 9.33 s**. Added checks for clipboard content, inspector identity when switching components, Start/Stop visibility and selected-message toolbar containment/order at 1000×720. Existing PTY, editor, inline toggle, mouse/keyboard, hover/press, disabled control, popup and system/theme-change checks passed. Reviewed regenerated Light/Dark populated renders at 1440×900 and 1000×720, plus the empty minimum-size view. Artifacts include `build/qa/modern-small-{light,dark}.png`. CodeGraph is up to date; `git diff --check` passed. Native desktop clipboard/compositor integration and physical UART/DMA were not tested in this change.

## Debug metric separators

Added spaced vertical bars between RX, TX, CRC errors, Pending and RX overflow, in both the initial and refreshed status text. Existing alert colors and muted typography remain.

**Verification:** full build and CTest **2/2 passed in 9.26 s**. Reviewed regenerated offscreen minimum-size Light/Dark status rows for separation and fit. CodeGraph is up to date; `git diff --check` passed. Native desktop rendering was not retested.

## Align app and TX titles

Added a 17 px left content inset to the app title, aligning its text with the TX Dashboard title (16 px panel padding plus the 1 px panel border).

**Verification:** full build and CTest **2/2 passed in 9.32 s**. Added a widget check comparing title content left edges in Light/Dark; reviewed minimum-size renders in both themes. CodeGraph is up to date; `git diff --check` passed. Native desktop rendering was not retested.

## Align app title with TX card edge

Corrected the previous alignment interpretation: removed the app title's 17 px inset so it aligns with the outer left edge of the TX Dashboard card, rather than the text inside it. Updated the widget check to compare against the card edge.

**Verification:** full build and CTest **2/2 passed in 9.27 s**. Reviewed minimum-size offscreen Light/Dark layouts. CodeGraph is up to date; `git diff --check` passed. Native desktop rendering was not retested.

## Remove selected-message caption

Removed the Selected caption and its unused label/update code from TX Dashboard. The action toolbar sits directly beneath the message list; selection and action enablement remain in use.

**Verification:** full build and CTest **2/2 passed in 9.38 s**. Reviewed regenerated offscreen Light/Dark populated layouts. Existing interaction checks passed. CodeGraph is up to date; `git diff --check` passed. Native desktop rendering was not retested.

## TX message card hierarchy

Restyled TX entries with soft 1 px outlines and a light selection tint, removing the side accent and inner divider. Each 120 px row separates message identity from three aligned metrics: 11 px labels above 14 px values. Sender labels explicitly identify SYS and COMP. Values elide within their columns; full information remains in tooltips. Cleared rectangular selection/hover fills outside rounded cards. Inline state toggles and keyboard/toolbar actions remain available.

**Verification:** full build and CTest **2/2 passed in 9.34 s**, including PTY scheduling, inline toggle, selection, keyboard, hover/press, disabled controls, popup and theme-change checks. Reviewed regenerated Light/Dark offscreen populated renders at minimum size, plus the full-size Dark render. Native desktop rendering and physical UART/DMA were not retested.

## TX ID beside message name

TX card headings now use `[ID] MESSAGE_NAME`, for example `[0] HEARTBEAT`. Removed ID from the secondary line; SysID/CompID start at the same left content edge as the heading. Long headings retain ellipsis and full-identity tooltips.

**Verification:** full build and CTest **2/2 passed in 9.32 s**. Reviewed minimum-size Light/Dark offscreen renders for heading/sender alignment and long-name clipping. Existing interaction checks passed; native desktop rendering was not retested.

## Remove TX card tooltips

Removed message/status tooltips from TX entries while retaining the full accessible description. Updated the existing desktop check to verify an empty card tooltip and preserved accessible rate information.

**Verification:** full build and CTest **2/2 passed in 9.39 s**. CodeGraph is up to date; `git diff --check` passed. Native desktop tooltip behavior was not separately tested.

## Start newly added messages when connected

After accepting a new message, the GUI queues saveEntry followed by setActive when connected. Existing worker validation still rejects excessive offered load and closed ports. Offline additions remain stopped; connect/reconnect does not activate entries. Editing retains its stop-on-save behavior. Updated the editor description and usage documentation.

**Verification:** full build and CTest **2/2 passed in 9.50 s**. The desktop PTY flow now verifies Running state, frame count and received output immediately after adding, before any explicit Start/Send once action, then verifies Stop. Existing worker capacity, disconnected-connect silence and dashboard interaction checks passed. CodeGraph is up to date; `git diff --check` passed. Physical UART was not tested.

## Narrower TX message cards

Reduced message heading type from 15 to 14 px and horizontal content padding to 12 px. Reduced the initial TX splitter size from 400 to 360 px and its minimum from 360 to 340 px, leaving more space for RX while preserving manual splitter resizing. Card height and inline toggle geometry remain unchanged.

**Verification:** full build and CTest **2/2 passed in 9.50 s**. Reviewed minimum-size offscreen Light/Dark renders: toolbar controls remain contained, metric columns fit, and long headings elide before the state button. Existing hover, pressed, keyboard, disabled, popup, theme-change and PTY checks passed. Native desktop rendering was not retested.

## Compact TX typography and vertical spacing

TX headings and metric values now use 13 px type; sender/state and metric labels use 11 px. Headings align vertically with the Running/Stopped control. Sender text uses `System ID: … - Component ID: …`. Reduced row height from 120 to 108 px and tightened internal spacing while retaining the existing state-button hit area. Disconnect uses normal font weight.

**Verification:** full build and CTest **2/2 passed in 9.56 s** after the final spacing change. Reviewed offscreen minimum-size Light/Dark populated renders for label/value spacing, containment and alignment. Existing interaction and PTY checks passed. Native desktop rendering was not retested.

## Theme-aware dialog decorations

Replaced native decorations for Settings, About and the message editor with a shared application-painted title bar. Header background, text, border and close button follow the current theme. About uses a normal themed QDialog instead of the static QMessageBox helper. Title-bar dragging requests Qt system movement and falls back to pointer movement when unsupported; QDialog size grips preserve resizing. Close actions reject dialogs, and Escape remains native QDialog behavior.

**Verification:** full build and CTest **2/2 passed in 9.77 s**. Added offscreen checks of actual header pixel colors while switching an open Settings dialog between Light/Dark, and checks of About title-bar colors, close button and Escape dismissal. Reviewed Light/Dark Settings/About and Dark message-editor renders. Existing focus/hover/press, disabled controls, popup, resize, system-theme, editor and PTY checks passed. Native compositor dragging, window decorations and physical UART were not tested. CodeGraph is up to date; `git diff --check` passed.

## Project information in About

Expanded About with the CMake project version, serial workbench purpose, TX/RX features, MAVLink 1/2 and ardupilotmega/common support, official library and Ubuntu/Linux C++17/Qt stack. Version comes from PROJECT_VERSION through a private desktop compile definition. Rich-text content has sufficient minimum height to avoid clipping its last line.

**Verification:** full build and CTest **2/2 passed in 9.72 s**. Reviewed offscreen Light/Dark About renders and corrected last-line clipping; existing dialog close/Escape and theme checks passed. Native desktop rendering was not tested.

## TX card context actions

Removed the TX bottom toolbar and replaced its widgets with QAction entries in a per-card context menu: Send once, Start/Stop, Edit message and Remove message. Right-click selects its target row before opening the menu; invalid rows do not open it, and keyboard context requests use the current row. Start/Stop visibility and connection-based enablement remain synchronized with snapshots. Double-click editing and inline state controls remain available.

**Verification:** full build and CTest **2/2 passed in 10.12 s**. Updated desktop flow to use actions and verified that opening the menu on the second card selects it and starting/stopping it leaves the first card running. Reviewed Light/Dark context menus and the minimum-size populated TX layout with the bottom toolbar removed. Existing editor, disconnected enablement, inline toggle, theme, popup and PTY checks passed. Native desktop context-menu integration was not tested.

## TX batch controls

Added Run all, Stop all and Delete all below the TX list and removed the upper Stop all button. Per-card context actions remain. Worker runAll validates aggregate load before activating entries; rejection leaves prior active states unchanged. deleteAll stops scheduling, cancels pending output and clears TX entries. Run/Stop are disabled when disconnected or empty; Delete remains available offline when entries exist.

**Verification:** full build and CTest **2/2 passed in 11.17 s**. Added PTY coverage for aggregate overload rejection, successful batch transmission, deletion and post-delete silence/pending count. GUI checks exercise bottom button placement, Stop all, Run all, Delete all and empty-state disablement. Reviewed minimum-size Light/Dark renders. Existing per-card menu, inline toggle, editor, theme, popup and PTY checks passed. Physical UART/native compositor integration was not tested.

## Header surfaces and compact controls

Dialog title bars now use 15 px semibold text on a separate Light/Dark surface with internal padding and a bottom divider. Updated live-theme pixel checks to the new header colors. TX batch labels use sentence case, Stop all has no icon, and a thin divider separates the batch row from the list. Scrollbars use 6 px thickness with orientation-specific minimum handle lengths. TX title-to-summary spacing is 6 px.

**Verification:** full build and CTest **2/2 passed in 11.06 s**. Reviewed offscreen Settings in Light/Dark and About in Dark for title/body separation. Live theme switching, dialog closing, popup, resize, focus/hover/pressed and PTY/batch checks passed. Native desktop integration was not retested.

## Integrated dialog header

Replaced the inset colored title block with a full-width top header and a separate inset content layout. Softened the header surfaces and made the close button transparent at rest, preserving hover/pressed and keyboard focus feedback. Shared layout applies to Settings, About and the message editor.

**Verification:** full build and CTest **2/2 passed in 11.11 s**, including live header-color checks and dialog closing. Reviewed minimum-size Light/Dark Settings and Dark About renders. Existing interaction, popup, resize and PTY checks passed. Native desktop integration was not retested.

## Continuous dialog surface

Unified header and body backgrounds and removed the header divider. A single 18 px semibold title replaces duplicate content headings in Settings, About and the message editor. Header/body text share the same left inset. A 1 px outer layout inset prevents header painting from covering the window border, preserving its continuous outline.

**Verification:** full build and CTest **2/2 passed in 11.16 s**. Reviewed offscreen Settings/About/editor layouts in the preceding Light/Dark pass and final Light Settings/Dark About border renders. Live theme, close/Escape and existing interaction/PTY checks passed. Native desktop integration was not retested.

## TX card state distinction

Running cards now retain a soft teal surface and a narrow inset accent, including while selected or hovered. Stopped cards remain neutral; error cards use a soft red surface/accent. Indigo outlines identify selection independently of transmission state. Existing status text, toggle icons and accessible descriptions remain available.

**Verification:** full build and CTest **2/2 passed in 10.94 s**. Reviewed minimum-size populated Light/Dark offscreen renders with Running and Stopped cards together. Existing inline start/stop, context-menu, batch, live-theme, focus/hover/pressed, resize and PTY checks passed. Native desktop and physical UART were not tested.

## Softer stopped-card surface

Removed vertical side accents from TX cards. Stopped cards now use soft gray surfaces in both themes; Running retains its teal surface and selection remains an independent indigo outline.

**Verification:** full build and targeted `modernDashboard` test passed (3 Qt test entries, 0 failures). Reviewed Light/Dark minimum-size offscreen renders with mixed Running/Stopped cards. Native desktop was not tested.

## Message editor layout and feedback

Refined Add/Edit message with a draggable catalog/editor divider, `[ID] NAME` catalog entries, result count/empty-search feedback and a selected-message heading. Sender/protocol/rate controls share a 34 px height; rate includes Hz. Payload value cells have a dedicated theme-aware surface, visible Read-only constants and cell-level invalid markers/outline. Field-specific array/text guidance replaces the permanent footer instructions. A divider separates the fixed action row; the primary label follows Add & start/Add message/Save changes and connection changes.

**Verification:** full build and CTest **2/2 passed in 11.65 s**. New editor UI coverage checks empty/filtered catalog results without discarding edits, read-only constants, char/numeric array guidance, splitter adjustment, live theme changes and minimum-size control/footer geometry. Existing PTY desktop flow verifies connected Add & start, invalid uint8 cell marking/correction, automatic transmission and Edit Save changes. Reviewed Light/Dark default and minimum-size offscreen renders plus validation feedback. Existing popup, focus/hover/pressed, batch and serial checks passed. Native desktop/compositor and physical UART were not tested.

## Parameter table clarity

The message parameter table now has a distinct 36 px header, 40 px rows, a soft outer border and consistent row separators. Parameter names and type annotations have separate typography levels. Editable values are painted as inset rounded inputs with hover/selection/error outlines; read-only constants remain plain labeled values. Native text editors align with those input rectangles and expose their parameter name for accessibility.

**Verification:** full build and CTest **2/2 passed in 11.61 s**. Editor tests now enter and commit a value through the real QLineEdit, waiting for Qt's queued commit before checking model data. Reviewed Light/Dark minimum-size renders and the highlighted invalid-value render; existing array guidance, filtering/edit preservation, read-only, resize, live-theme, PTY and validation checks passed. Rendering used offscreen Qt; native desktop was not tested.

## Message editor usability refinement

Aligned catalog IDs/names in separate painted columns with soft Light/Dark selection. Search adds a theme-aware vector magnifier and native clear action, ranking exact matches before prefixes/substrings. The default catalog width is approximately 29%; sender IDs and Protocol/Rate are separated by spacing. Value outlines are quieter at rest and type labels use 12 px text. Cancel precedes the primary action in a right-aligned footer, with matching keyboard traversal.

Payload validation now runs on committed edits, showing a reason below each invalid input without discarding the raw text. Submission still checks the complete payload and reveals filtered invalid fields. Messages with more than 12 fields expose a parameter-name filter. Arrays offer a themed multiline editor with validated Apply and non-mutating Cancel; numeric input remains comma-separated and is compacted for table display. Per-message payload drafts, including active editor contents and invalid text, survive catalog switches within the open dialog.

**Verification:** full build and CTest **2/2 passed in 12.66 s**. Expanded editor tests exercise exact-ID ranking, native search clear, valid/invalid draft restoration, committing an active editor before switching messages, char/numeric array Cancel and invalid/valid Apply, parameter filtering and revealing a hidden invalid field on submit, plus footer order/containment at 880×600. Initial checks exposed a premature native clear-button click in the test and RTL footer alignment; the test now targets the actual clear control after its appearance transition, and the footer uses absolute right alignment. Reviewed regenerated Light/Dark offscreen editor, inline error, many-field and array error renders. Existing interaction, popup, live theme, resize, read-only and PTY tests passed. CodeGraph is up to date; git diff --check passed. Native desktop/compositor integration and physical UART/DMA were not tested.

## RX readability and presentation preferences

Reduced nested RX header emphasis and made System/Component labels secondary to message names. Decoded fields share the editor's name/type/value typography and soft selection. The inspector separates `[ID] NAME` from its sender line. RX search adds a theme-aware magnifier and reports matching/total displayed streams when filtered. Pause adds an explicit Display paused label; parsing and counters continue. TX headings wrap at underscore boundaries into at most two lines, with wrapped rows growing from 114 to 130 px. Normal window close saves geometry, splitters and RX/field column widths through application-scoped QSettings; startup restores presentation only, retaining the hidden Unit column and leaving UART/TX state untouched.

**Verification:** full build and CTest **2/2 passed in 12.68 s**. Added isolated temporary settings coverage for a close/reopen geometry, splitter and column-width round trip, invalid geometry/splitter fallback, and absence of restored connection/TX entries. Dashboard checks cover separate message/sender headings, matched/total search summaries, taller long-name TX cards and the explicit Pause label; the PTY flow verifies frozen display counts followed by resumed updates. Existing interaction, popup, focus/hover/pressed, disabled control, live-theme, editor and serial checks passed. Reviewed regenerated minimum-size Light/Dark populated and paused renders. Fixed reproduced Resume display clipping and stale value colors on theme changes during render review. CodeGraph is up to date; git diff --check passed. Native desktop geometry/compositor integration and physical UART/DMA were not tested. Multiple instances use the last normally saved presentation layout.

## Subdued search placeholders

Search controls now set only their QPalette PlaceholderText role to a softer Light/Dark hint color, including the magnifier, while retaining normal input text colors and inherited palette roles. Palette/style changes refresh this role for RX search, catalog search and parameter filtering.

**Verification:** build and targeted messageEditorUi/modernDashboard checks passed (4 Qt test entries, 0 failures). Reviewed regenerated Light/Dark offscreen search renders; existing typed-input, clear, filtering and live-theme interactions passed. Native desktop rendering was not retested.

## Whole-number dashboard frequencies

TX Target/Actual and RX Hz now display rounded whole numbers, such as 10 Hz instead of 10.0 Hz. Scheduling and fractional Add/Edit rate inputs retain their original precision.

**Verification:** build and targeted modernDashboard test passed (3 Qt entries, 0 failures), including the updated accessible TX frequency check. Reviewed regenerated Light/Dark minimum-size offscreen dashboard renders. CodeGraph is current; git diff --check passed. Native desktop and physical UART were not retested.

## Whole-number TX rate configuration

Add/Edit Rate now configures TX at whole-number rates from 1 to 1000 Hz in 1 Hz steps, removing the fractional input and decimal suffix. This supersedes fractional Add/Edit input described in the preceding verification entry; core timing calculations retain their existing precision.

**Verification:** full build and CTest 2/2 passed in 12.42 s. Editor checks exercise lower/upper bounds, fractional-value rounding, integer text and 1 Hz stepping. Reviewed Light/Dark offscreen editor renders; existing PTY scheduling, validation and interaction checks passed. CodeGraph is current; git diff --check passed. Physical UART and native desktop were not tested.

## TX target-rate badge

TX cards replace the Target label/value pair with a compact rounded indigo badge, such as `1 HZ`. Actual Hz, frame count and scheduling behavior are unchanged; accessible text still identifies the target frequency.

**Verification:** build and targeted modernDashboard/interactionStates checks passed (4 Qt entries, 0 failures). Reviewed regenerated minimum-size Light/Dark offscreen dashboard renders, including running/stopped cards and wrapped headings. Existing selection, focus, hover, pressed, disabled, popup and live-theme checks passed. CodeGraph is current; git diff --check passed. Native desktop and physical UART were not retested.

## Softer, smaller TX frequency badge

The target-rate badge now uses `Hz` casing, a subtler indigo background in both themes and a 22 px height (previously 26 px), with reduced horizontal padding.

**Verification:** build and targeted modernDashboard/interactionStates checks passed (4 Qt entries, 0 failures). Reviewed regenerated minimum-size Light/Dark offscreen dashboards. CodeGraph is current; git diff --check passed. Native desktop was not retested.

## Slate-blue frequency badge

The frequency badge now uses a quiet slate-blue surface and matching text instead of indigo, retaining its 22 px height and `Hz` casing.

**Verification:** build and targeted modernDashboard check passed (3 Qt entries, 0 failures). Reviewed minimum-size Light/Dark offscreen dashboards with running/stopped cards and live theme changes. CodeGraph is current; git diff --check passed. Native desktop was not retested.

## Frequency badge contrast across card states

Strengthened the slate-blue badge surface and text, and added a thin outline to separate it from both Running and Stopped card backgrounds. Text/background contrast calculated from the opaque sRGB colors is 7.76:1 in Light and 8.02:1 in Dark.

**Verification:** build and targeted modernDashboard/interactionStates checks passed (4 Qt entries, 0 failures). Reviewed regenerated minimum-size Light/Dark offscreen dashboards with Running/Stopped cards; interaction and live-theme checks passed. CodeGraph is current; git diff --check passed. Native desktop was not retested.

## Wider TX cards

Reduced horizontal card insets from 4/8 px to 1/2 px, adding 9 px of usable width. Updated title-height calculation and status-toggle hover/click geometry to match. The dashboard check now allows the longer identity to fit on one line at the minimum window size.

**Verification:** build and targeted modernDashboard/interactionStates checks passed (4 Qt entries, 0 failures). Reviewed regenerated Light/Dark minimum-size offscreen dashboards; selection, status actions, resize and live-theme checks passed. CodeGraph is current; git diff --check passed. Native desktop was not retested.

## Frequency badge sizing alignment

Enlarged the frequency badge from 22 to 28 px high, with a minimum width of 56 px and increased horizontal padding. Its vertical center aligns with the adjacent two-line Actual metric.

**Verification:** build and targeted modernDashboard check passed (3 Qt entries, 0 failures). Reviewed regenerated Light/Dark minimum-size offscreen dashboards with Running/Stopped cards. CodeGraph is current; git diff --check passed. Native desktop was not retested.

## Narrower frequency badge

Removed the minimum badge width and reduced horizontal padding to 8 px per side. Height remains 28 px, preserving vertical alignment with Actual.

**Verification:** build and targeted modernDashboard check passed (3 Qt entries, 0 failures). Reviewed regenerated minimum-size Light/Dark offscreen dashboards. CodeGraph is current; git diff --check passed. Native desktop was not retested.

## Message editor sender defaults and rate unit

New GUI messages initialize System ID and Component ID to 1; editing retains the saved IDs. Moved the rate unit from the numeric input suffix to the visible/accessibility label `Rate (Hz)`.

**Verification:** build and full CTest 2/2 passed in 12.62 s. Editor checks assert sender defaults, rate accessible name and numeric-only input text. Reviewed regenerated minimum-size Light/Dark offscreen editors. Existing PTY and interaction checks passed. CodeGraph is current; git diff --check passed. Native desktop and physical UART were not retested.
