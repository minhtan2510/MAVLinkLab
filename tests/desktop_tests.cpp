#include "window.h"
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QSignalSpy>
#include <QSpinBox>
#include <QScrollBar>
#include <QListWidget>
#include <QDoubleSpinBox>
#include <QTest>
#include <fcntl.h>
#include <pty.h>
#include <unistd.h>
#include <cerrno>

class Pty
{
public:
    int master = -1;
    int slave = -1;
    char name[256]{};
    /** @brief Allocate a nonblocking PTY master and retain the slave path. */
    Pty()
    {
        if (openpty(&master, &slave, name, nullptr, nullptr) == 0)
        {
            fcntl(master, F_SETFL, fcntl(master, F_GETFL) | O_NONBLOCK);
        }
    }
    /** @brief Release both PTY handles. */
    ~Pty()
    {
        if (master >= 0)
        {
            ::close(master);
        }
        if (slave >= 0)
        {
            ::close(slave);
        }
    }
    /** @brief Read all currently available master output. */
    QByteArray read()
    {
        QByteArray output;
        char data[4096];
        ssize_t bytes = 0;
        while ((bytes = ::read(master, data, sizeof(data))) > 0)
        {
            output.append(data, int(bytes));
        }
        return output;
    }
};

class DesktopTests : public QObject
{
    Q_OBJECT
private slots:
    /** @brief Verify serial RX/TX, silence on connect, stop/reconnect and error state. */
    void serialPty()
    {
        Pty pty;
        QVERIFY(pty.master >= 0);
        SerialWorker worker;
        worker.initialize();
        QSignalSpy states(&worker, &SerialWorker::stateChanged);
        QSignalSpy errors(&worker, &SerialWorker::failure);
        QSignalSpy snapshots(&worker, &SerialWorker::snapshotReady);
        PortConfig config;
        config.port = pty.name;
        worker.openPort(config);
        QVERIFY(states.last()[1].toBool());
        QTest::qWait(50);
        QVERIFY(pty.read().isEmpty());
        Mav::TxEntry entry;
        entry.key = 1;
        entry.id = 0;
        entry.hz = 20;
        entry.payload.resize(mavlink_get_msg_entry(0)->max_msg_len);
        worker.saveEntry(entry);
        worker.sendOnce(1);
        QTest::qWait(50);
        Mav::Parser parser;
        auto output = pty.read();
        QVERIFY(!output.isEmpty());
        auto decoded = parser.feed(Mav::Bytes(output.begin(), output.end()));
        QCOMPARE(decoded.size(), size_t(1));
        QCOMPARE(uint32_t(decoded[0].msgid), uint32_t(0));
        // Feed real serial fragments and an independent component.
        mavlink_status_t txStatus{};
        auto frame = Mav::encode(0, 7, 42, false, entry.payload, txStatus);
        QCOMPARE(::write(pty.master, frame.data(), 4), ssize_t(4));
        QTest::qWait(20);
        worker.requestSnapshot();
        QVERIFY(qvariant_cast<SnapshotPtr>(snapshots.last()[0])->rx.empty());
        QCOMPARE(::write(pty.master, frame.data() + 4, frame.size() - 4), ssize_t(frame.size() - 4));
        frame = Mav::encode(0, 7, 43, true, entry.payload, txStatus);
        QCOMPARE(::write(pty.master, frame.data(), frame.size()), ssize_t(frame.size()));
        QTest::qWait(50);
        worker.requestSnapshot();
        QCOMPARE(qvariant_cast<SnapshotPtr>(snapshots.last()[0])->rx.size(), size_t(2));
        worker.setActive(1, true);
        QTest::qWait(160);
        output = pty.read();
        QVERIFY(!output.isEmpty());
        worker.closePort();
        pty.read();
        worker.openPort(config);
        QTest::qWait(150);
        QVERIFY(pty.read().isEmpty());
        worker.requestSnapshot();
        const auto reopened = qvariant_cast<SnapshotPtr>(snapshots.last()[0]);
        QCOMPARE(reopened->pending, size_t(0));
        QVERIFY(!reopened->tx[0].entry.active);
        // Deliberately overload a configured link and verify rejection.
        worker.closePort();
        config.baud = 9600;
        worker.openPort(config);
        entry.hz = 1000;
        worker.saveEntry(entry);
        worker.setActive(1, true);
        worker.requestSnapshot();
        QVERIFY(!qvariant_cast<SnapshotPtr>(snapshots.last()[0])->tx[0].entry.active);
        QVERIFY(!errors.empty());
        worker.closePort();
        config.port = "/dev/mavlink-simulator-nonexistent";
        worker.openPort(config);
        QVERIFY(!states.last()[1].toBool());
        worker.closePort();
    }

    /** @brief Verify a stalled serial peer cannot grow queues without a bound. */
    void serialBackpressure()
    {
        Pty pty;
        QVERIFY(pty.master >= 0);
        SerialWorker worker;
        worker.initialize();
        QSignalSpy snapshots(&worker, &SerialWorker::snapshotReady);
        QSignalSpy states(&worker, &SerialWorker::stateChanged);
        PortConfig config;
        config.port = pty.name;
        config.baud = 921600;
        worker.openPort(config);
        QVERIFY(states.last()[1].toBool());
        Mav::TxEntry entry;
        entry.key = 1;
        entry.id = MAVLINK_MSG_ID_ENCAPSULATED_DATA;
        entry.hz = 300;
        entry.payload.assign(mavlink_get_msg_entry(entry.id)->max_msg_len, 0xff);
        worker.saveEntry(entry);
        worker.setActive(1, true);
        // Do not read the PTY master: eventually the kernel output path stalls.
        QTest::qWait(2200);
        worker.requestSnapshot();
        const auto snapshot = qvariant_cast<SnapshotPtr>(snapshots.last()[0]);
        QVERIFY(snapshot->pending <= Mav::WriteQueue::Limit + 4096);
        QVERIFY(!snapshot->tx[0].entry.active);
        QVERIFY(snapshot->tx[0].entry.error.find("queue full") != std::string::npos);
        worker.closePort();
        worker.requestSnapshot();
        QCOMPARE(qvariant_cast<SnapshotPtr>(snapshots.last()[0])->pending, size_t(0));
        pty.read();
        worker.openPort(config);
        QTest::qWait(100);
        QVERIFY(pty.read().isEmpty());
        worker.closePort();
    }

    /** @brief Exercise real widgets and save offscreen renders for visual review. */
    void desktopFlow()
    {
        Pty pty;
        QVERIFY(pty.master >= 0);
        MainWindow window;
        window.show();
        QTest::qWait(100);
        auto *port = window.findChild<QComboBox *>("port");
        auto *connectButton = window.findChild<QPushButton *>("connect");
        auto *add = window.findChild<QPushButton *>("addMessage");
        auto *tx = window.findChild<QTableWidget *>("txTable");
        auto *theme = window.findChild<QComboBox *>("theme");
        auto *rx = window.findChild<QTreeWidget *>("rxTree");
        QVERIFY(port && connectButton && add && tx && theme && rx);
        auto *settings = window.findChild<QPushButton *>("serialSettings");
        auto *advanced = window.findChild<QWidget *>("serialAdvanced");
        auto *connectionBar = window.findChild<QWidget *>("connectionBar");
        QVERIFY(settings && advanced && connectionBar);
        QVERIFY(!advanced->isVisible());
        const int collapsedHeight = connectionBar->height();
        QVERIFY(collapsedHeight <= 76);
        settings->click();
        QTRY_VERIFY(advanced->isVisible());
        QTRY_VERIFY(connectionBar->height() > collapsedHeight);
        auto controls = advanced->findChildren<QComboBox *>();
        QCOMPARE(controls.size(), 4);
        auto *summary = window.findChild<QLabel *>("serialSummary");
        QVERIFY(summary);
        controls[3]->setCurrentIndex(1);
        QVERIFY(summary->text().contains("RTS/CTS"));
        controls[3]->setCurrentIndex(0);
        QVERIFY(summary->text().contains("No flow"));
        controls[2]->setCurrentIndex(1);
        settings->click();
        QTRY_VERIFY(!advanced->isVisible());
        QCOMPARE(controls[2]->currentText(), QString("2"));
        qInfo() << "Compact UART height:" << collapsedHeight << "px";
        port->setCurrentText(pty.name);
        QTest::mouseClick(window.findChild<QPushButton *>("refreshPorts"), Qt::LeftButton);
        QCOMPARE(port->currentText(), QString::fromLocal8Bit(pty.name));
        connectButton->click();
        QTRY_COMPARE(connectButton->text(), QString("Disconnect"));
        settings->click();
        for (auto *control : controls)
        {
            QVERIFY(!control->isEnabled());
        }
        settings->click();
        QVERIFY(pty.read().isEmpty());
        QDir().mkpath("qa");
        QTimer::singleShot(50, &window, [&]()
        {
            auto *dialog = window.findChild<QDialog *>("messageDialog");
            QVERIFY(dialog);
            auto *fields = dialog->findChild<QTableWidget *>("payloadFields");
            auto *buttons = dialog->findChild<QDialogButtonBox *>();
            QVERIFY(fields && buttons);
            const auto senderControls = dialog->findChildren<QSpinBox *>();
            QCOMPARE(senderControls.size(), 2);
            auto *component = senderControls[1];
            const int originalComponent = component->value();
            QTest::mouseClick(component, Qt::LeftButton, Qt::NoModifier, QPoint(component->width() - 10, 7));
            QCOMPARE(component->value(), originalComponent + 1);
            QTest::mouseClick(component, Qt::LeftButton, Qt::NoModifier, QPoint(component->width() - 10, component->height() - 7));
            QCOMPARE(component->value(), originalComponent);
            int uint8Row = -1;
            for (int i = 0; i < fields->rowCount(); ++i)
            {
                if (fields->item(i, 1)->text() == "uint8_t")
                {
                    uint8Row = i;
                    break;
                }
            }
            QVERIFY(uint8Row >= 0);
            fields->item(uint8Row, 2)->setText("256");
            buttons->button(QDialogButtonBox::Save)->click();
            QVERIFY(dialog->isVisible());
            QVERIFY(!dialog->findChild<QLabel *>("validationError")->text().isEmpty());
            QVERIFY(dialog->grab().save("qa/message-validation-light.png"));
            fields->item(uint8Row, 2)->setText("0");
            buttons->button(QDialogButtonBox::Save)->click();
        });
        add->click();
        QTRY_COMPARE(tx->rowCount(), 1);
        tx->setCurrentCell(0, 0);
        auto *once = window.findChild<QPushButton *>("sendOnce");
        auto *start = window.findChild<QPushButton *>("startTx");
        auto *stop = window.findChild<QPushButton *>("stopTx");
        QVERIFY(once->isEnabled());
        once->click();
        QTest::qWait(100);
        QVERIFY(!pty.read().isEmpty());
        Mav::Bytes payload(mavlink_get_msg_entry(0)->max_msg_len);
        mavlink_status_t status{};
        const auto frame = Mav::encode(0, 1, 42, false, payload, status);
        QCOMPARE(::write(pty.master, frame.data(), frame.size()), ssize_t(frame.size()));
        QTRY_COMPARE(rx->topLevelItemCount(), 1);
        auto *message = rx->topLevelItem(0)->child(0)->child(0);
        rx->setCurrentItem(message);
        auto *inspector = window.findChild<QTableWidget *>("inspector");
        QTRY_VERIFY(inspector->rowCount() > 0);
        auto *filter = window.findChild<QLineEdit *>("rxFilter");
        QVERIFY(filter);
        filter->setText("no_such_message");
        auto *rxEmpty = rx->viewport()->findChild<QLabel *>("emptyState");
        QVERIFY(!rxEmpty);
        QVERIFY(rx->topLevelItem(0)->isHidden());
        filter->clear();
        QVERIFY(!rx->topLevelItem(0)->isHidden());
        QCOMPARE(rx->currentItem(), message);
        theme->setCurrentIndex(1);
        QTest::qWait(30);
        QVERIFY(window.grab().save("qa/dashboard-light.png"));
        // Snapshot updates retain selected stream and inspector field.
        inspector->setCurrentCell(0, 0);
        QTest::qWait(250);
        QCOMPARE(rx->currentItem(), message);
        QCOMPARE(inspector->currentRow(), 0);
        auto *pause = window.findChild<QPushButton *>("pauseDisplay");
        pause->click();
        const auto frozenCount = message->text(2);
        QCOMPARE(::write(pty.master, frame.data(), frame.size()), ssize_t(frame.size()));
        QTest::qWait(300);
        QCOMPARE(message->text(2), frozenCount);
        pause->click();
        QTRY_VERIFY(message->text(2) != frozenCount);
        start->click();
        QTest::qWait(100);
        stop->click();
        QTest::qWait(50);
        pty.read();
        QTest::qWait(1100);
        QVERIFY(pty.read().isEmpty());
        theme->setCurrentIndex(2);
        QTest::qWait(30);
        QVERIFY(window.grab().save("qa/dashboard-dark.png"));
        window.resize(1000, 640);
        QTest::qWait(30);
        QCOMPARE(rx->horizontalScrollBar()->maximum(), 0);
        QCOMPARE(inspector->horizontalScrollBar()->maximum(), 0);
        QVERIFY(window.grab().save("qa/dashboard-small-dark.png"));
        connectButton->click();
        QTRY_COMPARE(connectButton->text(), QString("Connect"));
        QVERIFY(!once->isEnabled());
        window.findChild<QPushButton *>("clearStatistics")->click();
        QTRY_COMPARE(rx->topLevelItemCount(), 0);
        // Open/edit/close an existing message to exercise catalog restoration.
        QTimer::singleShot(50, &window, [&]()
        {
            auto *dialog = window.findChild<QDialog *>("messageDialog");
            QVERIFY(dialog);
            QVERIFY(dialog->grab().save("qa/message-dark.png"));
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
        });
        window.findChild<QPushButton *>("editMessage")->click();
    }

    /** @brief Exercise card selection, scheduling and a populated modern dashboard through real widgets. */
    void modernDashboard()
    {
        Pty pty;
        QVERIFY(pty.master >= 0);
        MainWindow window;
        window.show();
        auto *theme = window.findChild<QComboBox *>("theme");
        theme->setCurrentIndex(1);
        auto *tx = window.findChild<QTableWidget *>("txTable");
        auto *add = window.findChild<QPushButton *>("addMessage");
        int count = 0;
        for (const uint32_t id : {0U, 30U, 33U, 76U})
        {
            QTimer::singleShot(30, &window, [&window, id]()
            {
                auto *dialog = window.findChild<QDialog *>("messageDialog");
                QVERIFY(dialog);
                auto *catalog = dialog->findChild<QListWidget *>("messageCatalog");
                QVERIFY(catalog);
                bool selected = false;
                for (int row = 0; row < catalog->count(); ++row)
                {
                    if (catalog->item(row)->data(Qt::UserRole).toUInt() == id)
                    {
                        catalog->setCurrentRow(row);
                        selected = true;
                        break;
                    }
                }
                QVERIFY(selected);
                dialog->findChild<QDoubleSpinBox *>()->setValue(id == 0 ? 1.0 : 10.0);
                dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
            });
            add->click();
            ++count;
            QTRY_COMPARE(tx->rowCount(), count);
        }
        const QRect card = tx->visualItemRect(tx->item(1, 1));
        QTest::mouseClick(tx->viewport(), Qt::LeftButton, Qt::NoModifier, card.center());
        QCOMPARE(tx->currentRow(), 1);
        QTest::keyClick(tx, Qt::Key_Down);
        QCOMPARE(tx->currentRow(), 2);
        QTest::qWait(250);
        QCOMPARE(tx->currentRow(), 2);
        QVERIFY(tx->item(2, 1)->toolTip().contains("Target 10.0 Hz"));
        window.findChild<QComboBox *>("port")->setCurrentText(pty.name);
        auto *connection = window.findChild<QPushButton *>("connect");
        connection->click();
        QTRY_COMPARE(connection->text(), QString("Disconnect"));
        mavlink_status_t status{};
        for (const uint32_t id : {0U, 30U, 33U})
        {
            Mav::Bytes payload(mavlink_get_msg_entry(id)->max_msg_len);
            const auto frame = Mav::encode(id, 1, id == 33 ? 42 : 1, false, payload, status);
            QCOMPARE(::write(pty.master, frame.data(), frame.size()), ssize_t(frame.size()));
        }
        auto *rx = window.findChild<QTreeWidget *>("rxTree");
        QTRY_COMPARE(rx->topLevelItemCount(), 1);
        QTRY_COMPARE(rx->topLevelItem(0)->childCount(), 2);
        rx->setCurrentItem(rx->topLevelItem(0)->child(0)->child(1));
        auto *inspector = window.findChild<QTableWidget *>("inspector");
        QTRY_VERIFY(inspector->rowCount() > 0);
        tx->setCurrentCell(0, 1);
        window.findChild<QPushButton *>("startTx")->click();
        QTRY_COMPARE(tx->item(0, 6)->text(), QString("Running"));
        QDir().mkpath("qa");
        for (int mode : {1, 2})
        {
            theme->setCurrentIndex(mode);
            QTest::mouseMove(tx->viewport(), tx->visualItemRect(tx->item(1, 1)).center());
            QTest::qWait(30);
            QVERIFY(window.grab().save(mode == 1 ? "qa/modern-populated-light.png" : "qa/modern-populated-dark.png"));
        }
        // Test inline toggle directly on the card badge.
        const QRect card0 = tx->visualItemRect(tx->item(0, 1)).adjusted(2, 6, -2, -6);
        const QPoint badgePos(card0.right() - 50, card0.top() + 18);
        QTest::mouseClick(tx->viewport(), Qt::LeftButton, Qt::NoModifier, badgePos);
        QTRY_COMPARE(tx->item(0, 6)->text(), QString("Stopped"));
        QTest::mouseClick(tx->viewport(), Qt::LeftButton, Qt::NoModifier, badgePos);
        QTRY_COMPARE(tx->item(0, 6)->text(), QString("Running"));
    }

    /** @brief Render keyboard focus, press, hover, popup and system theme changes. */
    void interactionStates()
    {
        MainWindow window;
        window.show();
        QTest::qWait(50);
        auto *theme = window.findChild<QComboBox *>("theme");
        auto *add = window.findChild<QPushButton *>("addMessage");
        auto *settings = window.findChild<QPushButton *>("serialSettings");
        QVERIFY(theme && add);
        QDir().mkpath("qa");
        for (int index : {1, 2})
        {
            const QString mode = index == 1 ? "light" : "dark";
            theme->setCurrentIndex(index);
            auto *tx = window.findChild<QTableWidget *>("txTable");
            auto *empty = tx->viewport()->findChild<QLabel *>("emptyState");
            QVERIFY(!empty);
            QCOMPARE(tx->rowCount(), 0);
            QVERIFY(window.grab().save("qa/empty-" + mode + ".png"));
            theme->setFocus();
            add->setFocus(Qt::TabFocusReason);
            QVERIFY(add->hasFocus());
            QVERIFY(add->property("keyboardFocus").toBool());
            const QRect buttonBounds = add->geometry();
            QTest::mouseMove(add, add->rect().center());
            QTest::qWait(20);
            QVERIFY(window.grab().save("qa/focus-hover-" + mode + ".png"));
            QTest::mousePress(add, Qt::LeftButton);
            QVERIFY(!add->property("keyboardFocus").toBool());
            QCOMPARE(add->geometry(), buttonBounds);
            QVERIFY(window.grab().save("qa/pressed-" + mode + ".png"));
            // Release outside the button so this state check does not open a dialog.
            QTest::mouseRelease(add, Qt::LeftButton, Qt::NoModifier, QPoint(-10, -10));
            theme->showPopup();
            QTest::qWait(20);
            QVERIFY(theme->view()->window()->grab().save("qa/popup-" + mode + ".png"));
            theme->hidePopup();
            QTest::mouseClick(settings, Qt::LeftButton);
            QVERIFY(!settings->property("keyboardFocus").toBool());
            QTest::qWait(20);
            QVERIFY(window.grab().save("qa/serial-settings-" + mode + ".png"));
            window.resize(1000, 720);
            QTest::qWait(20);
            auto *advanced = window.findChild<QWidget *>("serialAdvanced");
            QVERIFY(advanced);
            for (auto *control : advanced->findChildren<QComboBox *>())
            {
                const QRect bounds(control->mapTo(advanced, QPoint(0, 0)), control->size());
                QVERIFY(advanced->rect().contains(bounds));
            }
            QVERIFY(window.grab().save("qa/serial-settings-small-" + mode + ".png"));
            window.resize(1440, 900);
            QTest::qWait(20);
            QTest::mouseClick(settings, Qt::LeftButton);
            QTest::keyClick(settings, Qt::Key_Space);
            QVERIFY(settings->property("keyboardFocus").toBool());
            QVERIFY(window.findChild<QWidget *>("serialAdvanced")->isVisible());
            QVERIFY(window.grab().save("qa/keyboard-settings-" + mode + ".png"));
            QTest::keyClick(settings, Qt::Key_Space);
            QVERIFY(!window.findChild<QWidget *>("serialAdvanced")->isVisible());
        }
        window.resize(1000, 720);
        QTest::qWait(30);
        const QRect client = window.centralWidget()->rect();
        for (const auto &name : {"connect", "addMessage", "pauseDisplay", "clearStatistics"})
        {
            auto *control = window.findChild<QPushButton *>(name);
            QVERIFY(control);
            const QRect bounds(control->mapTo(window.centralWidget(), QPoint(0, 0)), control->size());
            QVERIFY2(client.contains(bounds), name);
        }
        QVERIFY(window.grab().save("qa/empty-small-dark.png"));
        const QPalette original = qApp->palette();
        theme->setCurrentIndex(0);
        QPalette simulated = original;
        simulated.setColor(QPalette::Window, QColor("#101010"));
        qApp->setPalette(simulated);
        QTest::qWait(20);
        QVERIFY(window.palette().color(QPalette::Window).lightness() < 128);
        simulated.setColor(QPalette::Window, QColor("#f0f0f0"));
        qApp->setPalette(simulated);
        QTest::qWait(20);
        QVERIFY(window.palette().color(QPalette::Window).lightness() > 128);
        qApp->setPalette(original);
    }
};

/** @brief Start the Qt integration tests with registered queued payload types. */
int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    qRegisterMetaType<PortConfig>();
    qRegisterMetaType<Mav::TxEntry>();
    qRegisterMetaType<SnapshotPtr>();
    DesktopTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "desktop_tests.moc"
