#include "profile.h"
#include "worker.h"
#include <QSignalSpy>
#include <QtTest>
#include <limits>
#include <pty.h>
#include <unistd.h>

/** @brief Create a valid stopped HEARTBEAT definition for import cases. */
static Mav::TxEntry heartbeat(quint64 key)
{
    Mav::TxEntry entry;
    entry.key = key;
    entry.id = MAVLINK_MSG_ID_HEARTBEAT;
    entry.payload.resize(MAVLINK_MSG_ID_HEARTBEAT_LEN);
    entry.payload.back() = 3;
    return entry;
}

/** @brief Observe the worker's current authoritative entries synchronously. */
static SnapshotPtr snapshot(SerialWorker &worker)
{
    SnapshotPtr result;
    const auto connection = QObject::connect(&worker, &SerialWorker::snapshotReady,
                                             &worker, [&](SnapshotPtr value)
    {
        result = value;
    });
    worker.requestSnapshot();
    QObject::disconnect(connection);
    return result;
}

class ProfileWorkerTests : public QObject
{
    Q_OBJECT
private slots:
    /** @brief Replace removes existing definitions and creates stopped entries with new keys. */
    void replaceAndExport()
    {
        SerialWorker worker;
        worker.initialize();
        worker.saveEntry(heartbeat(40));
        QString error;
        const auto data = Profile::encode({heartbeat(1), heartbeat(1)}, error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QSignalSpy imported(&worker, &SerialWorker::profileImported);
        worker.importProfile(data, false, 50);
        QCOMPARE(imported.size(), 1);
        QCOMPARE(imported.first()[2].toULongLong(), qulonglong(52));
        const auto current = snapshot(worker);
        QCOMPARE(current->tx.size(), size_t(2));
        QCOMPARE(current->tx[0].entry.key, uint64_t(50));
        QCOMPARE(current->tx[1].entry.key, uint64_t(51));
        for (const auto &view : current->tx)
        {
            QVERIFY(!view.entry.active);
            QCOMPARE(view.entry.count, uint64_t(0));
        }
        QSignalSpy exported(&worker, &SerialWorker::profileExported);
        worker.exportProfile();
        QCOMPARE(exported.size(), 1);
        std::vector<Mav::TxEntry> decoded;
        QVERIFY(Profile::decode(exported.first()[0].toByteArray(), decoded, error));
        QCOMPARE(decoded.size(), size_t(2));
    }

    /** @brief Invalid input and full-list append leave all current definitions untouched. */
    void invalidImportIsAtomic()
    {
        SerialWorker worker;
        worker.initialize();
        for (quint64 key = 1; key <= 128; ++key)
        {
            worker.saveEntry(heartbeat(key));
        }
        QSignalSpy errors(&worker, &SerialWorker::profileImportFailed);
        QSignalSpy successes(&worker, &SerialWorker::profileImported);
        worker.importProfile(QByteArray("invalid"), false, 129);
        QString error;
        const auto data = Profile::encode({heartbeat(0)}, error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        worker.importProfile(data, true, 129);
        QCOMPARE(errors.size(), 2);
        QVERIFY(successes.isEmpty());
        const auto current = snapshot(worker);
        QCOMPARE(current->tx.size(), size_t(128));
        QCOMPARE(current->tx.front().entry.key, uint64_t(1));
        QCOMPARE(current->tx.back().entry.key, uint64_t(128));
    }

    /** @brief Key exhaustion rejects replace without deleting the previous entries. */
    void keyExhaustion()
    {
        SerialWorker worker;
        worker.initialize();
        worker.saveEntry(heartbeat(1));
        QString error;
        const auto data = Profile::encode({heartbeat(0)}, error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QSignalSpy errors(&worker, &SerialWorker::profileImportFailed);
        worker.importProfile(data, false, std::numeric_limits<quint64>::max());
        QCOMPARE(errors.size(), 1);
        const auto current = snapshot(worker);
        QCOMPARE(current->tx.size(), size_t(1));
        QCOMPARE(current->tx[0].entry.key, uint64_t(1));
    }

    /** @brief Append preserves running entries and counters; replace stops all old entries. */
    void runningAppendAndReplace()
    {
        int master = -1;
        int slave = -1;
        char path[256]{};
        QVERIFY(::openpty(&master, &slave, path, nullptr, nullptr) == 0);
        SerialWorker worker;
        worker.initialize();
        PortConfig config;
        config.port = path;
        config.deassertControlLines = false;
        worker.openPort(config);
        worker.saveEntry(heartbeat(10));
        worker.setActive(10, true);
        worker.sendOnce(10);
        auto before = snapshot(worker);
        QVERIFY(before->connected);
        QVERIFY(before->tx[0].entry.active);
        QVERIFY(before->tx[0].entry.count > 0);
        const auto oldCount = before->tx[0].entry.count;
        QString error;
        const auto data = Profile::encode({heartbeat(0)}, error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        worker.importProfile(data, true, 11);
        auto appended = snapshot(worker);
        QCOMPARE(appended->tx.size(), size_t(2));
        QVERIFY(appended->tx[0].entry.active);
        QCOMPARE(appended->tx[0].entry.count, oldCount);
        QVERIFY(!appended->tx[1].entry.active);
        QCOMPARE(appended->tx[1].entry.count, uint64_t(0));
        worker.importProfile(data, false, 12);
        auto replaced = snapshot(worker);
        QCOMPARE(replaced->tx.size(), size_t(1));
        QVERIFY(!replaced->tx[0].entry.active);
        QCOMPARE(replaced->pending, size_t(0));
        worker.closePort();
        ::close(master);
        ::close(slave);
    }

    /** @brief setRate updates transmission frequency and validates range and link capacity. */
    void setRateUpdatesFrequencyAndValidatesBounds()
    {
        SerialWorker worker;
        worker.initialize();
        worker.saveEntry(heartbeat(1));

        // Test valid rate update when stopped
        worker.setRate(1, 10.0);
        auto current = snapshot(worker);
        QCOMPARE(current->tx.size(), size_t(1));
        QCOMPARE(current->tx[0].entry.hz, 10.0);

        // Test out-of-range rates trigger failure signal
        QSignalSpy failedSpy(&worker, &SerialWorker::failure);
        worker.setRate(1, 0.05);
        QCOMPARE(failedSpy.size(), 1);
        worker.setRate(1, 1500.0);
        QCOMPARE(failedSpy.size(), 2);
        worker.setRate(1, std::numeric_limits<double>::quiet_NaN());
        QCOMPARE(failedSpy.size(), 3);

        // Verify frequency unchanged after failed attempts
        current = snapshot(worker);
        QCOMPARE(current->tx[0].entry.hz, 10.0);
    }
};

QTEST_GUILESS_MAIN(ProfileWorkerTests)
#include "profile_worker_tests.moc"
