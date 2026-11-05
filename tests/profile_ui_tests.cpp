#include "profile.h"
#include "window.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <functional>

/** @brief Exercise portable profile workflows through public UI controls. */
class ProfileUiTests : public QObject
{
    Q_OBJECT
    QTemporaryDir directory;

    /** @brief Create a valid profile fixture with distinguishable sender identities. */
    QString fixture(const QString &name, const std::vector<int> &identities)
    {
        std::vector<Mav::TxEntry> entries;
        for (int identity : identities)
        {
            Mav::TxEntry entry;
            entry.id = MAVLINK_MSG_ID_HEARTBEAT;
            entry.sysid = identity;
            entry.compid = 154;
            entry.hz = 2;
            entry.payload.resize(mavlink_get_msg_entry(entry.id)->max_msg_len);
            entry.payload[8] = 3;
            entries.push_back(entry);
        }
        QString error;
        const QByteArray data = Profile::encode(entries, error);
        if (data.isEmpty())
        {
            return {};
        }
        const QString path = directory.filePath(name + ".mavprofile");
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size())
        {
            return {};
        }
        return path;
    }

    /** @brief Drive an import modal with a bounded watchdog even when an assertion fails. */
    void importDialog(MainWindow &window, const std::function<void(QDialog *)> &interaction)
    {
        auto *action = window.findChild<QAction *>("actionImportProfile");
        QVERIFY(action);
        QTimer watchdog;
        watchdog.setSingleShot(true);
        connect(&watchdog, &QTimer::timeout, &window, []()
        {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
            {
                dialog->reject();
            }
        });
        watchdog.start(5000);
        bool visited = false;
        QTimer::singleShot(20, &window, [&]()
        {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            QCOMPARE(dialog->objectName(), QString("importProfileDialog"));
            visited = true;
            interaction(dialog);
        });
        action->trigger();
        watchdog.stop();
        QVERIFY(visited);
    }

    /** @brief Import a fixture using the same controls offered to the user. */
    void load(MainWindow &window, const QString &path, bool append = false)
    {
        importDialog(window, [&](QDialog *dialog)
        {
            auto *pathEdit = dialog->findChild<QLineEdit *>("profilePath");
            auto *appendBox = dialog->findChild<QCheckBox *>("appendProfile");
            auto *confirm = dialog->findChild<QPushButton *>("confirmImportProfile");
            QVERIFY(pathEdit && appendBox && confirm);
            QVERIFY(!appendBox->isChecked());
            pathEdit->setText(path);
            appendBox->setChecked(append);
            QVERIFY(confirm->isEnabled());
            confirm->click();
        });
    }

private slots:
    /** @brief Redirect all settings and native file dialogs into isolated test facilities. */
    void initTestCase()
    {
        QVERIFY(directory.isValid());
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, directory.path());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    }

    /** @brief Start each test with empty application preferences. */
    void init()
    {
        for (const QString &scope : {QString("Layout"), QString("Preferences"), QString("Workspace")})
        {
            QSettings settings("MAVLinkSimulator", scope);
            settings.clear();
            settings.sync();
        }
    }

    /** @brief Verify Replace, Append duplicates, default reset and cancellation. */
    void importWorkflow()
    {
        MainWindow window;
        window.show();
        auto *table = window.findChild<QTableWidget *>("txTable");
        QVERIFY(table);
        const QString first = fixture("first", {11, 12});
        const QString second = fixture("second", {21});
        QVERIFY(!first.isEmpty() && !second.isEmpty());
        load(window, first);
        QTRY_COMPARE(table->rowCount(), 2);
        load(window, second);
        QTRY_COMPARE(table->rowCount(), 1);
        load(window, second, true);
        QTRY_COMPARE(table->rowCount(), 2);
        for (int row = 0; row < table->rowCount(); ++row)
        {
            QCOMPARE(table->item(row, 6)->text(), QString("Stopped"));
            QCOMPARE(table->item(row, 5)->text().toULongLong(), qulonglong(0));
        }
        importDialog(window, [&](QDialog *dialog)
        {
            auto *append = dialog->findChild<QCheckBox *>("appendProfile");
            QVERIFY(append);
            QVERIFY(!append->isChecked());
            dialog->findChild<QLineEdit *>("profilePath")->setText(first);
            auto *buttons = dialog->findChild<QDialogButtonBox *>();
            QVERIFY(buttons && buttons->button(QDialogButtonBox::Cancel));
            buttons->button(QDialogButtonBox::Cancel)->click();
        });
        QTest::qWait(150);
        QCOMPARE(table->rowCount(), 2);
        const qulonglong firstKey = table->item(0, 0)->data(Qt::UserRole).toULongLong();
        const qulonglong secondKey = table->item(1, 0)->data(Qt::UserRole).toULongLong();
        QTimer::singleShot(20, &window, [&]()
        {
            auto *dialog = window.findChild<QDialog *>("messageDialog");
            QVERIFY(dialog);
            QTimer::singleShot(3000, dialog, &QDialog::reject);
            auto *buttons = dialog->findChild<QDialogButtonBox *>();
            QVERIFY(buttons && buttons->button(QDialogButtonBox::Save));
            QTest::mouseClick(buttons->button(QDialogButtonBox::Save), Qt::LeftButton, Qt::ControlModifier);
        });
        window.findChild<QPushButton *>("addMessage")->click();
        QTRY_COMPARE(table->rowCount(), 3);
        QCOMPARE(table->item(0, 0)->data(Qt::UserRole).toULongLong(), firstKey);
        QCOMPARE(table->item(1, 0)->data(Qt::UserRole).toULongLong(), secondKey);
        const qulonglong addedKey = table->item(2, 0)->data(Qt::UserRole).toULongLong();
        QVERIFY(addedKey != firstKey && addedKey != secondKey);
    }

    /** @brief Reject invalid input without replacing existing TX definitions. */
    void invalidImport()
    {
        MainWindow window;
        window.show();
        const QString valid = fixture("valid", {31, 32});
        QVERIFY(!valid.isEmpty());
        load(window, valid);
        auto *table = window.findChild<QTableWidget *>("txTable");
        QTRY_COMPARE(table->rowCount(), 2);
        QFile bad(directory.filePath("invalid.mavprofile"));
        QVERIFY(bad.open(QIODevice::WriteOnly));
        QCOMPARE(bad.write("invalid CBOR"), qint64(12));
        bad.close();
        importDialog(window, [&](QDialog *dialog)
        {
            auto *path = dialog->findChild<QLineEdit *>("profilePath");
            auto *confirm = dialog->findChild<QPushButton *>("confirmImportProfile");
            auto *error = dialog->findChild<QLabel *>("profileError");
            QVERIFY(path && confirm && error);
            path->setText(bad.fileName());
            confirm->click();
            QVERIFY(dialog->isVisible());
            QTRY_VERIFY(!error->text().isEmpty());
            QCOMPARE(table->rowCount(), 2);
            dialog->reject();
        });
        QCOMPARE(table->rowCount(), 2);
    }

    /** @brief Export the current worker definitions and decode the resulting portable file. */
    void exportRoundTrip()
    {
        MainWindow window;
        window.show();
        const QString source = fixture("export-source", {41, 42});
        QVERIFY(!source.isEmpty());
        load(window, source);
        QTRY_COMPARE(window.findChild<QTableWidget *>("txTable")->rowCount(), 2);
        auto *action = window.findChild<QAction *>("actionExportProfile");
        QVERIFY(action && action->isEnabled());
        const QString target = directory.filePath("export-result.mavprofile");
        QTimer watchdog;
        watchdog.setSingleShot(true);
        connect(&watchdog, &QTimer::timeout, &window, []()
        {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
            {
                dialog->reject();
            }
        });
        watchdog.start(5000);
        bool visited = false;
        QTimer::singleShot(20, &window, [&]()
        {
            auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            visited = true;
            dialog->setDirectory(directory.path());
            for (auto *edit : dialog->findChildren<QLineEdit *>())
            {
                if (edit->text().contains("TX messages.mavprofile"))
                {
                    edit->setText(QFileInfo(target).fileName());
                }
            }
            dialog->selectFile(target);
            QTRY_COMPARE(dialog->selectedFiles(), QStringList{target});
            QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
        });
        action->trigger();
        watchdog.stop();
        QVERIFY(visited);
        QTRY_VERIFY(QFile::exists(target));
        QFile file(target);
        QVERIFY(file.open(QIODevice::ReadOnly));
        std::vector<Mav::TxEntry> entries;
        QString error;
        QVERIFY2(Profile::decode(file.readAll(), entries, error), qPrintable(error));
        QCOMPARE(entries.size(), size_t(2));
        QCOMPARE(int(entries[0].sysid), 41);
        QCOMPARE(int(entries[1].sysid), 42);
        QCOMPARE(entries[0].payload[8], uint8_t(3));
        QVERIFY(!entries[0].active && !entries[1].active);
    }

    /** @brief Render both themes and interaction states without changing the user's settings. */
    void rendering()
    {
        MainWindow window;
        window.show();
        QDir().mkpath("qa");
        auto *theme = window.findChild<QComboBox *>("theme");
        QVERIFY(theme);
        const QString selectedProfile = fixture("render-profile", {51, 52});
        QVERIFY(!selectedProfile.isEmpty());
        for (int index : {1, 2})
        {
            const QString mode = index == 1 ? "light" : "dark";
            theme->setCurrentIndex(index);
            auto *menu = window.findChild<QMenu *>("fileMenu");
            QVERIFY(menu);
            menu->popup(window.mapToGlobal(QPoint(10, 30)));
            QTest::qWait(30);
            QVERIFY(menu->grab().save("qa/profile-file-menu-" + mode + ".png"));
            menu->hide();
            importDialog(window, [&](QDialog *dialog)
            {
                auto *append = dialog->findChild<QCheckBox *>("appendProfile");
                auto *path = dialog->findChild<QLineEdit *>("profilePath");
                auto *confirm = dialog->findChild<QPushButton *>("confirmImportProfile");
                auto *error = dialog->findChild<QLabel *>("profileError");
                auto *badge = dialog->findChild<QLabel *>("profileModeBadge");
                auto *hint = dialog->findChild<QLabel *>("profileHint");
                auto *safety = dialog->findChild<QLabel *>("profileSafetyHint");
                QVERIFY(append && path && confirm && error && badge && hint && safety);
                QVERIFY(!append->isChecked());
                QCOMPARE(badge->text(), QString("Replace"));
                QCOMPARE(hint->text(), QString("Current TX messages will be replaced."));
                QCOMPARE(safety->text(), QString("Imported messages always start stopped."));
                QVERIFY(!confirm->isEnabled());
                QTest::qWait(30);
                QVERIFY(dialog->grab().save("qa/profile-empty-" + mode + ".png"));
                path->setText(selectedProfile);
                QVERIFY(confirm->isEnabled());
                QTest::qWait(20);
                QVERIFY(dialog->grab().save("qa/profile-selected-replace-" + mode + ".png"));
                append->setFocus(Qt::TabFocusReason);
                QTRY_VERIFY(append->hasFocus());
                QTest::mouseMove(append, QPoint(10, append->height() / 2));
                QTest::mousePress(append, Qt::LeftButton, Qt::NoModifier, QPoint(10, append->height() / 2));
                QVERIFY(append->isDown());
                QVERIFY(dialog->grab().save("qa/profile-pressed-" + mode + ".png"));
                QTest::mouseRelease(append, Qt::LeftButton, Qt::NoModifier, QPoint(10, append->height() / 2));
                QVERIFY(append->isChecked());
                QCOMPARE(badge->text(), QString("Append"));
                QCOMPARE(hint->text(), QString("Current TX messages will be kept. New messages are added at the end."));
                QCOMPARE(safety->text(), QString("Imported messages always start stopped."));
                QTest::qWait(20);
                QVERIFY(dialog->grab().save("qa/profile-selected-append-" + mode + ".png"));
                QVERIFY(dialog->grab().save("qa/profile-append-focus-hover-" + mode + ".png"));
                QTest::keyClick(append, Qt::Key_Space);
                QVERIFY(!append->isChecked());
                QCOMPARE(badge->text(), QString("Replace"));
                QCOMPARE(hint->text(), QString("Current TX messages will be replaced."));
                QTest::keyClick(append, Qt::Key_Space);
                QVERIFY(append->isChecked());
                append->setEnabled(false);
                QVERIFY(dialog->grab().save("qa/profile-disabled-" + mode + ".png"));
                append->setEnabled(true);
                path->setText(directory.filePath("missing.mavprofile"));
                confirm->click();
                QVERIFY(!error->text().isEmpty());
                QTest::qWait(20);
                QVERIFY(dialog->grab().save("qa/profile-error-" + mode + ".png"));
                dialog->resize(dialog->width() + 120, dialog->height() + 60);
                QTest::qWait(20);
                for (QWidget *control : {static_cast<QWidget *>(path), static_cast<QWidget *>(append),
                                         static_cast<QWidget *>(confirm), static_cast<QWidget *>(error),
                                         static_cast<QWidget *>(badge), static_cast<QWidget *>(hint),
                                         static_cast<QWidget *>(safety)})
                {
                    QVERIFY(dialog->rect().contains(QRect(control->mapTo(dialog, QPoint()), control->size())));
                }
                QVERIFY(dialog->grab().save("qa/profile-resized-" + mode + ".png"));
                dialog->reject();
            });
        }
    }
};

QTEST_MAIN(ProfileUiTests)
#include "profile_ui_tests.moc"
