/**
 * @file window_settings.cpp
 * @brief MainWindow persistent settings, profile import/export, updates, and shortcuts.
 */

#include "window.h"
#include "window_private.h"
#include "profile.h"
#include "version.h"
#include <QSettings>
#include <QFileDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalBlocker>
#include <QMessageBox>

/** @brief Reset application preferences and configurations to default values. */
void MainWindow::restoreDefaultSettings()
{
    const auto answer = QMessageBox::question(
        settingsDialog,
        "Restore Default Settings",
        "Are you sure you want to restore all settings to their default values?",
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (answer != QMessageBox::Yes)
    {
        return;
    }

    if (themeBox)
    {
        themeBox->setCurrentIndex(0);
    }
    if (showDebugBarBox)
    {
        showDebugBarBox->setChecked(true);
    }
    if (statsRateUnitBox)
    {
        statsRateUnitBox->setCurrentIndex(1);
    }
    if (defaultSysIdBox)
    {
        defaultSysIdBox->setValue(1);
    }
    if (defaultCompIdBox)
    {
        defaultCompIdBox->setValue(1);
    }
    if (defaultVersionBox)
    {
        defaultVersionBox->setCurrentIndex(0);
    }
    if (autoReconnectBox)
    {
        autoReconnectBox->setChecked(false);
    }
    if (restoreTxBox)
    {
        restoreTxBox->setChecked(false);
    }
    if (singleActiveStreamBox)
    {
        singleActiveStreamBox->setChecked(true);
    }
    if (deassertControlLinesBox)
    {
        deassertControlLinesBox->setChecked(true);
    }
    if (baudBox)
    {
        baudBox->setCurrentText("115200");
    }
    if (dataBox)
    {
        const int idx = dataBox->findText("8");
        dataBox->setCurrentIndex(idx >= 0 ? idx : 3);
    }
    if (parityBox)
    {
        parityBox->setCurrentIndex(0);
    }
    if (stopBox)
    {
        stopBox->setCurrentIndex(0);
    }
    if (flowBox)
    {
        flowBox->setCurrentIndex(0);
    }

    saveSettings();

    if (worker)
    {
        QMetaObject::invokeMethod(worker, "setAutoReconnect", Qt::QueuedConnection, Q_ARG(bool, false));
        QMetaObject::invokeMethod(worker, "setDeassertControlLines", Qt::QueuedConnection, Q_ARG(bool, true));
    }
    if (statusBarCard)
    {
        statusBarCard->setVisible(true);
    }
    if (statsRateUnitBox)
    {
        statsRateUnitBox->setEnabled(true);
    }
    if (statsRateUnitLabel)
    {
        statsRateUnitLabel->setEnabled(true);
    }
    if (metricsWidget)
    {
        metricsWidget->setRateUnit(1);
    }
    updateSerialSummary();

    if (lastError)
    {
        lastError->setText("Settings restored to defaults");
    }
}

/** @brief Restore application preferences such as theme and MAVLink defaults from storage. */
void MainWindow::restoreSettings()
{
    QSettings settings("MAVLinkSimulator", "Preferences");
    const QString savedPort = settings.value("serial/port", "").toString();
    if (portBox && !savedPort.isEmpty())
    {
        const QSignalBlocker blocker(portBox);
        portBox->setCurrentText(savedPort);
    }
    const QString savedBaud = settings.value("serial/baud", "").toString();
    if (baudBox && !savedBaud.isEmpty())
    {
        const QSignalBlocker blocker(baudBox);
        baudBox->setCurrentText(savedBaud);
    }
    const int themeIndex = settings.value("theme", 0).toInt();
    if (themeBox && themeIndex >= 0 && themeIndex < themeBox->count())
    {
        const QSignalBlocker blocker(themeBox);
        themeBox->setCurrentIndex(themeIndex);
    }
    const int sysid = settings.value("mavlink/defaultSysId", 1).toInt();
    if (defaultSysIdBox)
    {
        const QSignalBlocker blocker(defaultSysIdBox);
        defaultSysIdBox->setValue(qBound(1, sysid, 255));
    }
    const int compid = settings.value("mavlink/defaultCompId", 1).toInt();
    if (defaultCompIdBox)
    {
        const QSignalBlocker blocker(defaultCompIdBox);
        defaultCompIdBox->setValue(qBound(0, compid, 255));
    }
    const int version = settings.value("mavlink/defaultVersion", 0).toInt();
    if (defaultVersionBox && version >= 0 && version < defaultVersionBox->count())
    {
        const QSignalBlocker blocker(defaultVersionBox);
        defaultVersionBox->setCurrentIndex(version);
    }
    const bool autoReconnect = settings.value("serial/autoReconnect", false).toBool();
    if (autoReconnectBox)
    {
        const QSignalBlocker blocker(autoReconnectBox);
        autoReconnectBox->setChecked(autoReconnect);
    }
    if (worker && autoReconnectBox)
    {
        QMetaObject::invokeMethod(worker, "setAutoReconnect", Qt::QueuedConnection,
                                  Q_ARG(bool, autoReconnectBox->isChecked()));
    }
    const bool deassertControlLines = settings.value("serial/deassertControlLines", true).toBool();
    if (deassertControlLinesBox)
    {
        const QSignalBlocker blocker(deassertControlLinesBox);
        deassertControlLinesBox->setChecked(deassertControlLines);
    }
    if (worker)
    {
        QMetaObject::invokeMethod(worker, "setDeassertControlLines", Qt::QueuedConnection,
                                  Q_ARG(bool, deassertControlLines));
    }
    const bool restoreTx = settings.value("workspace/restoreTx", false).toBool();
    if (restoreTxBox)
    {
        const QSignalBlocker blocker(restoreTxBox);
        restoreTxBox->setChecked(restoreTx);
    }
    const bool singleActiveStream = settings.value("workspace/singleActiveStream", true).toBool();
    if (singleActiveStreamBox)
    {
        const QSignalBlocker blocker(singleActiveStreamBox);
        singleActiveStreamBox->setChecked(singleActiveStream);
    }
    if (worker && singleActiveStreamBox)
    {
        QMetaObject::invokeMethod(worker, "setSingleActiveStream", Qt::QueuedConnection,
                                  Q_ARG(bool, singleActiveStreamBox->isChecked()));
    }
    const bool showDebugBar = settings.value("ui/showDebugBar", true).toBool();
    if (showDebugBarBox)
    {
        const QSignalBlocker blocker(showDebugBarBox);
        showDebugBarBox->setChecked(showDebugBar);
    }
    if (statusBarCard)
    {
        statusBarCard->setVisible(showDebugBar);
    }
    const int statsUnit = settings.value("ui/statsRateUnit", 1).toInt();
    if (statsRateUnitBox && statsUnit >= 0 && statsUnit < statsRateUnitBox->count())
    {
        const QSignalBlocker blocker(statsRateUnitBox);
        statsRateUnitBox->setCurrentIndex(statsUnit);
    }
    if (statsRateUnitBox)
    {
        statsRateUnitBox->setEnabled(showDebugBar);
    }
    if (statsRateUnitLabel)
    {
        statsRateUnitLabel->setEnabled(showDebugBar);
    }
    if (metricsWidget)
    {
        metricsWidget->setRateUnit(statsRateUnitBox ? statsRateUnitBox->currentIndex() : 1);
    }
    const QString savedData = settings.value("serial/dataBits", "").toString();
    if (dataBox && !savedData.isEmpty())
    {
        const int idx = dataBox->findText(savedData);
        if (idx >= 0)
        {
            const QSignalBlocker blocker(dataBox);
            dataBox->setCurrentIndex(idx);
        }
    }
    const QVariant savedParity = settings.value("serial/parity");
    if (parityBox && savedParity.isValid())
    {
        const int pIdx = savedParity.toInt();
        if (pIdx >= 0 && pIdx < parityBox->count())
        {
            const QSignalBlocker blocker(parityBox);
            parityBox->setCurrentIndex(pIdx);
        }
    }
    const QVariant savedStop = settings.value("serial/stopBits");
    if (stopBox && savedStop.isValid())
    {
        const int sIdx = savedStop.toInt();
        if (sIdx >= 0 && sIdx < stopBox->count())
        {
            const QSignalBlocker blocker(stopBox);
            stopBox->setCurrentIndex(sIdx);
        }
    }
    const QVariant savedFlow = settings.value("serial/flowControl");
    if (flowBox && savedFlow.isValid())
    {
        const int fIdx = savedFlow.toInt();
        if (fIdx >= 0 && fIdx < flowBox->count())
        {
            const QSignalBlocker blocker(flowBox);
            flowBox->setCurrentIndex(fIdx);
        }
    }
    updateSerialSummary();
}

/** @brief Save application preferences such as theme and MAVLink defaults to storage. */
void MainWindow::saveSettings()
{
    QSettings settings("MAVLinkSimulator", "Preferences");
    if (themeBox)
    {
        settings.setValue("theme", themeBox->currentIndex());
    }
    if (showDebugBarBox)
    {
        settings.setValue("ui/showDebugBar", showDebugBarBox->isChecked());
    }
    if (statsRateUnitBox)
    {
        settings.setValue("ui/statsRateUnit", statsRateUnitBox->currentIndex());
    }
    if (defaultSysIdBox)
    {
        settings.setValue("mavlink/defaultSysId", defaultSysIdBox->value());
    }
    if (defaultCompIdBox)
    {
        settings.setValue("mavlink/defaultCompId", defaultCompIdBox->value());
    }
    if (defaultVersionBox)
    {
        settings.setValue("mavlink/defaultVersion", defaultVersionBox->currentIndex());
    }
    if (autoReconnectBox)
    {
        settings.setValue("serial/autoReconnect", autoReconnectBox->isChecked());
    }
    if (restoreTxBox)
    {
        settings.setValue("workspace/restoreTx", restoreTxBox->isChecked());
    }
    if (singleActiveStreamBox)
    {
        settings.setValue("workspace/singleActiveStream", singleActiveStreamBox->isChecked());
    }
    if (deassertControlLinesBox)
    {
        settings.setValue("serial/deassertControlLines", deassertControlLinesBox->isChecked());
    }
    if (baudBox)
    {
        settings.setValue("serial/baud", baudBox->currentText());
    }
    if (dataBox)
    {
        settings.setValue("serial/dataBits", dataBox->currentText());
    }
    if (parityBox)
    {
        settings.setValue("serial/parity", parityBox->currentIndex());
    }
    if (stopBox)
    {
        settings.setValue("serial/stopBits", stopBox->currentIndex());
    }
    if (flowBox)
    {
        settings.setValue("serial/flowControl", flowBox->currentIndex());
    }
    settings.sync();
}

/** @brief Import all definitions atomically; Append defaults to off for every dialog. */
void MainWindow::importProfile()
{
    if (profileBusy)
    {
        return;
    }
    ProfileImportDialog dialog(this);
    dialog.setObjectName("importProfileDialog");
    dialog.setWindowTitle("Import Profile");
    dialog.setMinimumWidth(560);
    dialog.resize(600, 420);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 16, 20, 20);
    layout->setSpacing(16);
    layout = addDialogTitleBar(&dialog, layout);
    auto *subtitle = new QLabel("Load saved TX messages into your workspace.", &dialog);
    subtitle->setObjectName("secondaryText");
    subtitle->setWordWrap(true);
    layout->addWidget(subtitle);

    auto *fileCard = new QFrame(&dialog);
    fileCard->setObjectName("profileFileCard");
    auto *fileLayout = new QVBoxLayout(fileCard);
    fileLayout->setContentsMargins(16, 16, 16, 16);
    fileLayout->setSpacing(12);
    auto *fileHeading = new QHBoxLayout;
    fileHeading->setSpacing(12);
    fileHeading->addWidget(new ProfileDocumentIcon(fileCard));
    auto *caption = new QLabel("Profile file", fileCard);
    caption->setObjectName("profileSectionTitle");
    fileHeading->addWidget(caption, 1);
    auto *extension = new QLabel(".mavprofile", fileCard);
    extension->setObjectName("profileFileType");
    fileHeading->addWidget(extension, 0, Qt::AlignVCenter);
    fileLayout->addLayout(fileHeading);
    auto *fileRow = new QHBoxLayout;
    fileRow->setSpacing(8);
    auto *path = new WorkbenchLineEdit(fileCard);
    path->setObjectName("profilePath");
    path->setAccessibleName("Profile file");
    path->setPlaceholderText("Select a profile");
    path->setMinimumWidth(0);
    path->setMinimumHeight(36);
    caption->setBuddy(path);
    fileRow->addWidget(path, 1);
    auto *browse = new QPushButton("Browse…", fileCard);
    browse->setObjectName("browseProfile");
    browse->setMinimumHeight(36);
    browse->setMinimumWidth(96);
    fileRow->addWidget(browse);
    fileLayout->addLayout(fileRow);
    layout->addWidget(fileCard);

    auto *modeCard = new QFrame(&dialog);
    modeCard->setObjectName("profileModeCard");
    auto *modeLayout = new QVBoxLayout(modeCard);
    modeLayout->setContentsMargins(16, 14, 16, 14);
    modeLayout->setSpacing(10);
    auto *modeHeading = new QHBoxLayout;
    auto *modeTitle = new QLabel("Import mode", modeCard);
    modeTitle->setObjectName("profileSectionTitle");
    modeHeading->addWidget(modeTitle, 1);
    auto *modeBadge = new QLabel("Replace", modeCard);
    modeBadge->setObjectName("profileModeBadge");
    modeBadge->setAlignment(Qt::AlignCenter);
    modeHeading->addWidget(modeBadge);
    modeLayout->addLayout(modeHeading);
    auto *append = new WorkbenchCheckBox("Append", modeCard);
    append->setObjectName("appendProfile");
    append->setChecked(false);
    append->setAccessibleName("Append imported TX messages");
    modeLayout->addWidget(append);
    auto *description = new QLabel("Current TX messages will be replaced.", modeCard);
    description->setObjectName("profileHint");
    description->setWordWrap(true);
    modeLayout->addWidget(description);
    layout->addWidget(modeCard);

    auto *error = new QLabel(&dialog);
    error->setObjectName("profileError");
    error->setWordWrap(true);
    error->setTextFormat(Qt::PlainText);
    error->setAccessibleName("Profile import error");
    error->hide();
    layout->addWidget(error);
    layout->addStretch(1);
    auto *divider = new QFrame(&dialog);
    divider->setObjectName("profileFooterDivider");
    divider->setFixedHeight(1);
    layout->addWidget(divider);
    auto *footer = new QHBoxLayout;
    footer->setSpacing(16);
    auto *safetyHint = new QLabel("Imported messages always start stopped.", &dialog);
    safetyHint->setObjectName("profileSafetyHint");
    safetyHint->setWordWrap(true);
    footer->addWidget(safetyHint, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    buttons->setLayoutDirection(Qt::RightToLeft);
    auto *confirm = buttons->addButton("Import", QDialogButtonBox::AcceptRole);
    confirm->setObjectName("confirmImportProfile");
    confirm->setProperty("role", "primary");
    confirm->setMinimumSize(96, 36);
    buttons->button(QDialogButtonBox::Cancel)->setMinimumHeight(36);
    confirm->setEnabled(false);
    footer->addWidget(buttons);
    layout->addLayout(footer);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(path, &QLineEdit::textChanged, &dialog, [confirm, error](const QString &text)
    {
        confirm->setEnabled(!text.trimmed().isEmpty());
        error->hide();
    });
    connect(append, &QCheckBox::toggled, &dialog, [description, modeBadge](bool enabled)
    {
        modeBadge->setText(enabled ? "Append" : "Replace");
        description->setText(enabled ? "Current TX messages will be kept. New messages are added at the end."
                                     : "Current TX messages will be replaced.");
    });
    connect(browse, &QPushButton::clicked, &dialog, [&dialog, path]()
    {
        const QString fileName = QFileDialog::getOpenFileName(&dialog, "Import Profile", QString(),
                                                            "MAVLinkLab profiles (*.mavprofile)");
        if (!fileName.isEmpty())
        {
            path->setText(fileName);
        }
    });
    connect(worker, &SerialWorker::profileImported, &dialog, [&dialog](int, bool, quint64)
    {
        dialog.accept();
    });
    connect(worker, &SerialWorker::profileImportFailed, &dialog, [&, error](const QString &reason)
    {
        dialog.applying = false;
        confirm->setText("Import");
        error->setText(reason);
        error->show();
        path->setEnabled(true);
        browse->setEnabled(true);
        append->setEnabled(true);
        buttons->setEnabled(true);
        confirm->setEnabled(!path->text().trimmed().isEmpty());
    });
    connect(confirm, &QPushButton::clicked, &dialog, [&, error]()
    {
        const QString fileName = path->text().trimmed();
        if (!QFileInfo(fileName).isFile())
        {
            error->setText("Select an existing regular profile file.");
            error->show();
            return;
        }
        QFile file(fileName);
        if (!file.open(QIODevice::ReadOnly))
        {
            error->setText("Could not open profile: " + file.errorString());
            error->show();
            return;
        }
        const QByteArray data = file.read(Profile::MaxFileSize + 1);
        if (file.error() != QFileDevice::NoError || data.size() > Profile::MaxFileSize)
        {
            error->setText("Could not read profile or file exceeds the 1 MiB limit.");
            error->show();
            return;
        }
        error->hide();
        dialog.applying = true;
        confirm->setText("Importing…");
        profileBusy = true;
        updateActions();
        path->setEnabled(false);
        browse->setEnabled(false);
        append->setEnabled(false);
        buttons->setEnabled(false);
        txUndoStack.clear();
        lastDeletedTxEntry = Mav::TxEntry{};
        lastDeletedTxRowIndex = -1;
        if (txUndoToast)
        {
            txUndoToast->dismiss();
        }
        QMetaObject::invokeMethod(worker, "importProfile", Qt::QueuedConnection,
                                  Q_ARG(QByteArray, data), Q_ARG(bool, append->isChecked()),
                                  Q_ARG(quint64, nextKey));
    });
    dialog.exec();
}

/** @brief Export an authoritative worker snapshot using an atomic file replacement. */
void MainWindow::exportProfile()
{
    if (profileBusy)
    {
        return;
    }
    QString path = QFileDialog::getSaveFileName(this, "Export Profile", "TX messages.mavprofile",
                                               "MAVLinkLab profiles (*.mavprofile)");
    if (path.isEmpty())
    {
        return;
    }
    if (!path.endsWith(".mavprofile", Qt::CaseInsensitive))
    {
        path += ".mavprofile";
        if (QFileInfo::exists(path) && QMessageBox::question(this, "Replace Profile",
                "The destination already exists. Replace it?", QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No) != QMessageBox::Yes)
        {
            return;
        }
    }
    profileExportPath = path;
    profileBusy = true;
    updateActions();
    QMetaObject::invokeMethod(worker, "exportProfile", Qt::QueuedConnection);
}

/**
 * @brief Open a URL in the system browser quietly, suppressing external browser stderr logs.
 * @param url Target URL to navigate to.
 */
static void openUrlQuietly(const QString &url)
{
#if defined(Q_OS_LINUX)
    // Run xdg-open in detached mode redirecting stdout/stderr to /dev/null
    // to suppress noisy Chromium zygote/broken-pipe logs on Linux terminals.
    QProcess process;
    process.setProgram(QStringLiteral("xdg-open"));
    process.setArguments({url});
    process.setStandardOutputFile(QProcess::nullDevice());
    process.setStandardErrorFile(QProcess::nullDevice());
    if (process.startDetached())
    {
        return;
    }
#endif
    QDesktopServices::openUrl(QUrl(url));
}

/**
 * @brief Display the Check for Updates dialog and query GitHub latest release.
 */
void MainWindow::checkForUpdates()
{
    QDialog dialog(this);
    dialog.setObjectName("updateDialog");
    dialog.setWindowTitle("Check for Updates");
    dialog.setWindowIcon(appIcon());
    dialog.setMinimumWidth(440);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(14);
    layout = addDialogTitleBar(&dialog, layout);

    auto *headerRow = new QHBoxLayout;
    headerRow->setSpacing(16);
    headerRow->setContentsMargins(0, 4, 0, 6);

    auto *logoLabel = new QLabel(&dialog);
    logoLabel->setObjectName("updateLogo");
    logoLabel->setPixmap(QPixmap(":/icons/mavlink-lab-64.png"));
    logoLabel->setFixedSize(64, 64);
    headerRow->addWidget(logoLabel, 0, Qt::AlignVCenter);

    auto *titleCol = new QVBoxLayout;
    titleCol->setSpacing(4);
    titleCol->setAlignment(Qt::AlignVCenter);
    auto *appName = new QLabel("MAVLinkLab", &dialog);
    appName->setObjectName("updateAppName");
    appName->setStyleSheet("font-size: 20px; font-weight: 700;");
    auto *currentVersionLabel = new QLabel(QString("Current Version: v%1").arg(MAVLINK_LAB_VERSION), &dialog);
    currentVersionLabel->setObjectName("currentVersionLabel");
    titleCol->addWidget(appName);
    titleCol->addWidget(currentVersionLabel);
    headerRow->addLayout(titleCol, 1);

    layout->addLayout(headerRow);

    auto *statusLabel = new QLabel("Connecting to GitHub to check for updates…", &dialog);
    statusLabel->setObjectName("updateStatusLabel");
    statusLabel->setWordWrap(true);
    layout->addWidget(statusLabel);

    auto *buttonLayout = new QHBoxLayout;
    buttonLayout->addStretch(1);

    auto *updateButton = new QPushButton("Update", &dialog);
    updateButton->setObjectName("btnUpdate");
    updateButton->setVisible(false);
    buttonLayout->addWidget(updateButton);

    auto *closeButton = new QPushButton("Close", &dialog);
    closeButton->setObjectName("btnCloseUpdate");
    connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    buttonLayout->addWidget(closeButton);

    layout->addLayout(buttonLayout);

    QString releasePageUrl = QString::fromLatin1(Version::kReleasesPageUrl);
    connect(updateButton, &QPushButton::clicked, &dialog, [&releasePageUrl, &dialog]()
    {
        openUrlQuietly(releasePageUrl);
        dialog.accept();
    });

    auto *fetchProcess = new QProcess(&dialog);
    connect(fetchProcess, &QProcess::errorOccurred, &dialog, [statusLabel](QProcess::ProcessError)
    {
        statusLabel->setText("Failed to start network request (curl is required).");
    });

    connect(fetchProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), &dialog,
            [fetchProcess, statusLabel, updateButton, &releasePageUrl](int exitCode, QProcess::ExitStatus exitStatus)
    {
        if (exitStatus == QProcess::NormalExit && exitCode == 0)
        {
            const QByteArray payload = fetchProcess->readAllStandardOutput();
            const QJsonDocument doc = QJsonDocument::fromJson(payload);
            if (doc.isObject())
            {
                const QJsonObject obj = doc.object();
                const QString latestTag = obj.value(QStringLiteral("tag_name")).toString().trimmed();
                const QString htmlUrl = obj.value(QStringLiteral("html_url")).toString().trimmed();

                if (!latestTag.isEmpty())
                {
                    if (Version::isNewerVersion(latestTag, QStringLiteral(MAVLINK_LAB_VERSION)))
                    {
                        statusLabel->setText(QString("A new version (%1) is available! Click Update to open release page.").arg(latestTag));
                        if (!htmlUrl.isEmpty())
                        {
                            releasePageUrl = htmlUrl;
                        }
                        updateButton->setVisible(true);
                    }
                    else
                    {
                        statusLabel->setText("You are using the latest version.");
                        updateButton->setVisible(false);
                    }
                }
                else
                {
                    statusLabel->setText("Unable to parse version information from GitHub.");
                    updateButton->setVisible(false);
                }
            }
            else
            {
                statusLabel->setText("Invalid response received from GitHub.");
                updateButton->setVisible(false);
            }
        }
        else
        {
            statusLabel->setText("Failed to fetch release information. Please check your network connection.");
            updateButton->setVisible(false);
        }
    });

    const QStringList args = {
        "-s",
        "--connect-timeout", "10",
        "-H", "User-Agent: MAVLinkLab",
        "-H", "Accept: application/vnd.github.v3+json",
        QString::fromLatin1(Version::kLatestReleaseApiUrl)
    };
    fetchProcess->start("curl", args);

    dialog.exec();

    if (fetchProcess->state() != QProcess::NotRunning)
    {
        fetchProcess->disconnect();
        fetchProcess->kill();
        fetchProcess->waitForFinished(500);
    }
}

/**
 * @brief Display reference dialog showing available keyboard shortcuts.
 */
void MainWindow::showKeyboardShortcuts()
{
    QDialog dialog(this);
    dialog.setObjectName("keyboardShortcutsDialog");
    dialog.setWindowTitle("Keyboard Shortcuts");
    dialog.setWindowIcon(appIcon());
    dialog.setFixedWidth(440);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(14);
    layout = addDialogTitleBar(&dialog, layout);
    dialog.setSizeGripEnabled(false);
    if (dialog.layout())
    {
        dialog.layout()->setSizeConstraint(QLayout::SetFixedSize);
    }

    auto *card = new QFrame(&dialog);
    card->setObjectName("shortcutsCard");
    card->setFixedWidth(400);
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(16, 8, 16, 8);
    cardLayout->setSpacing(0);

    struct ShortcutRow
    {
        QString description;
        QStringList tokens;
    };

    const std::vector<ShortcutRow> rows = {
        {"Toggle transmission", {"Space"}},
        {"Duplicate message", {"Ctrl", "+", "D"}},
        {"Delete message", {"Delete", "/", "Ctrl", "+", "X"}},
        {"Undo delete", {"Ctrl", "+", "Z"}},
        {"Move message up", {"Ctrl", "+", "↑"}},
        {"Move message down", {"Ctrl", "+", "↓"}},
        {"Cancel drag & drop", {"Esc"}},
        {"Keyboard shortcuts", {"F1"}}
    };

    for (size_t i = 0; i < rows.size(); ++i)
    {
        const auto &row = rows[i];
        if (i > 0)
        {
            auto *divider = new QFrame(card);
            divider->setObjectName("shortcutRowDivider");
            divider->setFixedHeight(1);
            cardLayout->addWidget(divider);
        }

        auto *rowWidget = new QWidget(card);
        auto *rowLayout = new QHBoxLayout(rowWidget);
        rowLayout->setContentsMargins(0, 6, 0, 6);
        rowLayout->setSpacing(12);

        auto *descLabel = new QLabel(row.description, rowWidget);
        descLabel->setObjectName("shortcutDesc");
        rowLayout->addWidget(descLabel, 1);

        auto *badgeRow = new QHBoxLayout;
        badgeRow->setContentsMargins(0, 0, 0, 0);
        badgeRow->setSpacing(4);

        for (const auto &token : row.tokens)
        {
            if (token == "+" || token == "/")
            {
                auto *symbol = new QLabel(token, rowWidget);
                symbol->setObjectName("shortcutSymbol");
                badgeRow->addWidget(symbol);
            }
            else
            {
                auto *badge = new QLabel(token, rowWidget);
                badge->setObjectName("keycapBadge");
                badge->setAlignment(Qt::AlignCenter);
                badgeRow->addWidget(badge);
            }
        }

        rowLayout->addLayout(badgeRow);
        cardLayout->addWidget(rowWidget);
    }

    layout->addWidget(card);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
    auto *closeBtn = buttonBox->button(QDialogButtonBox::Ok);
    closeBtn->setText("Close");
    closeBtn->setObjectName("btnCloseShortcuts");
    closeBtn->setProperty("role", "primary");
    closeBtn->setFixedHeight(34);
    closeBtn->setMinimumWidth(80);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    layout->addWidget(buttonBox);

    dialog.exec();
}


/** @brief Restore configured TX messages from workspace storage. */
void MainWindow::restoreTxMessages()
{
    QSettings preferences("MAVLinkSimulator", "Preferences");
    const bool restoreTx = preferences.value("workspace/restoreTx", false).toBool();
    if (!restoreTx)
    {
        return;
    }
    QSettings workspace("MAVLinkSimulator", "Workspace");
    const int count = workspace.beginReadArray("txMessages");
    for (int i = 0; i < count; ++i)
    {
        workspace.setArrayIndex(i);
        Mav::TxEntry entry;
        entry.key = nextKey++;
        entry.id = workspace.value("id").toUInt();
        entry.sysid = static_cast<uint8_t>(workspace.value("sysid", 1).toUInt());
        entry.compid = static_cast<uint8_t>(workspace.value("compid", 1).toUInt());
        entry.version1 = workspace.value("version1", false).toBool();
        entry.hz = workspace.value("hz", 1.0).toDouble();
        const QByteArray payloadBytes = workspace.value("payload").toByteArray();
        const auto *bytesPtr = reinterpret_cast<const uint8_t *>(payloadBytes.constData());
        entry.payload.assign(bytesPtr, bytesPtr + payloadBytes.size());
        entry.active = false;
        entry.deadline = 0;
        entry.count = 0;
        entry.error.clear();
        if (Mav::validTx(entry))
        {
            QMetaObject::invokeMethod(worker, "saveEntry", Qt::QueuedConnection, Q_ARG(Mav::TxEntry, entry));
        }
    }
    workspace.endArray();
}

/** @brief Save configured TX messages to workspace storage. */
void MainWindow::saveTxMessages()
{
    QSettings workspace("MAVLinkSimulator", "Workspace");
    if (!latest)
    {
        return;
    }
    workspace.beginWriteArray("txMessages", int(latest->tx.size()));
    for (int i = 0; i < int(latest->tx.size()); ++i)
    {
        workspace.setArrayIndex(i);
        const auto &entry = latest->tx[i].entry;
        workspace.setValue("id", entry.id);
        workspace.setValue("sysid", entry.sysid);
        workspace.setValue("compid", entry.compid);
        workspace.setValue("version1", entry.version1);
        workspace.setValue("hz", entry.hz);
        const QByteArray payloadBytes(reinterpret_cast<const char *>(entry.payload.data()), int(entry.payload.size()));
        workspace.setValue("payload", payloadBytes);
    }
    workspace.endArray();
    workspace.sync();
}



void MainWindow::setStatusBarRateUnit(int unit)
{
    if (statsRateUnitBox && unit >= 0 && unit < statsRateUnitBox->count())
    {
        const QSignalBlocker blocker(statsRateUnitBox);
        statsRateUnitBox->setCurrentIndex(unit);
    }
    if (metricsWidget)
    {
        metricsWidget->setRateUnit(unit);
    }
}

