#include "worker.h"
#include <QSignalSpy>
#include <QTest>
#include <cerrno>
#include <cstdarg>
#include <cstring>
#include <fcntl.h>
#include <pty.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace
{
int clearError = 0;
int getError = 0;
int reportedBits = 0;
int clearedMask = 0;
int clears = 0;
int reads = 0;
bool interruptOnce = false;
bool pretendSerial = false;
}

extern "C" int __real_ioctl(int fd, unsigned long request, ...);
extern "C" int __real_ttyname_r(int fd, char *buffer, size_t size);

/** @brief Substitute modem ioctls from the statically linked worker, leaving Qt I/O real. */
extern "C" int __wrap_ioctl(int fd, unsigned long request, ...)
{
    va_list args;
    va_start(args, request);
    auto *bits = va_arg(args, int *);
    va_end(args);
    if (request == TIOCMBIC)
    {
        ++clears;
        clearedMask = *bits;
        if (interruptOnce)
        {
            interruptOnce = false;
            errno = EINTR;
            return -1;
        }
        errno = clearError;
        return clearError ? -1 : 0;
    }
    if (request == TIOCMGET)
    {
        ++reads;
        *bits = reportedBits;
        errno = getError;
        return getError ? -1 : 0;
    }
    return __real_ioctl(fd, request, bits);
}

/** @brief Distinguish real PTY fallback from unsupported physical serial devices. */
extern "C" int __wrap_ttyname_r(int fd, char *buffer, size_t size)
{
    if (pretendSerial)
    {
        const char path[] = "/dev/ttyUSBmock";
        if (size < sizeof(path))
        {
            return ERANGE;
        }
        std::memcpy(buffer, path, sizeof(path));
        return 0;
    }
    return __real_ttyname_r(fd, buffer, size);
}

class ControlLinesTests : public QObject
{
    Q_OBJECT
private slots:
    /** @brief Reset syscall simulation between independent lifecycle cases. */
    void init()
    {
        clearError = getError = reportedBits = clearedMask = clears = reads = 0;
        interruptOnce = pretendSerial = false;
    }

    /** @brief Cover flow-control ownership, syscall errors and driver readback failures. */
    void connectPolicy_data()
    {
        QTest::addColumn<int>("flow");
        QTest::addColumn<int>("clearFailure");
        QTest::addColumn<int>("readFailure");
        QTest::addColumn<int>("bits");
        QTest::addColumn<bool>("serial");
        QTest::addColumn<bool>("success");
        QTest::newRow("both-cleared") << 0 << 0 << 0 << TIOCM_CTS << false << true;
        QTest::newRow("software") << 2 << 0 << 0 << 0 << false << true;
        QTest::newRow("hardware-owns-rts") << 1 << 0 << 0 << TIOCM_RTS << false << true;
        QTest::newRow("clear-denied") << 0 << EACCES << 0 << 0 << false << false;
        QTest::newRow("read-failed") << 0 << 0 << EIO << 0 << false << false;
        QTest::newRow("dtr-still-asserted") << 0 << 0 << 0 << TIOCM_DTR << false << false;
        QTest::newRow("rts-still-asserted") << 0 << 0 << 0 << TIOCM_RTS << false << false;
        QTest::newRow("pty-no-modem") << 0 << ENOTTY << 0 << 0 << false << true;
        QTest::newRow("serial-no-modem") << 0 << ENOTTY << 0 << 0 << true << false;
    }

    /** @brief Check Connected is emitted only after the requested control-line policy succeeds. */
    void connectPolicy()
    {
        QFETCH(int, flow);
        QFETCH(int, clearFailure);
        QFETCH(int, readFailure);
        QFETCH(int, bits);
        QFETCH(bool, serial);
        QFETCH(bool, success);
        clearError = clearFailure;
        getError = readFailure;
        reportedBits = bits;
        pretendSerial = serial;
        int master = -1;
        int slave = -1;
        char path[256]{};
        QVERIFY(::openpty(&master, &slave, path, nullptr, nullptr) == 0);
        SerialWorker worker;
        worker.initialize();
        QSignalSpy states(&worker, &SerialWorker::stateChanged);
        QSignalSpy errors(&worker, &SerialWorker::failure);
        PortConfig config;
        config.port = path;
        config.flow = static_cast<QSerialPort::FlowControl>(flow);
        worker.openPort(config);
        QCOMPARE(states.last()[1].toBool(), success);
        QCOMPARE(errors.isEmpty(), success);
        QCOMPARE(clearedMask, flow == 1 ? TIOCM_DTR : TIOCM_DTR | TIOCM_RTS);
        QCOMPARE(clears, 1);
        QCOMPARE(reads, clearFailure ? 0 : 1);
        if (!success)
        {
            QVERIFY(!worker.findChild<QSerialPort *>()->isOpen());
            for (const auto &state : states)
            {
                QVERIFY(!state[1].toBool());
            }
        }
        worker.closePort();
        ::close(master);
        ::close(slave);
    }

    /** @brief Retry EINTR and repeat deassertion/readback after a silent automatic reconnect. */
    void reconnectPolicy()
    {
        int master = -1;
        int slave = -1;
        char path[256]{};
        QVERIFY(::openpty(&master, &slave, path, nullptr, nullptr) == 0);
        SerialWorker worker;
        worker.initialize();
        QSignalSpy states(&worker, &SerialWorker::stateChanged);
        PortConfig config;
        config.port = path;
        config.autoReconnect = true;
        interruptOnce = true;
        worker.openPort(config);
        QVERIFY(states.last()[1].toBool());
        QCOMPARE(clears, 2);
        QCOMPARE(reads, 1);
        auto *port = worker.findChild<QSerialPort *>();
        QVERIFY(port);
        emit port->errorOccurred(QSerialPort::ResourceError);
        QCOMPARE(states.last()[0].toString(), QString("Reconnecting"));
        // A failed readback must leave the port closed and retry timer running.
        getError = EIO;
        QTRY_VERIFY_WITH_TIMEOUT(reads >= 2, 2000);
        QVERIFY(!port->isOpen());
        QVERIFY(!states.last()[1].toBool());
        getError = 0;
        QTRY_VERIFY_WITH_TIMEOUT(states.last()[1].toBool(), 2000);
        QVERIFY(clears >= 4);
        QVERIFY(reads >= 3);
        QVERIFY(::fcntl(master, F_SETFL, O_NONBLOCK) >= 0);
        char byte;
        QCOMPARE(::read(master, &byte, 1), ssize_t(-1));
        QVERIFY(errno == EAGAIN || errno == EWOULDBLOCK);
        worker.closePort();
        ::close(master);
        ::close(slave);
    }
};

QTEST_GUILESS_MAIN(ControlLinesTests)
#include "control_lines_tests.moc"
