# Troubleshooting Guide

**Audience:** Developers, test engineers, and firmware integrators diagnosing build, connection, protocol, or runtime issues in MAVLinkLab.

All diagnostic procedures and solutions documented here are supported by repository source code, build scripts, and automated test suites.

---

## 1. CMake Configuration or Build Failure

### Problem
CMake fails during configuration or compilation.

### Symptoms
- CMake error: `Initialize MAVLink: git submodule update --init --recursive`.
- CMake error: `Could NOT find Qt6 (missing: Widgets SerialPort)` or `missing: Test`.
- Compiler errors referencing missing MAVLink headers.

### Possible Causes
- The `third_party/mavlink` Git submodule has not been cloned or initialized.
- Qt 6 development libraries (`qt6-base-dev`, `libqt6serialport6-dev`) are not installed in system paths.
- Building tests with `BUILD_TESTING=ON` when Qt6 Test is not available.

### Diagnosis
1. Check if `third_party/mavlink/ardupilotmega/mavlink.h` exists.
2. Check your Qt 6 installation version and prefix:
   ```bash
   qmake6 -query QT_INSTALL_PREFIX || qmake -query QT_INSTALL_PREFIX
   ```
3. Check if local dependencies exist in `.deps/usr`.

### Solution
1. **Initialize the MAVLink submodule:**
   ```bash
   git submodule update --init --recursive
   ```
2. **If Qt 6 is installed via system packages:**
   ```bash
   sudo apt-get install -y qt6-base-dev libqt6serialport6-dev
   cmake -S . -B build
   cmake --build build -j
   ```
3. **If Qt 6 is in a custom directory:**
   Specify `CMAKE_PREFIX_PATH`:
   ```bash
   cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/gcc_64
   cmake --build build -j
   ```
4. **If building without Qt (Core-only):**
   ```bash
   cmake -S . -B build-core -DBUILD_GUI=OFF
   cmake --build build-core -j
   ctest --test-dir build-core --output-on-failure
   ```
5. **If using `./cli.sh` on Ubuntu without root access:**
   Run `./cli.sh build all`, which automatically downloads required packages into `.deps/usr` using `apt-get download` and `dpkg-deb -x`.

### Notes
Do not mix Qt major/minor versions between build time and runtime.

---

## 2. Launcher or Execution Failure

### Problem
`./cli.sh run` fails to launch or runs an outdated binary.

### Symptoms
- Terminal output: `Application not found`, `Configured Qt installation is missing`, or `Qt runtime not found`.
- Application starts but changes made to source files are not visible.

### Possible Causes
- The application binary was not built after modifying code.
- Custom Qt installation path or temporary directory was removed.
- Missing `LD_LIBRARY_PATH` or `QT_PLUGIN_PATH` when running the binary directly outside `cli.sh`.

### Diagnosis
1. Check executable timestamp against source code:
   ```bash
   ls -l build/mavlink-lab src/window.cpp
   ```
2. Verify Qt dynamic linker dependencies:
   ```bash
   ldd build/mavlink-lab | grep -i qt
   ```

### Solution
1. Execute a full rebuild:
   ```bash
   cmake --build build -j
   ```
2. Use `./cli.sh run` to automatically set up the runtime environment (`QT_PLUGIN_PATH` and `LD_LIBRARY_PATH`) derived from `CMakeCache.txt` or `.deps/usr`.

---

## 3. Serial Port Connection Failure

### Problem
Clicking **Connect** results in an immediate `Error` state or a popup alert dialog.

### Symptoms
- Modal alert dialog: `Connection Error: Failed to open serial port: ...`.
- Toolbar status badge remains `Disconnected`.

### Possible Causes
- Current Linux user lacks read/write permissions for `/dev/ttyUSB*` or `/dev/ttyACM*`.
- The port is locked by another running process (e.g. `minicom`, `screen`, `ModemManager`, or another instance).
- An invalid or non-numeric baud rate was entered.
- Linux modem control line deassertion failed on physical hardware.

### Diagnosis
1. Check device permissions:
   ```bash
   ls -l /dev/ttyUSB0 /dev/ttyACM0
   ```
2. Check for active processes holding the device:
   ```bash
   lsof /dev/ttyUSB0
   fuser /dev/ttyUSB0
   ```
3. Check `dmesg` output for USB disconnect or driver errors:
   ```bash
   dmesg | tail -n 20
   ```

### Solution
1. **Grant serial permissions:**
   Add your Linux user to the `dialout` or `uucp` group:
   ```bash
   sudo usermod -a -G dialout $USER
   ```
   Log out and log back in for changes to take effect.
2. **Stop conflicting services:**
   Stop `ModemManager` if it probes the serial port on connection:
   ```bash
   sudo systemctl stop ModemManager
   ```
3. **Check Baud Rate:**
   Ensure the baud rate input contains a positive integer (e.g. `115200`, `921600`). Non-numeric inputs are rejected immediately.
4. **Toggle DTR/RTS deassertion:**
   If a custom USB bridge rejects modem line `ioctl` calls, open **Tool → Settings… → Serial Management** and uncheck **Deassert DTR/RTS on connect**.

---

## 4. Connected Successfully but TX Does Not Transmit

### Problem
The serial port connects, but no MAVLink messages are received by the peer.

### Symptoms
- Status badge shows `Connected`.
- TX Card shows `Stopped` or `Frames: 0`.
- No frames received on target flight controller or peer tool.

### Possible Causes
- **Designed Invariant:** Connecting or reconnecting **never** transmits frames automatically, including `HEARTBEAT`.
- The TX entry is in `Stopped` state (`active = false`).
- Newly created or restored messages always start in `Stopped` state for safety.

### Diagnosis
1. Check the inline state badge on the TX card (`Running` vs `Stopped`).
2. Verify target System ID and Component ID match peer expectations.
3. Check TX Card `Actual:` metric to see if frames are actively being dispatched.

### Solution
1. Click the play button (`▶`) on the target TX card, or click **Run all** in the TX Dashboard header.
2. Use context menu **Send once** to send an individual frame immediately.
3. To test with virtual PTY serial ports, use `socat` to create a loopback pair:
   ```bash
   socat -d -d pty,raw,echo=0 pty,raw,echo=0
   ```

---

## 5. TX Stops, Pending Bytes Accumulate, or Queue Full

### Problem
A running TX message stops unexpectedly, or the status bar shows high `Pending` bytes.

### Symptoms
- TX Card status changes to `Error` with message: `"TX queue full or serialization failed; periodic TX stopped"`.
- Status bar `Pending:` counter grows continuously.

### Possible Causes
- Downstream serial device or receiver is stalled or not reading from the serial port.
- Hardware flow control (RTS/CTS) is enabled while CTS is deasserted by the peer.
- Aggregate offered load across all active TX entries exceeds the physical baud rate capacity.
- The internal `WriteQueue` limit (16,384 bytes) has been exceeded.

### Diagnosis
1. Check `Pending:` in the debug status bar.
2. Check UART Flow Control in UART Settings popup.
3. Compare aggregate planned load (`~X B/s`) against baud rate capacity.

### Solution
1. If using hardware flow control without CTS wiring, switch Flow Control to **None**.
2. Reduce the transmission frequency (`Rate (Hz)`) or stop non-essential messages.
3. Resolve receiver stalls on the peer device.
4. Once the link clears, click the play button (`▶`) on the stopped card to resume transmission.

---

## 6. RX List Empty, CRC Errors Incrementing, or Corrupted Data

### Problem
Incoming serial data produces no decoded messages, or CRC errors rapidly increment.

### Symptoms
- RX Dashboard shows no streams or missing messages.
- Status bar `CRC:` error count increments with every received frame.
- Decoded fields display corrupted or default values.

### Possible Causes
- Baud rate, parity, or stop bits mismatch between MAVLinkLab and the transmitting device.
- Software flow control (XON/XOFF) is corrupting binary bytes `0x11` or `0x13`.
- Dialect mismatch: The remote node is transmitting messages not defined in `ardupilotmega` / `common`.
- **Pause Display** is enabled in the RX Dashboard.

### Diagnosis
1. Check UART framing parameters (ensure **8 Data bits**, **None parity**, **1 Stop bit** for standard MAVLink).
2. Check if **Pause Display** is checked. When paused, incoming frames continue parsing, but UI updates are suspended.
3. Inspect `CRC:` errors in the debug status bar.

### Solution
1. Synchronize baud rate and framing settings with the transmitting node.
2. Ensure Flow Control is set to **None** or **Hardware RTS/CTS** (never use Software XON/XOFF for binary protocols).
3. Click **Resume display** if paused.
4. If using a custom dialect, recompile with the required dialect XML definitions.

---

## 7. Payload Editor Rejects Input

### Problem
Entering a parameter value in the message editor shows an inline error or cannot be saved.

### Symptoms
- Red inline validation text below the field.
- Save changes button is disabled or highlights error fields.
- Error: `MAVLink 1 supports message IDs 0–255 only`.

### Possible Causes
- Integer value exceeds minimum or maximum range of the MAVLink type (e.g. entering `300` for a `uint8_t`).
- Floating-point field contains `NaN`, `Infinity`, or non-numeric characters.
- Numeric array input has incorrect number of elements or trailing commas.
- Character string exceeds the fixed byte buffer length.
- Message ID exceeds 255 while protocol version is set to MAVLink 1.

### Diagnosis
1. Read the inline validation message below the highlighted field.
2. Check the message ID and selected Protocol version (v1 vs v2).

### Solution
1. Enter values within the valid type boundaries:
   - `uint8_t`: 0 to 255
   - `int8_t`: -128 to 127
   - `uint16_t`: 0 to 65535
   - `int16_t`: -32768 to 32767
   - `float` / `double`: Valid finite decimal numbers (scientific notation like `1.5e-3` is supported).
2. For numeric arrays, supply exactly the required number of comma-separated values (e.g. `0, 0, 0, 0`).
3. For MAVLink 1, switch to MAVLink 2 if the message ID is greater than 255.

---

## 8. Parameter Protocol Loading Incomplete

### Problem
Parameter View shows partial parameters (`PARAMETERS (45 / 420)`).

### Symptoms
- Parameter list stops populating before total count is reached.
- Loading indicator remains active.

### Possible Causes
- Remote target dropped parameter packets due to wireless loss or buffer overflow.
- Target component does not implement standard MAVLink Parameter Protocol (`PARAM_REQUEST_LIST`).

### Diagnosis
1. Check if RX message `PARAM_VALUE` (msgid 22) is still arriving.
2. Check for CRC errors or dropped packets on the serial link.

### Solution
1. Click **Refresh** in the Parameter View header to re-request the full parameter list (`PARAM_REQUEST_LIST`).
2. Double-click any parameter to edit and send `PARAM_SET` directly.

---

## 9. Telemetry Plotter Waveform Not Displaying

### Problem
Right-clicking a field and choosing "Plot this field" does not draw a waveform.

### Symptoms
- Strip-chart canvas is blank or flatlined at zero.
- Plot panel is collapsed.

### Possible Causes
- The selected field is non-numeric (e.g. character array or string).
- Received field values are non-finite (`NaN` or `Inf`).
- The incoming message stream has stopped arriving.
- Telemetry plot is paused (**Pause** button active).

### Diagnosis
1. Verify the field type in the Decoded Fields table (`float`, `uint16_t`, etc.).
2. Check the inline freshness status dot of the parent message in the RX tree (ensure it is Green/Active).
3. Check if the **Pause** button on the plotter toolbar is toggled.

### Solution
1. Only select numeric scalar or array fields for plotting.
2. Ensure the remote device is actively transmitting the target message.
3. Click **Resume** if the plot canvas is paused.
