#include "plotter.h"
#include "worker.h"

#include <QApplication>
#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QTextStream>
#include <cmath>

#include <ardupilotmega/mavlink.h>

/**
 * @brief Automated test suite exercising TelemetryPlotWidget and PlotCanvasWidget independently.
 */
class PlotterTests : public QObject
{
    Q_OBJECT

private slots:
    /** @brief Test channel capacity limits, uniqueness, and removal. */
    void testChannelManagement()
    {
        TelemetryPlotWidget plotter;
        QCOMPARE(plotter.channelCount(), size_t(0));
        QVERIFY(plotter.channels().empty());

        // 1. Add first channel
        QVERIFY(plotter.addChannel(1, 1, 30, "roll", "ATTITUDE.roll"));
        QCOMPARE(plotter.channelCount(), size_t(1));
        QVERIFY(plotter.hasChannel(1, 1, 30, "roll"));

        // 2. Reject duplicate channel
        QVERIFY(!plotter.addChannel(1, 1, 30, "roll", "ATTITUDE.roll"));
        QCOMPARE(plotter.channelCount(), size_t(1));

        // 3. Add channels up to hard limit of 6
        QVERIFY(plotter.addChannel(1, 1, 30, "pitch", "ATTITUDE.pitch"));
        QVERIFY(plotter.addChannel(1, 1, 30, "yaw", "ATTITUDE.yaw"));
        QVERIFY(plotter.addChannel(1, 1, 30, "rollspeed", "ATTITUDE.rollspeed"));
        QVERIFY(plotter.addChannel(1, 1, 30, "pitchspeed", "ATTITUDE.pitchspeed"));
        QVERIFY(plotter.addChannel(1, 1, 30, "yawspeed", "ATTITUDE.yawspeed"));
        QCOMPARE(plotter.channelCount(), size_t(6));

        // 4. Reject 7th channel
        QVERIFY(!plotter.addChannel(1, 1, 30, "time_boot_ms", "ATTITUDE.time_boot_ms"));
        QCOMPARE(plotter.channelCount(), size_t(6));

        // 5. Remove channel by key
        QVERIFY(plotter.removeChannel(1, 1, 30, "yawspeed"));
        QCOMPARE(plotter.channelCount(), size_t(5));
        QVERIFY(!plotter.hasChannel(1, 1, 30, "yawspeed"));

        // Reject removing non-existent channel
        QVERIFY(!plotter.removeChannel(1, 1, 30, "yawspeed"));

        // 6. Remove channel by index
        plotter.removeChannelAt(0);
        QCOMPARE(plotter.channelCount(), size_t(4));
        QVERIFY(!plotter.hasChannel(1, 1, 30, "roll"));

        // Out-of-bounds remove does not crash
        plotter.removeChannelAt(999);
        QCOMPARE(plotter.channelCount(), size_t(4));
    }

    /** @brief Test snapshot ingestion and numeric field extraction for standard MAVLink messages. */
    void testProcessSnapshotNumeric()
    {
        TelemetryPlotWidget plotter;
        QVERIFY(plotter.addChannel(1, 1, 30, "roll", "ATTITUDE.roll"));
        QVERIFY(plotter.addChannel(1, 1, 30, "pitch", "ATTITUDE.pitch"));

        mavlink_attitude_t att{};
        att.time_boot_ms = 1000;
        att.roll = 0.85f;
        att.pitch = -0.42f;
        att.yaw = 1.57f;

        mavlink_message_t msg{};
        mavlink_msg_attitude_encode(1, 1, &msg, &att);

        Snapshot snapshot;
        RxView view{};
        view.message = msg;
        view.count = 1;
        view.hz = 10.0;
        view.age = 0.05;
        snapshot.rx.push_back(view);

        plotter.processSnapshot(snapshot, 1000000);

        const auto &channels = plotter.channels();
        QCOMPARE(channels.size(), size_t(2));
        QVERIFY(channels[0].hasValue);
        QVERIFY(std::abs(channels[0].latestValue - 0.85) < 1e-4);
        QCOMPARE(channels[0].buffer.size(), size_t(1));

        QVERIFY(channels[1].hasValue);
        QVERIFY(std::abs(channels[1].latestValue - (-0.42)) < 1e-4);
        QCOMPARE(channels[1].buffer.size(), size_t(1));

        // Feed another sample
        att.roll = 0.90f;
        mavlink_msg_attitude_encode(1, 1, &msg, &att);
        snapshot.rx[0].message = msg;
        plotter.processSnapshot(snapshot, 1100000);

        QCOMPARE(channels[0].buffer.size(), size_t(2));
        QVERIFY(std::abs(channels[0].latestValue - 0.90) < 1e-4);
    }

    /** @brief Test parameter protocol values with union type conversions. */
    void testProcessSnapshotParameters()
    {
        TelemetryPlotWidget plotter;
        QVERIFY(plotter.addChannel(1, 1, 22, "BATT_VOLT", "PARAM.BATT_VOLT"));
        QVERIFY(plotter.addChannel(1, 1, 22, "SYSID_MYID", "PARAM.SYSID_MYID"));

        Snapshot snapshot;
        const auto devKey = std::make_pair(uint32_t(1), uint8_t(1));
        auto &devStore = snapshot.params[devKey];

        // Float parameter
        Mav::ParamItem floatParam;
        floatParam.id = "BATT_VOLT";
        floatParam.type = MAV_PARAM_TYPE_REAL32;
        floatParam.value = 14.8f;
        devStore.byId["BATT_VOLT"] = floatParam;

        // Integer parameter punned into float
        mavlink_param_union_t u;
        u.param_int32 = 42;
        Mav::ParamItem intParam;
        intParam.id = "SYSID_MYID";
        intParam.type = MAV_PARAM_TYPE_INT32;
        intParam.value = u.param_float;
        devStore.byId["SYSID_MYID"] = intParam;

        plotter.processSnapshot(snapshot, 2000000);

        const auto &channels = plotter.channels();
        QCOMPARE(channels.size(), size_t(2));
        QVERIFY(channels[0].hasValue);
        QVERIFY(std::abs(channels[0].latestValue - 14.8) < 1e-4);

        QVERIFY(channels[1].hasValue);
        QVERIFY(std::abs(channels[1].latestValue - 42.0) < 1e-4);
    }

    /** @brief Test canvas controls, time windows, and auto-scale behaviors. */
    void testCanvasControlsAndTimeWindows()
    {
        TelemetryPlotWidget plotter;
        auto *canvas = plotter.canvas();
        QVERIFY(canvas != nullptr);

        // Time range options
        auto *timeBox = plotter.timeRangeBox();
        QVERIFY(timeBox != nullptr);

        timeBox->setCurrentIndex(0); // 10s
        QCOMPARE(canvas->timeWindow(), 10);
        timeBox->setCurrentIndex(2); // 60s
        QCOMPARE(canvas->timeWindow(), 60);
        timeBox->setCurrentIndex(3); // 120s
        QCOMPARE(canvas->timeWindow(), 120);
        timeBox->setCurrentIndex(1); // 30s
        QCOMPARE(canvas->timeWindow(), 30);

        // Auto-scale toggle
        auto *autoScaleBtn = plotter.autoScaleButton();
        QVERIFY(autoScaleBtn != nullptr);
        QVERIFY(canvas->isAutoScale());
        autoScaleBtn->click();
        QVERIFY(!canvas->isAutoScale());
        autoScaleBtn->click();
        QVERIFY(canvas->isAutoScale());

        // Pause toggle
        auto *pauseBtn = plotter.pauseButton();
        QVERIFY(pauseBtn != nullptr);
        QVERIFY(!canvas->isPaused());
        QCOMPARE(pauseBtn->text(), QString("Pause"));
        pauseBtn->click();
        QVERIFY(canvas->isPaused());
        QCOMPARE(pauseBtn->text(), QString("Resume"));
        pauseBtn->click();
        QVERIFY(!canvas->isPaused());
        QCOMPARE(pauseBtn->text(), QString("Pause"));
    }

    /** @brief Test CSV serialization and buffer clearing. */
    void testCsvExportAndDataClearing()
    {
        TelemetryPlotWidget plotter;
        QVERIFY(plotter.addChannel(1, 1, 30, "roll", "ATTITUDE.roll"));

        // Push samples directly to buffer
        plotter.channels()[0].buffer.push(1000000, 1.0);
        plotter.channels()[0].buffer.push(2000000, 2.0);
        plotter.channels()[0].latestValue = 2.0;
        plotter.channels()[0].hasValue = true;
        plotter.updateCanvas();

        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        const QString csvPath = tempDir.filePath("test_export.csv");

        QVERIFY(plotter.exportCsv(csvPath));
        QVERIFY(QFile::exists(csvPath));

        QFile csvFile(csvPath);
        QVERIFY(csvFile.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString content = QString::fromUtf8(csvFile.readAll());
        csvFile.close();

        QVERIFY(content.contains("Time_us,Relative_s,ATTITUDE.roll"));
        QVERIFY(content.contains("1000000,0.0000,1"));
        QVERIFY(content.contains("2000000,1.0000,2"));

        // Test clear
        plotter.clearAllData();
        QCOMPARE(plotter.channels()[0].buffer.size(), size_t(0));
        QVERIFY(!plotter.channels()[0].hasValue);
    }

    /** @brief Test cursor tracking, hover leave, and rendering without crashes. */
    void testHoverAndInspectionReadout()
    {
        TelemetryPlotWidget plotter;
        plotter.resize(400, 300);
        plotter.show();
        QVERIFY(plotter.addChannel(1, 1, 30, "roll", "ATTITUDE.roll"));
        plotter.channels()[0].buffer.push(1000000, 0.5);

        auto *canvas = plotter.canvas();
        QVERIFY(canvas != nullptr);

        // Move inside plot area
        QMouseEvent moveEvent(QEvent::MouseMove, QPointF(100, 100), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &moveEvent);

        // Move outside / leave event
        QEvent leaveEvent(QEvent::Leave);
        QApplication::sendEvent(canvas, &leaveEvent);

        // Render repaint
        canvas->repaint();
    }

    /** @brief Test light and dark theme mode switching. */
    void testThemeSwitching()
    {
        TelemetryPlotWidget plotter;
        QVERIFY(plotter.addChannel(1, 1, 30, "roll", "ATTITUDE.roll"));

        plotter.setDarkTheme(true);
        plotter.updateCanvas();

        plotter.setDarkTheme(false);
        plotter.updateCanvas();
    }
};

/** @brief Execute the standalone plotter test suite. */
int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    PlotterTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "plotter_tests.moc"
