#include "worker.h"
#include "profile.h"
#include <QFile>
#include <QSerialPortInfo>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>
#include <sys/ioctl.h>
#include <unistd.h>

/** @brief Retry an interrupted modem-control ioctl without changing its mask. */
static int modemIoctl(int fd, unsigned long request, int *bits)
{
    int result;
    do
    {
        result = ::ioctl(fd, request, bits);
    }
    while (result < 0 && errno == EINTR);
    return result;
}

QString SerialWorker::prepareControlLines()
{
    if (!config.deassertControlLines)
    {
        return {};
    }
    const int fd = port->handle();
    int mask = TIOCM_DTR;
    if (config.flow != QSerialPort::HardwareControl)
    {
        mask |= TIOCM_RTS;
    }

    // Clear both lines in one operation; separate transitions may reset the peer.
    if (modemIoctl(fd, TIOCMBIC, &mask) < 0)
    {
        const int error = errno;
        char path[256]{};
        if (error == ENOTTY && ::ttyname_r(fd, path, sizeof(path)) == 0 &&
            QByteArray(path).startsWith("/dev/pts/"))
        {
            return {};
        }
        return QString("Cannot deassert serial control lines: %1")
            .arg(QString::fromLocal8Bit(std::strerror(error)));
    }
    int bits = 0;
    if (modemIoctl(fd, TIOCMGET, &bits) < 0)
    {
        const int error = errno;
        return QString("Cannot verify serial control lines: %1")
            .arg(QString::fromLocal8Bit(std::strerror(error)));
    }
    if ((bits & mask) != 0)
    {
        return "Serial control lines remain asserted after deassertion";
    }
    return {};
}

SerialWorker::SerialWorker(QObject *parent) : QObject(parent)
{
}

void SerialWorker::initialize()
{
    clock.start();
    port = new QSerialPort(this);
    port->setReadBufferSize(65536);
    timer = new QTimer(this);
    timer->setTimerType(Qt::PreciseTimer);
    timer->setInterval(1);
    connect(timer, &QTimer::timeout, this, &SerialWorker::tick);

    reconnectTimer = new QTimer(this);
    reconnectTimer->setInterval(1000);
    connect(reconnectTimer, &QTimer::timeout, this, &SerialWorker::attemptReconnect);

    connect(port, &QSerialPort::readyRead, this, &SerialWorker::readAvailable);
    connect(port, &QSerialPort::bytesWritten, this, [this](qint64 bytes)
    {
        txBytes += uint64_t(bytes);
        pump();
    });
    connect(port, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError error)
    {
        if (reconnecting)
        {
            return;
        }
        if (!closing && error != QSerialPort::NoError && error != QSerialPort::NotOpenError)
        {
            const QString reason = port->errorString();
            handleUnexpectedDisconnect(reason);
        }
    });
    timer->start();
}

/** @brief Open the configured port and verify control lines before reporting Connected. */
void SerialWorker::openPort(PortConfig requested)
{
    closePort();
    emit stateChanged("Connecting", false);
    config = requested;
    port->setPortName(config.port);
    const bool configured = port->setBaudRate(config.baud) && port->setDataBits(config.data) &&
                            port->setParity(config.parity) && port->setStopBits(config.stop) &&
                            port->setFlowControl(config.flow);
    if (!configured || !port->open(QIODevice::ReadWrite))
    {
        emit stateChanged("Error", false);
        fail(port->errorString());
        return;
    }
    const QString controlError = prepareControlLines();
    if (!controlError.isEmpty())
    {
        closePort();
        emit stateChanged("Error", false);
        fail(controlError);
        return;
    }
    parser = {};
    receiver = {};
    rxBytes = txBytes = 0;
    lastReport = clock.nsecsElapsed() / 1000;
    emit stateChanged("Connected", true);
}

void SerialWorker::closePort()
{
    if (reconnectTimer)
    {
        reconnectTimer->stop();
    }
    reconnecting = false;
    closing = true;
    stopAll();
    if (port && port->isOpen())
    {
        port->close();
    }
    parser = {};
    txStatuses.clear();
    closing = false;
    emit stateChanged("Disconnected", false);
}

void SerialWorker::setDeassertControlLines(bool enabled)
{
    config.deassertControlLines = enabled;
}

void SerialWorker::setAutoReconnect(bool enabled)
{
    config.autoReconnect = enabled;
    if (!enabled && reconnecting)
    {
        if (reconnectTimer)
        {
            reconnectTimer->stop();
        }
        reconnecting = false;
        emit stateChanged("Disconnected", false);
    }
}

void SerialWorker::saveEntry(Mav::TxEntry entry)
{
    if (!Mav::validTx(entry))
    {
        fail("Invalid TX entry or unsupported MAVLink version");
        return;
    }
    auto found = std::find_if(entries.begin(), entries.end(), [&](const auto &item)
    {
        return item.key == entry.key;
    });
    entry.active = false;
    entry.error.clear();
    if (found != entries.end())
    {
        discardOutput();
        entry.count = found->count;
        *found = std::move(entry);
    }
    else if (entries.size() < 128)
    {
        entries.push_back(std::move(entry));
    }
    else
    {
        fail("TX entry limit reached (128)");
    }
}

void SerialWorker::removeEntry(quint64 key)
{
    discardOutput();
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const auto &entry)
    {
        return entry.key == key;
    }), entries.end());
}

void SerialWorker::setActive(quint64 key, bool active)
{
    for (auto &entry : entries)
    {
        if (entry.key != key)
        {
            continue;
        }
        if (!active)
        {
            entry.active = false;
            discardOutput();
            return;
        }
        const double bits = 1 + int(config.data) + (config.parity == QSerialPort::NoParity ? 0 : 1) +
                            (config.stop == QSerialPort::TwoStop ? 2.0 :
                             config.stop == QSerialPort::OneAndHalfStop ? 1.5 : 1.0);
        if (!port->isOpen() || !Mav::fitsLink(entries, entry, config.baud, bits))
        {
            entry.error = "Port closed or offered load exceeds link capacity";
            fail(QString::fromStdString(entry.error));
            return;
        }
        entry.error.clear();
        entry.active = true;
        entry.deadline = clock.nsecsElapsed() / 1000;
    }
}

void SerialWorker::sendOnce(quint64 key)
{
    for (auto &entry : entries)
    {
        if (entry.key == key)
        {
            if (!port->isOpen())
            {
                fail("Connect a serial port first");
                return;
            }
            enqueue(entry);
            pump();
        }
    }
}

void SerialWorker::stopAll()
{
    for (auto &entry : entries)
    {
        entry.active = false;
    }
    discardOutput();
}

void SerialWorker::runAll()
{
    if (entries.empty())
    {
        return;
    }
    auto requested = entries;
    for (auto &entry : requested)
    {
        entry.active = true;
    }
    const double bits = 1 + int(config.data) + (config.parity == QSerialPort::NoParity ? 0 : 1) +
                        (config.stop == QSerialPort::TwoStop ? 2.0 :
                         config.stop == QSerialPort::OneAndHalfStop ? 1.5 : 1.0);
    for (const auto &entry : requested)
    {
        if (!port->isOpen() || !Mav::fitsLink(requested, entry, config.baud, bits))
        {
            fail("Cannot run all: port closed or total offered load exceeds link capacity");
            return;
        }
    }
    for (auto &entry : entries)
    {
        setActive(entry.key, true);
    }
}

void SerialWorker::deleteAll()
{
    stopAll();
    entries.clear();
}

void SerialWorker::importProfile(QByteArray data, bool append, quint64 minimumKey)
{
    std::vector<Mav::TxEntry> imported;
    QString error;
    if (!Profile::decode(data, imported, error))
    {
        emit profileImportFailed(error);
        return;
    }
    const size_t retained = append ? entries.size() : 0;
    if (imported.size() > 128 || retained > 128 - imported.size())
    {
        emit profileImportFailed("Import would exceed the TX entry limit (128)");
        return;
    }
    quint64 nextKey = std::max<quint64>(minimumKey, 1);
    for (const auto &entry : entries)
    {
        if (entry.key == std::numeric_limits<quint64>::max())
        {
            emit profileImportFailed("Cannot allocate fresh TX entry keys");
            return;
        }
        nextKey = std::max(nextKey, quint64(entry.key + 1));
    }
    // Reserve a representable next key for subsequent GUI-created entries as well.
    if (imported.size() > std::numeric_limits<quint64>::max() - nextKey)
    {
        emit profileImportFailed("Cannot allocate fresh TX entry keys");
        return;
    }
    for (auto &entry : imported)
    {
        entry.key = nextKey++;
        entry.active = false;
        entry.count = 0;
        entry.deadline = 0;
        entry.measured = {};
        entry.error.clear();
    }
    // Build the complete replacement before cancelling any existing output.
    auto updated = append ? entries : std::vector<Mav::TxEntry>{};
    updated.insert(updated.end(), imported.begin(), imported.end());
    if (!append)
    {
        discardOutput();
    }
    entries.swap(updated);
    emit profileImported(int(imported.size()), append, nextKey);
    requestSnapshot();
}

void SerialWorker::exportProfile()
{
    QString error;
    const auto data = Profile::encode(entries, error);
    if (!error.isEmpty())
    {
        emit profileExportFailed(error);
        return;
    }
    emit profileExported(data);
}

void SerialWorker::discardOutput()
{
    queue.clear();
    owners.clear();
    if (port && port->isOpen())
    {
        // Already transmitted bytes cannot be recalled.
        if (!port->clear(QSerialPort::Output))
        {
            fail(port->errorString());
        }
    }
}

void SerialWorker::clearStatistics()
{
    receiver = {};
    parser = {};
    rxBytes = txBytes = 0;
    lastReport = clock.nsecsElapsed() / 1000;
}

void SerialWorker::readAvailable()
{
    if (!port->isOpen())
    {
        return;
    }
    const QByteArray bytes = port->read(8192);
    rxBytes += uint64_t(bytes.size());
    const auto *data = reinterpret_cast<const uint8_t *>(bytes.constData());
    const auto messages = parser.feed(Mav::Bytes(data, data + bytes.size()));
    const int64_t now = clock.nsecsElapsed() / 1000;
    for (const auto &message : messages)
    {
        receiver.accept(message, now);
    }
    if (port->bytesAvailable() > 0)
    {
        QTimer::singleShot(0, this, &SerialWorker::readAvailable);
    }
}

void SerialWorker::tick()
{
    if (!port->isOpen())
    {
        return;
    }
    const int64_t now = clock.nsecsElapsed() / 1000;
    for (auto &entry : entries)
    {
        if (Mav::due(entry, now))
        {
            enqueue(entry);
        }
    }
    pump();
}

void SerialWorker::enqueue(Mav::TxEntry &entry)
{
    auto &status = txStatuses[{entry.sysid, entry.compid}];
    const auto frame = Mav::encode(entry.id, entry.sysid, entry.compid, entry.version1, entry.payload, status);
    if (!queue.push(frame))
    {
        entry.active = false;
        entry.error = "TX queue full or serialization failed; periodic TX stopped";
        fail(QString::fromStdString(entry.error));
        return;
    }
    owners.push_back(entry.key);
    entry.error.clear();
}

void SerialWorker::pump()
{
    if (!port->isOpen())
    {
        return;
    }
    // Bound the Qt write buffer in addition to the application queue.
    while (queue.size() && port->bytesToWrite() < 4096)
    {
        const auto frame = queue.front();
        const auto length = std::min<qint64>(qint64(frame.size()), 4096 - port->bytesToWrite());
        const auto written = port->write(reinterpret_cast<const char *>(frame.data()), length);
        if (written < 0)
        {
            handleUnexpectedDisconnect("Serial write failed");
            return;
        }
        if (!written)
        {
            break;
        }
        queue.consume(size_t(written));
        if (size_t(written) == frame.size())
        {
            const auto key = owners.front();
            owners.pop_front();
            for (auto &entry : entries)
            {
                if (entry.key == key)
                {
                    ++entry.count;
                    entry.measured.add(clock.nsecsElapsed() / 1000);
                }
            }
        }
    }
}

void SerialWorker::requestSnapshot()
{
    auto snapshot = std::make_shared<Snapshot>();
    const int64_t now = clock.nsecsElapsed() / 1000;
    snapshot->connected = port->isOpen();
    snapshot->crcErrors = parser.crcErrors;
    snapshot->overflow = receiver.overflow;
    snapshot->pending = queue.size() + size_t(port->bytesToWrite());
    const double elapsed = std::max(0.000001, (now - lastReport) / 1000000.0);
    snapshot->rxBytesPerSecond = rxBytes / elapsed;
    snapshot->txBytesPerSecond = txBytes / elapsed;
    rxBytes = txBytes = 0;
    lastReport = now;
    for (auto &[key, entry] : receiver.entries)
    {
        Q_UNUSED(key);
        snapshot->rx.push_back({entry.message, entry.count, entry.rate.hz(now), (now - entry.last) / 1000000.0});
    }
    for (auto &entry : entries)
    {
        const double measured = entry.measured.hz(now);
        Mav::TxEntry view = entry;
        view.measured = {};
        snapshot->tx.push_back({std::move(view), measured});
    }
    emit snapshotReady(snapshot);
}

void SerialWorker::fail(const QString &message)
{
    emit failure(message);
}

void SerialWorker::handleUnexpectedDisconnect(const QString &reason)
{
    closing = true;
    stopAll();
    if (port && port->isOpen())
    {
        port->close();
    }
    parser = {};
    txStatuses.clear();
    closing = false;

    fail(reason);

    if (config.autoReconnect && !config.port.isEmpty())
    {
        reconnecting = true;
        if (reconnectTimer && !reconnectTimer->isActive())
        {
            reconnectTimer->start();
        }
        emit stateChanged("Reconnecting", false);
    }
    else
    {
        reconnecting = false;
        if (reconnectTimer)
        {
            reconnectTimer->stop();
        }
        emit stateChanged("Error", false);
    }
}

/** @brief Retry opening the port and reapply control-line policy before reconnecting. */
void SerialWorker::attemptReconnect()
{
    if (!reconnecting || (port && port->isOpen()))
    {
        if (reconnectTimer)
        {
            reconnectTimer->stop();
        }
        return;
    }

    bool exists = false;
    const auto available = QSerialPortInfo::availablePorts();
    for (const auto &info : available)
    {
        if (info.portName() == config.port || info.systemLocation() == config.port)
        {
            exists = true;
            break;
        }
    }
    if (!exists && QFile::exists(config.port))
    {
        exists = true;
    }

    if (!exists)
    {
        return;
    }

    port->setPortName(config.port);
    const bool configured = port->setBaudRate(config.baud) && port->setDataBits(config.data) &&
                            port->setParity(config.parity) && port->setStopBits(config.stop) &&
                            port->setFlowControl(config.flow);
    if (!configured || !port->open(QIODevice::ReadWrite))
    {
        return;
    }

    const QString controlError = prepareControlLines();
    if (!controlError.isEmpty())
    {
        port->close();
        fail(controlError);
        return;
    }

    if (reconnectTimer)
    {
        reconnectTimer->stop();
    }
    reconnecting = false;
    parser = {};
    receiver = {};
    rxBytes = txBytes = 0;
    lastReport = clock.nsecsElapsed() / 1000;

    emit stateChanged("Connected", true);
}
