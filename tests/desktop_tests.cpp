#include "window.h"
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QMenuBar>
#include <QAction>
#include <QDir>
#include <QSignalSpy>
#include <QSpinBox>
#include <QScrollBar>
#include <QListWidget>
#include <QDoubleSpinBox>
#include <QTest>
#include <QSplitter>
#include <QPlainTextEdit>
#include <QToolButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QHeaderView>
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
    QTemporaryDir settingsDirectory;
private slots:
    /** @brief Isolate presentation preferences from the user's application configuration. */
    void initTestCase()
    {
        QVERIFY(settingsDirectory.isValid());
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settingsDirectory.path());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    }

    /** @brief Start each test with default presentation state. */
    void init()
    {
        QSettings("MAVLinkSimulator", "Layout").clear();
        QSettings("MAVLinkSimulator", "Preferences").clear();
        QSettings("MAVLinkSimulator", "Workspace").clear();
    }

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

    /** @brief Verify batch start capacity checks and deletion cancel every queued stream. */
    void batchTransmission()
    {
        Pty pty;
        QVERIFY(pty.master >= 0);
        SerialWorker worker;
        worker.initialize();
        QSignalSpy snapshots(&worker, &SerialWorker::snapshotReady);
        QSignalSpy errors(&worker, &SerialWorker::failure);
        PortConfig config;
        config.port = pty.name;
        config.baud = 9600;
        worker.openPort(config);
        Mav::TxEntry entry;
        entry.id = 0;
        entry.hz = 30;
        entry.payload.resize(mavlink_get_msg_entry(0)->max_msg_len);
        for (quint64 key : {1, 2})
        {
            entry.key = key;
            worker.saveEntry(entry);
        }
        worker.runAll();
        worker.requestSnapshot();
        auto snapshot = qvariant_cast<SnapshotPtr>(snapshots.last()[0]);
        QVERIFY(!errors.empty());
        for (const auto &view : snapshot->tx)
        {
            QVERIFY(!view.entry.active);
        }
        entry.hz = 10;
        for (quint64 key : {1, 2})
        {
            entry.key = key;
            worker.saveEntry(entry);
        }
        worker.runAll();
        QTest::qWait(150);
        worker.requestSnapshot();
        snapshot = qvariant_cast<SnapshotPtr>(snapshots.last()[0]);
        for (const auto &view : snapshot->tx)
        {
            QVERIFY(view.entry.active && view.entry.count > 0);
        }
        worker.deleteAll();
        worker.requestSnapshot();
        snapshot = qvariant_cast<SnapshotPtr>(snapshots.last()[0]);
        QVERIFY(snapshot->tx.empty());
        QCOMPARE(snapshot->pending, size_t(0));
        pty.read();
        QTest::qWait(150);
        QVERIFY(pty.read().isEmpty());
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
        auto *menuBar = window.findChild<QMenuBar *>("appMenuBar");
        auto *actionSettings = window.findChild<QAction *>("actionSettings");
        auto *actionExit = window.findChild<QAction *>("actionExit");
        QVERIFY(port && connectButton && add && tx && theme && rx && menuBar && actionSettings && actionExit);
        auto *settings = window.findChild<QPushButton *>("serialSettings");
        auto *advanced = window.findChild<QWidget *>("serialAdvanced");
        auto *connectionBar = window.findChild<QWidget *>("connectionBar");
        QVERIFY(settings && advanced && connectionBar);
        QVERIFY(!advanced->isVisible());
        const int collapsedHeight = connectionBar->height();
        QVERIFY(collapsedHeight <= 76);
        settings->click();
        QTRY_VERIFY(advanced->isVisible());
        QCOMPARE(connectionBar->height(), collapsedHeight);
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
            QCOMPARE(buttons->button(QDialogButtonBox::Save)->text(), QString("Add && start"));
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
            QVERIFY(fields->item(uint8Row, 2)->data(Qt::UserRole + 1).toBool());
            QVERIFY(dialog->grab().save("qa/message-validation-light.png"));
            fields->item(uint8Row, 2)->setText("0");
            QVERIFY(!fields->item(uint8Row, 2)->data(Qt::UserRole + 1).toBool());
            QVERIFY(dialog->findChild<QLabel *>("validationError")->isHidden());
            buttons->button(QDialogButtonBox::Save)->click();
        });
        add->click();
        QTRY_COMPARE(tx->rowCount(), 1);
        tx->setCurrentCell(0, 0);
        auto *once = window.findChild<QAction *>("sendOnce");
        auto *start = window.findChild<QAction *>("startTx");
        auto *stop = window.findChild<QAction *>("stopTx");
        QTRY_COMPARE(tx->item(0, 6)->text(), QString("Running"));
        QTRY_VERIFY(tx->item(0, 5)->text().toULongLong() > 0);
        QVERIFY(!pty.read().isEmpty());
        stop->trigger();
        QTRY_COMPARE(tx->item(0, 6)->text(), QString("Stopped"));
        QVERIFY(once->isEnabled());
        once->trigger();
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
        QVERIFY(window.findChild<QLabel *>("displayPaused")->isVisible());
        const auto frozenCount = message->text(2);
        QCOMPARE(::write(pty.master, frame.data(), frame.size()), ssize_t(frame.size()));
        QTest::qWait(300);
        QCOMPARE(message->text(2), frozenCount);
        pause->click();
        QVERIFY(window.findChild<QLabel *>("displayPaused")->isHidden());
        QTRY_VERIFY(message->text(2) != frozenCount);
        start->trigger();
        QTest::qWait(100);
        stop->trigger();
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
            QCOMPARE(dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->text(), QString("Save changes"));
            QVERIFY(dialog->grab().save("qa/message-dark.png"));
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
        });
        window.findChild<QAction *>("editMessage")->trigger();
    }

    /** @brief Verify catalog filtering, contextual payload guidance and editor geometry in both themes. */
    void messageEditorUi()
    {
        MainWindow window;
        window.show();
        QDir().mkpath("qa");
        for (int mode : {1, 2})
        {
            window.findChild<QComboBox *>("theme")->setCurrentIndex(mode);
            QTimer::singleShot(50, &window, [&]()
            {
                auto *dialog = window.findChild<QDialog *>("messageDialog");
                QVERIFY(dialog);
                QTimer::singleShot(3000, dialog, &QDialog::reject);
                auto *catalog = dialog->findChild<QListWidget *>("messageCatalog");
                auto *search = dialog->findChild<QLineEdit *>("catalogSearch");
                auto *fields = dialog->findChild<QTableWidget *>("payloadFields");
                auto *title = dialog->findChild<QLabel *>("selectedMessageTitle");
                auto *hint = dialog->findChild<QLabel *>("payloadHint");
                auto *buttons = dialog->findChild<QDialogButtonBox *>();
                auto *splitter = dialog->findChild<QSplitter *>("messageSplitter");
                QVERIFY(catalog && search && fields && title && hint && buttons && splitter);
                QCOMPARE(buttons->button(QDialogButtonBox::Save)->text(), QString("Add message"));
                QCOMPARE(title->text(), QString("[0] HEARTBEAT"));
                auto *rate = dialog->findChild<QDoubleSpinBox *>();
                QVERIFY(rate);
                QCOMPARE(rate->accessibleName(), QString("Rate (Hz)"));
                for (auto *sender : dialog->findChildren<QSpinBox *>())
                {
                    QCOMPARE(sender->value(), 1);
                }
                rate->setValue(0);
                QCOMPARE(rate->value(), 1.0);
                rate->setValue(1001);
                QCOMPARE(rate->value(), 1000.0);
                rate->setValue(10.4);
                QCOMPARE(rate->value(), 10.0);
                QCOMPARE(rate->text(), QString("10"));
                rate->stepUp();
                QCOMPARE(rate->value(), 11.0);
                rate->setValue(1);
                fields->editItem(fields->item(0, 2));
                auto *valueEditor = fields->findChild<QLineEdit *>("payloadValueEditor");
                QVERIFY(valueEditor);
                QCOMPARE(valueEditor->accessibleName(), fields->item(0, 0)->text());
                valueEditor->selectAll();
                QTest::keyClicks(valueEditor, "1");
                QTest::keyClick(valueEditor, Qt::Key_Return);
                QTRY_COMPARE(fields->item(0, 2)->text(), QString("1"));

                search->setText("no_such_message");
                QVERIFY(dialog->findChild<QLabel *>("catalogEmpty")->isVisible());
                QCOMPARE(dialog->findChild<QLabel *>("catalogResultCount")->text(), QString("0 results"));
                search->setText("heartbeat");
                QVERIFY(catalog->isVisible());
                QVERIFY(!catalog->item(0)->isHidden());
                QCOMPARE(fields->item(0, 2)->text(), QString("1"));
                search->setText("1");
                QCOMPARE(catalog->item(0)->data(Qt::UserRole).toUInt(), 1U);
                QVERIFY(search->isClearButtonEnabled());
                auto *clearSearch = search->findChild<QToolButton *>();
                QVERIFY(clearSearch);
                QTest::qWait(180);
                QTest::mouseClick(clearSearch, Qt::LeftButton);
                QTRY_VERIFY(search->text().isEmpty());
                QCOMPARE(catalog->currentItem()->data(Qt::UserRole).toUInt(), 0U);
                // Validate on commit, including corrections that remain invalid.
                fields->item(0, 2)->setText("256");
                QVERIFY(fields->item(0, 2)->data(Qt::UserRole + 1).toBool());
                fields->item(0, 2)->setText("-1");
                QVERIFY(fields->item(0, 2)->data(Qt::UserRole + 1).toBool());
                QVERIFY(dialog->grab().save(mode == 1 ? "qa/editor-inline-error-light.png" : "qa/editor-inline-error-dark.png"));
                catalog->setCurrentRow(1);
                catalog->setCurrentRow(0);
                QCOMPARE(fields->item(0, 2)->text(), QString("-1"));
                QVERIFY(fields->item(0, 2)->data(Qt::UserRole + 1).toBool());
                fields->item(0, 2)->setText("1");
                QVERIFY(!fields->item(0, 2)->data(Qt::UserRole + 1).toBool());
                // Switching messages commits an active native editor into that message's draft.
                fields->editItem(fields->item(0, 2));
                valueEditor = fields->findChild<QLineEdit *>("payloadValueEditor");
                QVERIFY(valueEditor);
                valueEditor->setText("2");
                catalog->setCurrentRow(1);
                catalog->setCurrentRow(0);
                QCOMPARE(fields->item(0, 2)->text(), QString("2"));
                fields->item(0, 2)->setText("1");
                for (int row = 0; row < fields->rowCount(); ++row)
                {
                    if (fields->item(row, 0)->text() == "mavlink_version")
                    {
                        QVERIFY(!(fields->item(row, 2)->flags() & Qt::ItemIsEditable));
                        QCOMPARE(fields->item(row, 2)->text(), QString("3"));
                        fields->setCurrentCell(row, 2);
                        QVERIFY(hint->text().contains("Read-only"));
                    }
                }
                const int beforeResize = splitter->sizes().first();
                splitter->setSizes({360, 580});
                QTest::qWait(20);
                QVERIFY(splitter->sizes().first() > beforeResize);
                auto *theme = window.findChild<QComboBox *>("theme");
                theme->setCurrentIndex(mode == 1 ? 2 : 1);
                QTest::qWait(20);
                QCOMPARE(fields->palette().color(QPalette::Window).lightness() < 128, mode == 1);
                theme->setCurrentIndex(mode);
                QTest::qWait(20);
                QVERIFY(dialog->grab().save(mode == 1 ? "qa/editor-light.png" : "qa/editor-dark.png"));
                // Exercise char and numeric array help using the actual catalog metadata.
                for (bool charArray : {true, false})
                {
                    bool found = false;
                    for (int i = 0; i < catalog->count() && !found; ++i)
                    {
                        const auto *info = mavlink_get_message_info_by_id(catalog->item(i)->data(Qt::UserRole).toUInt());
                        for (unsigned row = 0; row < info->num_fields; ++row)
                        {
                            const auto &field = info->fields[row];
                            if (field.array_length && ((field.type == MAVLINK_TYPE_CHAR) == charArray))
                            {
                                catalog->setCurrentRow(i);
                                fields->setCurrentCell(int(row), 2);
                                QVERIFY(hint->isVisible());
                                QVERIFY(hint->text().contains(charArray ? "UTF-8" : "comma-separated"));
                                auto *arrayButton = dialog->findChild<QPushButton *>("editPayloadArray");
                                QVERIFY(arrayButton && arrayButton->isVisible());
                                const QString original = fields->item(int(row), 2)->text();
                                // Cancel leaves the table untouched, including multiline input.
                                QTimer::singleShot(20, dialog, [&]()
                                {
                                    auto *arrayDialog = dialog->findChild<QDialog *>("arrayDialog");
                                    QVERIFY(arrayDialog);
                                    auto *input = arrayDialog->findChild<QPlainTextEdit *>("arrayValueEditor");
                                    QVERIFY(input);
                                    input->setPlainText("discarded draft");
                                    arrayDialog->reject();
                                });
                                arrayButton->click();
                                QCOMPARE(fields->item(int(row), 2)->text(), original);
                                QTimer::singleShot(20, dialog, [&]()
                                {
                                    auto *arrayDialog = dialog->findChild<QDialog *>("arrayDialog");
                                    QVERIFY(arrayDialog);
                                    QTimer::singleShot(1500, arrayDialog, &QDialog::reject);
                                    auto *input = arrayDialog->findChild<QPlainTextEdit *>("arrayValueEditor");
                                    auto *actions = arrayDialog->findChild<QDialogButtonBox *>();
                                    QVERIFY(input && actions);
                                    input->setPlainText(charArray ? QString(field.array_length + 1, 'x') : "invalid");
                                    actions->button(QDialogButtonBox::Save)->click();
                                    QVERIFY(arrayDialog->isVisible());
                                    QVERIFY(arrayDialog->findChild<QLabel *>("validationError")->isVisible());
                                    QVERIFY(arrayDialog->grab().save(mode == 1 ? "qa/array-error-light.png" : "qa/array-error-dark.png"));
                                    QStringList numbers;
                                    for (unsigned n = 0; n < field.array_length; ++n)
                                    {
                                        numbers.append("1");
                                    }
                                    input->setPlainText(charArray ? "sample" : numbers.join(",\n"));
                                    actions->button(QDialogButtonBox::Save)->click();
                                    QVERIFY(!arrayDialog->isVisible());
                                });
                                arrayButton->click();
                                QVERIFY(fields->item(int(row), 2)->text() != original);
                                QVERIFY(!fields->item(int(row), 2)->data(Qt::UserRole + 1).toBool());
                                found = true;
                                break;
                            }
                        }
                    }
                    QVERIFY(found);
                }
                auto *parameterSearch = dialog->findChild<QLineEdit *>("parameterSearch");
                QVERIFY(parameterSearch);
                bool manyFields = false;
                for (int i = 0; i < catalog->count(); ++i)
                {
                    const auto *info = mavlink_get_message_info_by_id(catalog->item(i)->data(Qt::UserRole).toUInt());
                    if (info->num_fields > 12)
                    {
                        catalog->setCurrentRow(i);
                        QVERIFY(parameterSearch->isVisible());
                        parameterSearch->setText("no_such_field");
                        for (int row = 0; row < fields->rowCount(); ++row)
                        {
                            QVERIFY(fields->isRowHidden(row));
                        }
                        parameterSearch->setText(fields->item(0, 0)->text());
                        QVERIFY(!fields->isRowHidden(0));
                        const QString originalValue = fields->item(0, 2)->text();
                        fields->item(0, 2)->setText("invalid");
                        QVERIFY(fields->item(0, 2)->data(Qt::UserRole + 1).toBool());
                        parameterSearch->setText("no_such_field");
                        buttons->button(QDialogButtonBox::Save)->click();
                        QVERIFY(dialog->isVisible());
                        QVERIFY(!fields->isRowHidden(0));
                        QVERIFY(parameterSearch->text().isEmpty());
                        fields->item(0, 2)->setText(originalValue);
                        QVERIFY(!fields->item(0, 2)->data(Qt::UserRole + 1).toBool());
                        QVERIFY(dialog->grab().save(mode == 1 ? "qa/editor-many-light.png" : "qa/editor-many-dark.png"));
                        manyFields = true;
                        break;
                    }
                }
                QVERIFY(manyFields);
                catalog->setCurrentRow(0);
                QVERIFY(parameterSearch->isHidden());
                QCOMPARE(fields->item(0, 2)->text(), QString("1"));
                dialog->resize(880, 600);
                splitter->setSizes({320, 540});
                QTest::qWait(30);
                for (auto *control : dialog->findChildren<QSpinBox *>())
                {
                    const QRect bounds(control->mapTo(dialog, QPoint(0, 0)), control->size());
                    QVERIFY(dialog->rect().contains(bounds));
                    QCOMPARE(control->height(), 34);
                }
                const QRect footer(buttons->mapTo(dialog, QPoint(0, 0)), buttons->size());
                QVERIFY(dialog->rect().contains(footer));
                QVERIFY(footer.top() > splitter->geometry().bottom());
                QVERIFY(footer.right() > dialog->width() - 30);
                QVERIFY(buttons->button(QDialogButtonBox::Cancel)->geometry().right() <
                        buttons->button(QDialogButtonBox::Save)->geometry().left());
                QVERIFY(dialog->grab().save(mode == 1 ? "qa/editor-small-light.png" : "qa/editor-small-dark.png"));
                dialog->reject();
            });
            window.findChild<QPushButton *>("addMessage")->click();
        }
    }

    /** @brief Restore geometry, splitters and column widths without restoring UART or TX state. */
    void layoutPreferences()
    {
        QSize size;
        QList<int> dashboardSizes;
        QList<int> inspectorSizes;
        {
            MainWindow window;
            window.show();
            window.resize(1200, 800);
            auto *dashboards = window.findChild<QSplitter *>("dashboards");
            auto *rxSplit = window.findChild<QSplitter *>("rxSplit");
            dashboards->setSizes({430, 700});
            rxSplit->setSizes({230, 390});
            auto *rx = window.findChild<QTreeWidget *>("rxTree");
            auto *fields = window.findChild<QTableWidget *>("inspector");
            rx->setColumnWidth(2, 96);
            fields->setColumnWidth(0, 185);
            QTest::qWait(30);
            size = window.size();
            dashboardSizes = dashboards->sizes();
            inspectorSizes = rxSplit->sizes();
            QVERIFY(window.close());
        }
        {
            MainWindow window;
            window.show();
            QTest::qWait(30);
            QCOMPARE(window.size(), size);
            const auto actual = window.findChild<QSplitter *>("dashboards")->sizes();
            QVERIFY(qAbs(actual[0] - dashboardSizes[0]) <= 2);
            const auto restored = window.findChild<QSplitter *>("rxSplit")->sizes();
            QVERIFY(qAbs(restored[0] - inspectorSizes[0]) <= 2);
            QCOMPARE(window.findChild<QTreeWidget *>("rxTree")->columnWidth(2), 96);
            QCOMPARE(window.findChild<QTableWidget *>("inspector")->columnWidth(0), 185);
            QCOMPARE(window.findChild<QPushButton *>("connect")->text(), QString("Connect"));
            QCOMPARE(window.findChild<QTableWidget *>("txTable")->rowCount(), 0);
            QVERIFY(window.findChild<QTableWidget *>("inspector")->isColumnHidden(3));
        }
        // Invalid or obsolete preferences must fall back to a usable default layout.
        QSettings settings("MAVLinkSimulator", "Layout");
        settings.setValue("geometry", QByteArray("invalid"));
        settings.setValue("dashboards", QByteArray("invalid"));
        MainWindow window;
        window.show();
        QTest::qWait(20);
        QVERIFY(window.width() >= 1000);
        QVERIFY(window.findChild<QSplitter *>("dashboards")->sizes().first() >= 340);
    }

    /** @brief Verify application settings persistence and default MAVLink sender identity in new messages. */
    void applicationSettingsPreferences()
    {
        {
            MainWindow window;
            window.show();
            QTest::qWait(20);
            auto *theme = window.findChild<QComboBox *>("theme");
            auto *sysIdBox = window.findChild<QSpinBox *>("defaultSysId");
            auto *compIdBox = window.findChild<QSpinBox *>("defaultCompId");
            auto *versionBox = window.findChild<QComboBox *>("defaultVersion");
            auto *restoreTx = window.findChild<QCheckBox *>("restoreTxMessages");
            QVERIFY(theme && sysIdBox && compIdBox && versionBox && restoreTx);

            QCOMPARE(sysIdBox->value(), 1);
            QCOMPARE(compIdBox->value(), 1);
            QCOMPARE(versionBox->currentIndex(), 0);
            QCOMPARE(restoreTx->isChecked(), false);

            sysIdBox->setValue(42);
            compIdBox->setValue(190);
            versionBox->setCurrentIndex(1);
            theme->setCurrentIndex(1);
            restoreTx->setChecked(true);

            QTimer::singleShot(50, &window, [&]()
            {
                auto *dialog = window.findChild<QDialog *>("messageDialog");
                QVERIFY(dialog);
                auto *sysid = dialog->findChild<QSpinBox *>("messageSysId");
                auto *compid = dialog->findChild<QSpinBox *>("messageCompId");
                auto *version = dialog->findChild<QComboBox *>("messageVersion");
                QVERIFY(sysid && compid && version);
                QCOMPARE(sysid->value(), 42);
                QCOMPARE(compid->value(), 190);
                QCOMPARE(version->currentIndex(), 1);
                dialog->reject();
            });
            window.findChild<QPushButton *>("addMessage")->click();
            QTest::qWait(100);
            QVERIFY(window.close());
        }

        {
            MainWindow window;
            window.show();
            QTest::qWait(20);
            auto *theme = window.findChild<QComboBox *>("theme");
            auto *sysIdBox = window.findChild<QSpinBox *>("defaultSysId");
            auto *compIdBox = window.findChild<QSpinBox *>("defaultCompId");
            auto *versionBox = window.findChild<QComboBox *>("defaultVersion");
            auto *restoreTx = window.findChild<QCheckBox *>("restoreTxMessages");
            QVERIFY(theme && sysIdBox && compIdBox && versionBox && restoreTx);

            QCOMPARE(sysIdBox->value(), 42);
            QCOMPARE(compIdBox->value(), 190);
            QCOMPARE(versionBox->currentIndex(), 1);
            QCOMPARE(theme->currentIndex(), 1);
            QCOMPARE(restoreTx->isChecked(), true);
        }
    }

    /** @brief Verify TX message list persistence across sessions and stopped initial state. */
    void workspaceTxPersistence()
    {
        {
            MainWindow window;
            window.show();
            auto *tx = window.findChild<QTableWidget *>("txTable");
            auto *add = window.findChild<QPushButton *>("addMessage");
            auto *restoreTx = window.findChild<QCheckBox *>("restoreTxMessages");
            QVERIFY(tx && add && restoreTx);
            QCOMPARE(restoreTx->isChecked(), false);
            restoreTx->setChecked(true);

            QTimer::singleShot(30, &window, [&window]()
            {
                auto *dialog = window.findChild<QDialog *>("messageDialog");
                QVERIFY(dialog);
                auto *catalog = dialog->findChild<QListWidget *>("messageCatalog");
                QVERIFY(catalog);
                for (int row = 0; row < catalog->count(); ++row)
                {
                    if (catalog->item(row)->data(Qt::UserRole).toUInt() == 0)
                    {
                        catalog->setCurrentRow(row);
                        break;
                    }
                }
                dialog->findChild<QDoubleSpinBox *>()->setValue(5.0);
                dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
            });
            add->click();
            QTRY_COMPARE(tx->rowCount(), 1);
            QVERIFY(window.close());
        }

        {
            MainWindow window;
            window.show();
            auto *tx = window.findChild<QTableWidget *>("txTable");
            QVERIFY(tx);
            QTRY_COMPARE(tx->rowCount(), 1);
            QCOMPARE(tx->item(0, 0)->text(), QString("0"));
            QCOMPARE(tx->item(0, 1)->text(), QString("HEARTBEAT"));
            QCOMPARE(tx->item(0, 3)->text(), QString("5"));
            QCOMPARE(tx->item(0, 6)->text(), QString("Stopped"));
            QCOMPARE(tx->item(0, 5)->text(), QString("0"));

            auto *restoreTx = window.findChild<QCheckBox *>("restoreTxMessages");
            QVERIFY(restoreTx);
            restoreTx->setChecked(false);
            QVERIFY(window.close());
        }

        {
            MainWindow window;
            window.show();
            auto *tx = window.findChild<QTableWidget *>("txTable");
            QVERIFY(tx);
            QTest::qWait(250);
            QCOMPARE(tx->rowCount(), 0);
        }
    }

    /** @brief Verify resetting window geometry, panel splitters and column widths to defaults. */
    void workspaceResetLayout()
    {
        MainWindow window;
        window.show();
        window.resize(1100, 750);
        auto *dashboards = window.findChild<QSplitter *>("dashboards");
        auto *rxSplit = window.findChild<QSplitter *>("rxSplit");
        auto *resetBtn = window.findChild<QPushButton *>("resetLayout");
        QVERIFY(dashboards && rxSplit && resetBtn);

        dashboards->setSizes({480, 620});
        rxSplit->setSizes({220, 380});
        QTest::qWait(30);

        resetBtn->click();
        QTest::qWait(30);

        const auto dashSizes = dashboards->sizes();
        QVERIFY(qAbs(dashSizes[0] - 360) <= 2);
        const auto splitSizes = rxSplit->sizes();
        QVERIFY(qAbs(splitSizes[0] - splitSizes[1]) <= 2);
        QVERIFY(window.close());
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
        QVERIFY(tx->item(2, 1)->toolTip().isEmpty());
        QVERIFY(tx->item(2, 1)->data(Qt::AccessibleTextRole).toString().contains("Target 10 Hz"));
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
        auto *fieldTitle = window.findChild<QLabel *>("inspectorMessageTitle");
        QVERIFY(fieldTitle);
        QTRY_COMPARE(fieldTitle->text(), QString("[30] ATTITUDE"));
        QCOMPARE(window.findChild<QLabel *>("inspectorSource")->text(), QString("System 1 · Component 1"));
        const QPoint valuePosition = inspector->visualItemRect(inspector->item(0, 2)).center();
        const QString expectedValue = inspector->item(0, 2)->text();
        QVERIFY(QMetaObject::invokeMethod(inspector, "customContextMenuRequested", Qt::DirectConnection,
                                        Q_ARG(QPoint, valuePosition)));
        auto *valueMenu = window.findChild<QMenu *>("inspectorMenu");
        auto *copyValue = window.findChild<QAction *>("copyInspectorValue");
        QVERIFY(valueMenu && copyValue && valueMenu->isVisible());
        copyValue->trigger();
        QCOMPARE(QApplication::clipboard()->text(), expectedValue);
        valueMenu->hide();
        rx->setCurrentItem(rx->topLevelItem(0)->child(1)->child(0));
        QTRY_COMPARE(fieldTitle->text(), QString("[33] GLOBAL_POSITION_INT"));
        QCOMPARE(window.findChild<QLabel *>("inspectorSource")->text(), QString("System 1 · Component 42"));
        rx->setCurrentItem(rx->topLevelItem(0)->child(0)->child(1));
        tx->setCurrentCell(0, 1);
        window.findChild<QAction *>("startTx")->trigger();
        QTRY_COMPARE(tx->item(0, 6)->text(), QString("Running"));
        QVERIFY(window.findChild<QAction *>("stopTx")->isVisible());
        QVERIFY(!window.findChild<QAction *>("startTx")->isVisible());
        auto *filter = window.findChild<QLineEdit *>("rxFilter");
        QVERIFY(filter && filter->isClearButtonEnabled());
        filter->setText("ATTITUDE");
        QCOMPARE(window.findChild<QLabel *>("summaryBadge")->text(), QString("1 / 3 streams"));
        filter->setText("no_such_message");
        QCOMPARE(window.findChild<QLabel *>("summaryBadge")->text(), QString("0 / 3 streams"));
        filter->clear();
        QDir().mkpath("qa");
        for (int mode : {1, 2})
        {
            theme->setCurrentIndex(mode);
            QTest::mouseMove(tx->viewport(), tx->visualItemRect(tx->item(1, 1)).center());
            QTest::qWait(30);
            QVERIFY(window.grab().save(mode == 1 ? "qa/modern-populated-light.png" : "qa/modern-populated-dark.png"));
            window.resize(1000, 720);
            QTest::qWait(30);
            QVERIFY(!window.findChild<QPushButton *>("sendOnce"));
            const QPoint target = tx->visualItemRect(tx->item(1, 1)).center();
            QVERIFY(QMetaObject::invokeMethod(tx, "customContextMenuRequested", Q_ARG(QPoint, target)));
            auto *menu = window.findChild<QMenu *>("txMessageMenu");
            QVERIFY(menu && menu->isVisible());
            QCOMPARE(tx->currentRow(), 1);
            QVERIFY(window.findChild<QAction *>("startTx")->isVisible());
            QVERIFY(!window.findChild<QAction *>("stopTx")->isVisible());
            QVERIFY(menu->grab().save(mode == 1 ? "qa/tx-menu-light.png" : "qa/tx-menu-dark.png"));
            menu->hide();
            tx->setCurrentCell(0, 1);
            QVERIFY(window.grab().save(mode == 1 ? "qa/modern-small-light.png" : "qa/modern-small-dark.png"));
            // Wider cards may fit the longer identity on one line.
            QVERIFY(tx->rowHeight(2) >= tx->rowHeight(0));
            auto *pause = window.findChild<QPushButton *>("pauseDisplay");
            pause->click();
            QVERIFY(window.findChild<QLabel *>("displayPaused")->isVisible());
            QVERIFY(window.grab().save(mode == 1 ? "qa/paused-light.png" : "qa/paused-dark.png"));
            pause->click();
            QVERIFY(window.findChild<QLabel *>("displayPaused")->isHidden());
            window.resize(1440, 900);
            QTest::qWait(30);
        }
        const QPoint secondCard = tx->visualItemRect(tx->item(1, 1)).center();
        QVERIFY(QMetaObject::invokeMethod(tx, "customContextMenuRequested", Q_ARG(QPoint, secondCard)));
        window.findChild<QAction *>("startTx")->trigger();
        window.findChild<QMenu *>("txMessageMenu")->hide();
        QTRY_COMPARE(tx->item(1, 6)->text(), QString("Running"));
        QCOMPARE(tx->item(0, 6)->text(), QString("Running"));
        window.findChild<QAction *>("stopTx")->trigger();
        QTRY_COMPARE(tx->item(1, 6)->text(), QString("Stopped"));
        QCOMPARE(tx->item(0, 6)->text(), QString("Running"));
        tx->setCurrentCell(0, 1);
        // Test inline toggle directly on the card badge.
        const QRect card0 = tx->visualItemRect(tx->item(0, 1)).adjusted(4, 6, -8, -6);
        const QPoint badgePos(card0.right() - 50, card0.top() + 18);
        QTest::mouseClick(tx->viewport(), Qt::LeftButton, Qt::NoModifier, badgePos);
        QTRY_COMPARE(tx->item(0, 6)->text(), QString("Stopped"));
        QTest::mouseClick(tx->viewport(), Qt::LeftButton, Qt::NoModifier, badgePos);
        QTRY_COMPARE(tx->item(0, 6)->text(), QString("Running"));
        auto *runAll = window.findChild<QPushButton *>("runAll");
        auto *stopAll = window.findChild<QPushButton *>("stopAll");
        auto *deleteAll = window.findChild<QPushButton *>("deleteAll");
        QVERIFY(runAll && stopAll && deleteAll);
        QVERIFY(stopAll->geometry().top() > tx->geometry().bottom());
        stopAll->click();
        for (int row = 0; row < tx->rowCount(); ++row)
        {
            QTRY_COMPARE(tx->item(row, 6)->text(), QString("Stopped"));
        }
        runAll->click();
        for (int row = 0; row < tx->rowCount(); ++row)
        {
            QTRY_COMPARE(tx->item(row, 6)->text(), QString("Running"));
        }
        deleteAll->click();
        QTRY_COMPARE(tx->rowCount(), 0);
        QVERIFY(!runAll->isEnabled() && !stopAll->isEnabled() && !deleteAll->isEnabled());
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
            auto *restoreTx = window.findChild<QCheckBox *>("restoreTxMessages");
            QVERIFY(restoreTx);
            QCOMPARE(restoreTx->property("darkTheme").toBool(), index == 2);
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
            auto *settingsDialog = window.findChild<QDialog *>("settingsDialog");
            QVERIFY(settingsDialog);
            settingsDialog->show();
            QVERIFY(settingsDialog->windowFlags() & Qt::FramelessWindowHint);
            auto *titleBar = settingsDialog->findChild<QWidget *>("dialogTitleBar");
            QVERIFY(titleBar);
            // Explicit theme switches must repaint an already visible dialog header.
            for (int liveMode : {2, 1, index})
            {
                theme->setCurrentIndex(liveMode);
                QTest::qWait(10);
                const QColor expected(liveMode == 2 ? "#0d1422" : "#edf1f8");
                QCOMPARE(titleBar->grab().toImage().pixelColor(2, 2), expected);
            }
            theme->showPopup();
            QTest::qWait(20);
            QVERIFY(theme->view()->window()->grab().save("qa/popup-" + mode + ".png"));
            theme->hidePopup();
            QVERIFY(settingsDialog->grab().save("qa/settings-dialog-" + mode + ".png"));
            settingsDialog->findChild<QPushButton *>("closeDialog")->click();
            QVERIFY(!settingsDialog->isVisible());
            QTimer::singleShot(30, &window, [&window, mode, index]()
            {
                auto *about = window.findChild<QDialog *>("aboutDialog");
                QVERIFY(about && about->isVisible());
                QVERIFY(about->windowFlags() & Qt::FramelessWindowHint);
                auto *header = about->findChild<QWidget *>("dialogTitleBar");
                QVERIFY(header);
                QCOMPARE(header->grab().toImage().pixelColor(2, 2),
                         QColor(index == 2 ? "#0d1422" : "#edf1f8"));
                QVERIFY(about->grab().save("qa/about-" + mode + ".png"));
                if (index == 2)
                {
                    QTest::keyClick(about, Qt::Key_Escape);
                }
                else
                {
                    about->findChild<QPushButton *>("closeDialog")->click();
                }
                QVERIFY(!about->isVisible());
            });
            window.findChild<QAction *>("actionAbout")->trigger();
            QTest::mouseClick(settings, Qt::LeftButton);
            QVERIFY(!settings->property("keyboardFocus").toBool());
            QTest::qWait(20);
            QVERIFY(window.grab().save("qa/serial-settings-" + mode + ".png"));
            auto *serialMenu = window.findChild<QMenu *>("serialMenu");
            QVERIFY(serialMenu);
            QVERIFY(serialMenu->grab().save("qa/uart-popup-" + mode + ".png"));
            serialMenu->hide();
            window.resize(1000, 720);
            QTest::qWait(20);
            settings->click();
            QTest::qWait(20);
            auto *advanced = window.findChild<QWidget *>("serialAdvanced");
            QVERIFY(advanced);
            auto *bar = window.findChild<QWidget *>("connectionBar");
            auto *title = window.findChild<QLabel *>("heading");
            QVERIFY(bar && title);
            QCOMPARE(title->mapTo(window.centralWidget(), title->contentsRect().topLeft()).x(),
                     tx->parentWidget()->mapTo(window.centralWidget(), QPoint(0, 0)).x());
            QVERIFY(!bar->isAncestorOf(title));
            const QRect titleBounds(title->mapTo(window.centralWidget(), QPoint(0, 0)), title->size());
            const QRect barBounds(bar->mapTo(window.centralWidget(), QPoint(0, 0)), bar->size());
            QVERIFY(titleBounds.right() < barBounds.left());
            QVERIFY(bar->width() <= 700);
            auto *dashboards = window.findChild<QWidget *>("dashboards");
            QVERIFY(dashboards);
            const QRect dashboardBounds(dashboards->mapTo(window.centralWidget(), QPoint(0, 0)), dashboards->size());
            QCOMPARE(barBounds.right(), dashboardBounds.right());
            int previousRight = -1;
            for (const auto &name : {"port", "refreshPorts", "baudRate", "serialSettings", "connectionCard"})
            {
                auto *control = window.findChild<QWidget *>(name);
                QVERIFY(control);
                const QRect bounds(control->mapTo(bar, QPoint(0, 0)), control->size());
                QVERIFY2(bar->rect().contains(bounds), name);
                QVERIFY2(bounds.left() > previousRight, name);
                const QRect centralBounds(control->mapTo(window.centralWidget(), QPoint(0, 0)), control->size());
                QVERIFY2(centralBounds.top() <= titleBounds.center().y() &&
                         centralBounds.bottom() >= titleBounds.center().y(), name);
                previousRight = bounds.right();
            }
            for (auto *control : advanced->findChildren<QComboBox *>())
            {
                const QRect bounds(control->mapTo(advanced, QPoint(0, 0)), control->size());
                QVERIFY(advanced->rect().contains(bounds));
            }
            QVERIFY(serialMenu->grab().save("qa/uart-popup-small-" + mode + ".png"));
            QVERIFY(window.grab().save("qa/serial-settings-small-" + mode + ".png"));
            window.resize(1440, 900);
            QTest::qWait(20);
            serialMenu->hide();
            window.activateWindow();
            QTest::qWait(20);
            settings->setFocus(Qt::TabFocusReason);
            QTest::keyClick(settings, Qt::Key_Space);
            QVERIFY(window.findChild<QWidget *>("serialAdvanced")->isVisible());
            QVERIFY(window.grab().save("qa/keyboard-settings-" + mode + ".png"));
            QTest::keyClick(serialMenu, Qt::Key_Escape);
            QTRY_VERIFY(!window.findChild<QWidget *>("serialAdvanced")->isVisible());
            QVERIFY(!settings->isChecked());
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
