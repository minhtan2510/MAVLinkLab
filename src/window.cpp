/**
 * @file window.cpp
 * @brief MainWindow core lifecycle, central layout, toolbar actions, and event filter.
 */

#include "window.h"
#include "window/window_private.h"
#include "version.h"
#include "plotter.h"
#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QStatusBar>
#include <QLabel>
#include <QPushButton>
#include <QAction>
#include <QMenuBar>
#include <QMenu>
#include <QToolBar>
#include <QCloseEvent>
#include <QResizeEvent>
#include <QKeyEvent>
#include <QEvent>
#include <QIcon>
#include <QSettings>

/**
 * @brief Compute the visual bounding rectangle for a TX message card inside a table cell.
 * @param cellRect The raw geometry of the table cell (column 1).
 * @param animDy Optional vertical animation translation offset in pixels.
 * @return Padded card rectangle with 6px left margin and 8px right breathing room.
 */
QRect MainWindow::txCardRect(const QRect &cellRect, int animDy)
{
    return cellRect.adjusted(6, 5 + animDy, -8, -5 + animDy);
}

/**
 * @brief Construct the multi-resolution application icon from bundled resources.
 * @return QIcon containing standard raster sizes from 16x16 to 512x512.
 */
QIcon MainWindow::appIcon()
{
    Q_INIT_RESOURCE(resources);
    QIcon icon;
    icon.addFile(":/icons/mavlink-lab-16.png", QSize(16, 16));
    icon.addFile(":/icons/mavlink-lab-24.png", QSize(24, 24));
    icon.addFile(":/icons/mavlink-lab-32.png", QSize(32, 32));
    icon.addFile(":/icons/mavlink-lab-48.png", QSize(48, 48));
    icon.addFile(":/icons/mavlink-lab-64.png", QSize(64, 64));
    icon.addFile(":/icons/mavlink-lab-128.png", QSize(128, 128));
    icon.addFile(":/icons/mavlink-lab-256.png", QSize(256, 256));
    icon.addFile(":/icons/mavlink-lab-512.png", QSize(512, 512));
    return icon;
}


MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle("MAVLinkLab");
    setWindowIcon(appIcon());
    setMinimumSize(1000, 720);
    resize(1440, 900);
    auto *central = new QWidget;
    setCentralWidget(central);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(20, 10, 20, 8);
    root->setSpacing(16);
    auto *heading = new QHBoxLayout;
    auto *title = new QLabel("MAVLinkLab");
    title->setObjectName("heading");
    title->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    title->setFixedHeight(40);
    heading->addWidget(title, 0, Qt::AlignTop);
    heading->setSpacing(12);

    settingsDialog = new QDialog(this);
    settingsDialog->setObjectName("settingsDialog");
    settingsDialog->setWindowTitle("Settings");
    settingsDialog->setModal(true);
    settingsDialog->setMinimumWidth(440);
    auto *settingsDialogLayout = new QVBoxLayout(settingsDialog);
    settingsDialogLayout->setContentsMargins(20, 20, 20, 16);
    settingsDialogLayout->setSpacing(16);
    settingsDialogLayout = addDialogTitleBar(settingsDialog, settingsDialogLayout);

    auto *appearanceCard = new QFrame(settingsDialog);
    appearanceCard->setObjectName("settingsCard");
    auto *appearanceLayout = new QVBoxLayout(appearanceCard);
    appearanceLayout->setContentsMargins(14, 12, 14, 12);
    appearanceLayout->setSpacing(8);

    auto *groupHeader = new QLabel("Appearance", appearanceCard);
    groupHeader->setObjectName("settingsGroupTitle");
    appearanceLayout->addWidget(groupHeader);

    auto *themeRow = new QHBoxLayout;
    themeRow->setContentsMargins(0, 4, 0, 0);
    auto *themeLabel = new QLabel("Theme:", appearanceCard);
    themeLabel->setObjectName("secondaryText");
    themeRow->addWidget(themeLabel);
    themeRow->addStretch();

    themeBox = new WorkbenchComboBox(appearanceCard);
    themeBox->setObjectName("theme");
    themeBox->addItems({"System", "Light", "Dark"});
    themeBox->setAccessibleName("Application theme");
    themeBox->setFixedWidth(160);
    themeBox->setFixedHeight(32);
    themeRow->addWidget(themeBox);
    appearanceLayout->addLayout(themeRow);

    auto *debugBarGroup = new QVBoxLayout;
    debugBarGroup->setContentsMargins(0, 4, 0, 0);

    showDebugBarBox = new WorkbenchCheckBox("Show stats bar", appearanceCard);
    showDebugBarBox->setObjectName("showDebugBar");
    showDebugBarBox->setChecked(true);
    showDebugBarBox->setAccessibleName("Show stats bar");
    debugBarGroup->addWidget(showDebugBarBox);

    appearanceLayout->addLayout(debugBarGroup);

    auto *statsRateUnitRow = new QHBoxLayout;
    statsRateUnitRow->setContentsMargins(0, 4, 0, 0);
    statsRateUnitLabel = new QLabel("Stats Bar Unit:", appearanceCard);
    statsRateUnitLabel->setObjectName("secondaryText");
    statsRateUnitRow->addWidget(statsRateUnitLabel);
    statsRateUnitRow->addStretch();

    statsRateUnitBox = new WorkbenchComboBox(appearanceCard);
    statsRateUnitBox->setObjectName("statsRateUnit");
    statsRateUnitBox->setAccessibleName("Stats Bar Unit");
    statsRateUnitBox->addItems({"B/s", "KB/s", "MB/s"});
    statsRateUnitBox->setFixedHeight(32);
    statsRateUnitBox->setFixedWidth(160);
    statsRateUnitBox->setCurrentIndex(1);
    statsRateUnitRow->addWidget(statsRateUnitBox);
    appearanceLayout->addLayout(statsRateUnitRow);

    settingsDialogLayout->addWidget(appearanceCard);

    auto *mavlinkCard = new QFrame(settingsDialog);
    mavlinkCard->setObjectName("settingsCard");
    auto *mavlinkLayout = new QVBoxLayout(mavlinkCard);
    mavlinkLayout->setContentsMargins(14, 12, 14, 12);
    mavlinkLayout->setSpacing(8);

    auto *mavlinkHeader = new QLabel("MAVLink Defaults", mavlinkCard);
    mavlinkHeader->setObjectName("settingsGroupTitle");
    mavlinkLayout->addWidget(mavlinkHeader);

    const auto addSettingsRow = [](QVBoxLayout *cardLayout, const QString &label, QWidget *control)
    {
        auto *row = new QHBoxLayout;
        row->setContentsMargins(0, 4, 0, 0);
        auto *caption = new QLabel(label);
        caption->setObjectName("secondaryText");
        row->addWidget(caption);
        row->addStretch();
        control->setFixedWidth(160);
        row->addWidget(control);
        cardLayout->addLayout(row);
    };

    auto *serialCard = new QFrame(settingsDialog);
    serialCard->setObjectName("settingsCard");
    auto *serialLayout = new QVBoxLayout(serialCard);
    serialLayout->setContentsMargins(14, 12, 14, 12);
    serialLayout->setSpacing(8);

    auto *serialHeader = new QLabel("Serial Management", serialCard);
    serialHeader->setObjectName("settingsGroupTitle");
    serialLayout->addWidget(serialHeader);

    auto *reconnectGroup = new QVBoxLayout;
    reconnectGroup->setContentsMargins(0, 4, 0, 0);
    reconnectGroup->setSpacing(3);

    autoReconnectBox = new WorkbenchCheckBox("Auto-reconnect on unexpected disconnect", serialCard);
    autoReconnectBox->setObjectName("autoReconnect");
    autoReconnectBox->setChecked(false);
    autoReconnectBox->setAccessibleName("Auto-reconnect on unexpected disconnect");
    reconnectGroup->addWidget(autoReconnectBox);

    serialLayout->addLayout(reconnectGroup);

    auto *controlLinesGroup = new QVBoxLayout;
    controlLinesGroup->setContentsMargins(0, 4, 0, 0);
    controlLinesGroup->setSpacing(3);
    deassertControlLinesBox = new WorkbenchCheckBox("Deassert DTR/RTS on connect", serialCard);
    deassertControlLinesBox->setObjectName("deassertControlLines");
    deassertControlLinesBox->setChecked(true);
    deassertControlLinesBox->setAccessibleName("Deassert DTR/RTS on connect");
    controlLinesGroup->addWidget(deassertControlLinesBox);
    serialLayout->addLayout(controlLinesGroup);

    settingsDialogLayout->addWidget(serialCard);

    defaultSysIdBox = new WorkbenchSpinBox<QSpinBox>(mavlinkCard);
    defaultSysIdBox->setObjectName("defaultSysId");
    defaultSysIdBox->setAccessibleName("Default System ID");
    defaultSysIdBox->setRange(1, 255);
    defaultSysIdBox->setValue(1);
    addSettingsRow(mavlinkLayout, "Default System ID:", defaultSysIdBox);

    defaultCompIdBox = new WorkbenchSpinBox<QSpinBox>(mavlinkCard);
    defaultCompIdBox->setObjectName("defaultCompId");
    defaultCompIdBox->setAccessibleName("Default Component ID");
    defaultCompIdBox->setRange(0, 255);
    defaultCompIdBox->setValue(1);
    addSettingsRow(mavlinkLayout, "Default Component ID:", defaultCompIdBox);

    defaultVersionBox = new WorkbenchComboBox(mavlinkCard);
    defaultVersionBox->setObjectName("defaultVersion");
    defaultVersionBox->setAccessibleName("Default MAVLink Version");
    defaultVersionBox->addItems({"MAVLink v2", "MAVLink v1"});
    defaultVersionBox->setFixedHeight(32);
    addSettingsRow(mavlinkLayout, "Default Version:", defaultVersionBox);

    settingsDialogLayout->addWidget(mavlinkCard);

    auto *workspaceCard = new QFrame(settingsDialog);
    workspaceCard->setObjectName("settingsCard");
    auto *workspaceLayout = new QVBoxLayout(workspaceCard);
    workspaceLayout->setContentsMargins(14, 12, 14, 12);
    workspaceLayout->setSpacing(10);

    auto *workspaceHeader = new QLabel("Workspace & Session", workspaceCard);
    workspaceHeader->setObjectName("settingsGroupTitle");
    workspaceLayout->addWidget(workspaceHeader);

    auto *restoreGroup = new QVBoxLayout;
    restoreGroup->setContentsMargins(0, 4, 0, 0);
    restoreGroup->setSpacing(3);

    restoreTxBox = new WorkbenchCheckBox("Restore TX messages on startup", workspaceCard);
    restoreTxBox->setObjectName("restoreTxMessages");
    restoreTxBox->setChecked(false);
    restoreTxBox->setAccessibleName("Restore TX messages on startup");
    restoreGroup->addWidget(restoreTxBox);

    singleActiveStreamBox = new WorkbenchCheckBox("Prevent duplicate active TX streams", workspaceCard);
    singleActiveStreamBox->setObjectName("singleActiveStream");
    singleActiveStreamBox->setChecked(true);
    singleActiveStreamBox->setAccessibleName("Prevent duplicate active TX streams");
    singleActiveStreamBox->setToolTip("Ensure only one message per (SysID, CompID, MsgID) stream transmits at a time");
    restoreGroup->addWidget(singleActiveStreamBox);

    workspaceLayout->addLayout(restoreGroup);

    auto *resetRow = new QHBoxLayout;
    resetRow->setContentsMargins(0, 6, 0, 0);
    auto *resetDesc = new QLabel("Window layout & panels:", workspaceCard);
    resetDesc->setObjectName("secondaryText");
    resetRow->addWidget(resetDesc);
    resetRow->addStretch();

    auto *resetLayoutButton = new QPushButton("Reset Layout", workspaceCard);
    resetLayoutButton->setObjectName("resetLayout");
    resetLayoutButton->setFixedWidth(130);
    resetRow->addWidget(resetLayoutButton);
    workspaceLayout->addLayout(resetRow);

    settingsDialogLayout->addWidget(workspaceCard);
    settingsDialogLayout->addStretch();

    auto *footerRow = new QHBoxLayout;
    footerRow->setContentsMargins(0, 0, 0, 0);

    auto *restoreDefaultsButton = new QPushButton("Restore defaults", settingsDialog);
    restoreDefaultsButton->setObjectName("restoreDefaults");
    restoreDefaultsButton->setProperty("role", "secondary");
    restoreDefaultsButton->setAutoDefault(false);
    restoreDefaultsButton->setToolTip("Restore all preferences to default values");
    connect(restoreDefaultsButton, &QPushButton::clicked, this, &MainWindow::restoreDefaultSettings);
    footerRow->addWidget(restoreDefaultsButton, 0, Qt::AlignLeft);

    footerRow->addStretch();

    auto *settingsButtonBox = new QDialogButtonBox(settingsDialog);
    settingsButtonBox->setObjectName("settingsButtonBox");
    auto *closeButton = settingsButtonBox->addButton("Close", QDialogButtonBox::RejectRole);
    closeButton->setObjectName("closeSettings");
    closeButton->setIcon(QIcon());
    connect(settingsButtonBox, &QDialogButtonBox::rejected, settingsDialog, &QDialog::accept);
    footerRow->addWidget(settingsButtonBox, 0, Qt::AlignRight);

    settingsDialogLayout->addLayout(footerRow);

    auto *appMenuBar = menuBar();
    appMenuBar->setObjectName("appMenuBar");

    auto *fileMenu = new WorkbenchMenu("File", appMenuBar);
    fileMenu->setObjectName("fileMenu");
    appMenuBar->addMenu(fileMenu);
    importProfileAction = fileMenu->addAction("Import Profile…", this, &MainWindow::importProfile);
    importProfileAction->setObjectName("actionImportProfile");
    exportProfileAction = fileMenu->addAction("Export Profile…", this, &MainWindow::exportProfile);
    exportProfileAction->setObjectName("actionExportProfile");
    fileMenu->addSeparator();
    auto *exitAction = fileMenu->addAction("Exit", this, &QWidget::close, QKeySequence::Quit);
    exitAction->setObjectName("actionExit");

    auto *viewMenu = new WorkbenchMenu("View", appMenuBar);
    viewMenu->setObjectName("viewMenu");
    appMenuBar->addMenu(viewMenu);

    auto *toolMenu = new WorkbenchMenu("Tool", appMenuBar);
    toolMenu->setObjectName("toolMenu");
    appMenuBar->addMenu(toolMenu);
    auto *settingsAction = toolMenu->addAction("Settings...", settingsDialog, &QDialog::exec, QKeySequence::Preferences);
    settingsAction->setObjectName("actionSettings");

    auto *helpMenu = new WorkbenchMenu("Help", appMenuBar);
    helpMenu->setObjectName("helpMenu");
    appMenuBar->addMenu(helpMenu);
    keyboardShortcutsAction = helpMenu->addAction("Keyboard Shortcuts", this, &MainWindow::showKeyboardShortcuts, QKeySequence::HelpContents);
    keyboardShortcutsAction->setObjectName("actionKeyboardShortcuts");
    helpMenu->addSeparator();
    checkUpdatesAction = helpMenu->addAction("Check for update...", this, &MainWindow::checkForUpdates);
    checkUpdatesAction->setObjectName("actionCheckUpdates");
    helpMenu->addSeparator();
    auto *aboutAction = helpMenu->addAction("About MAVLinkLab", this, [this]()
    {
        QDialog dialog(this);
        dialog.setObjectName("aboutDialog");
        dialog.setWindowTitle("About MAVLinkLab");
        dialog.setWindowIcon(appIcon());
        auto *layout = new QVBoxLayout(&dialog);
        layout->setContentsMargins(20, 16, 20, 16);
        layout->setSpacing(12);
        layout = addDialogTitleBar(&dialog, layout);
        dialog.setMinimumWidth(440);

        QString mavlinkLib = QString("Wire v%1 · %2").arg(MAVLINK_WIRE_PROTOCOL_VERSION, MAVLINK_BUILD_DATE);
        QFile gitRef(".git/modules/third_party/mavlink/refs/heads/master");
        if (gitRef.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            const QString commit = QString::fromUtf8(gitRef.readLine()).trimmed().left(8);
            if (!commit.isEmpty())
            {
                mavlinkLib = QString("Wire v%1 (rev %2) · %3").arg(MAVLINK_WIRE_PROTOCOL_VERSION, commit, MAVLINK_BUILD_DATE);
            }
        }

        auto *headerRow = new QHBoxLayout;
        headerRow->setSpacing(16);
        headerRow->setContentsMargins(0, 0, 0, 0);

        auto *logoLabel = new QLabel(&dialog);
        logoLabel->setObjectName("aboutLogo");
        logoLabel->setPixmap(QPixmap(":/icons/mavlink-lab-64.png"));
        logoLabel->setFixedSize(64, 64);
        headerRow->addWidget(logoLabel, 0, Qt::AlignVCenter);

        auto *titleCol = new QVBoxLayout;
        titleCol->setSpacing(4);
        titleCol->setAlignment(Qt::AlignVCenter);

        auto *appName = new QLabel("MAVLinkLab", &dialog);
        appName->setObjectName("aboutAppName");
        appName->setStyleSheet("font-size: 20px; font-weight: 700;");

        auto *version = new QLabel(QString("Version %1").arg(MAVLINK_LAB_VERSION), &dialog);
        version->setObjectName("secondaryText");

        titleCol->addWidget(appName);
        titleCol->addWidget(version);

        headerRow->addLayout(titleCol, 1);
        layout->addLayout(headerRow);

        auto *details = new QLabel(
            QString(
                "<p style='margin: 0 0 8px 0; line-height: 140%;'>"
                "A desktop workbench for sending, receiving and inspecting MAVLink telemetry over serial UART.</p>"
                "<p style='margin: 0; line-height: 140%;'>"
                "<b>Author:</b> Tân Nguyễn &lt;tan.nguyen@gremsy.com&gt;<br>"
                "<b>Protocol:</b> MAVLink 1 &amp; 2 · Dialect: ardupilotmega<br>"
                "<b>Library:</b> %1<br>"
                "<b>Platform:</b> Linux · C++17 · Qt %2</p>")
                .arg(mavlinkLib, qVersion()), &dialog);
        details->setObjectName("aboutDetails");
        details->setWordWrap(true);
        details->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(details);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        layout->addWidget(buttons);
        dialog.exec();
    });
    aboutAction->setObjectName("actionAbout");
    for (auto *menu : {fileMenu, viewMenu, toolMenu, helpMenu})
    {
        configureTranslucentMenu(menu);
        menu->installEventFilter(this);
    }
    auto *uart = new QFrame;
    uart->setObjectName("connectionBar");
    auto *uartLayout = new QVBoxLayout(uart);
    uartLayout->setContentsMargins(0, 4, 0, 4);
    uartLayout->setSpacing(10);
    auto *uartRow = new QHBoxLayout;
    uartRow->setSpacing(4);
    /** @brief Group each serial control with a persistent label and keyboard buddy. */
    const auto addCombo = [](QHBoxLayout *row, const QString &label,
                             QComboBox *&combo, const QStringList &values)
    {
        auto *field = new QWidget;
        auto *fieldLayout = new QVBoxLayout(field);
        fieldLayout->setContentsMargins(0, 0, 0, 0);
        fieldLayout->setSpacing(6);
        auto *caption = new QLabel(label);
        caption->setObjectName("serialFieldLabel");
        combo = new WorkbenchComboBox;
        combo->setFixedHeight(32);
        combo->addItems(values);
        combo->setAccessibleName(label);
        caption->setBuddy(combo);
        fieldLayout->addWidget(caption);
        fieldLayout->addWidget(combo);
        row->addWidget(field);
        return field;
    };
    portBox = new WorkbenchComboBox;
    portBox->setFixedHeight(32);
    portBox->setFixedWidth(160);
    portBox->setEditable(false);
    portBox->setProperty("hideSelectionCheckmark", true);
    portBox->setObjectName("port");
    portBox->setAccessibleName("Serial port");
    uartRow->addWidget(portBox);

    auto *refreshPortsButton = button("", "refreshPorts", uartRow);
    refreshPortsButton->setFixedSize(32, 32);
    refreshPortsButton->setAccessibleName("Refresh serial ports");
    refreshPortsButton->setToolTip("Refresh available serial ports");

    uartRow->addSpacing(8);
    baudBox = new WorkbenchComboBox;
    baudBox->setFixedHeight(32);
    baudBox->setFixedWidth(140);
    baudBox->addItems({"9600", "19200", "38400", "57600", "115200", "230400", "460800", "921600"});
    baudBox->setObjectName("baudRate");
    baudBox->setEditable(true);
    baudBox->setCurrentText("115200");
    if (baudBox->lineEdit())
    {
        baudBox->lineEdit()->setPlaceholderText("Baud rate");
        baudBox->lineEdit()->setTextMargins(0, 0, 0, 0);
        baudBox->lineEdit()->setCursorPosition(0);
    }
    baudBox->setAccessibleName("Baud rate");
    uartRow->addWidget(baudBox);

    serialSummary = new QLabel;
    serialSummary->setObjectName("serialSummary");
    serialSummary->setFixedHeight(32);
    serialSummary->setAlignment(Qt::AlignCenter);
    serialSummary->setAccessibleName("Serial framing and flow control");
    auto *settings = button("", "serialSettings", uartRow);
    settings->setFixedSize(32, 32);
    settings->setCheckable(true);
    settings->setAccessibleName("Advanced UART settings");
    settings->setToolTip("UART settings");

    uartRow->addSpacing(8);
    connectionStatusPill = new ConnectionStatusWidget;
    connectionStatusPill->setObjectName("connectionStatusPill");
    uartRow->addWidget(connectionStatusPill);

    uartRow->addSpacing(20);
    auto *connectionCard = new QFrame;
    connectionCard->setObjectName("connectionCard");
    auto *cardLayout = new QVBoxLayout(connectionCard);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(4);
    auto *connectRow = new QHBoxLayout;
    connectRow->setContentsMargins(0, 0, 0, 0);
    connectButton = button("Connect", "connect", connectRow);
    connectButton->setProperty("role", "primary");
    connectButton->setFixedSize(116, 32);
    cardLayout->addLayout(connectRow);
    uartRow->addWidget(connectionCard, 0, Qt::AlignTop);
    for (int index = 0; index < uartRow->count() - 1; ++index)
    {
        if (auto *control = uartRow->itemAt(index)->widget())
        {
            uartRow->setAlignment(control, Qt::AlignTop);
        }
    }

    uartLayout->addLayout(uartRow);
    auto *serialMenu = new QMenu(this);
    serialMenu->setObjectName("serialMenu");
    configureTranslucentMenu(serialMenu);
    serialMenu->installEventFilter(this);
    auto *advanced = new QFrame;
    advanced->setObjectName("serialAdvanced");
    auto *advancedLayout = new QVBoxLayout(advanced);
    advancedLayout->setContentsMargins(12, 10, 12, 12);
    advancedLayout->setSpacing(8);
    auto *advancedHeader = new QHBoxLayout;
    auto *advancedTitle = new QLabel("Serial parameters");
    advancedTitle->setObjectName("serialFieldLabel");
    advancedHeader->addWidget(advancedTitle);
    advancedHeader->addWidget(serialSummary);
    advancedHeader->addStretch();
    advancedLayout->addLayout(advancedHeader);
    auto *firstRow = new QHBoxLayout;
    auto *secondRow = new QHBoxLayout;
    firstRow->setSpacing(16);
    secondRow->setSpacing(16);
    addCombo(firstRow, "Data bits", dataBox, {"8", "7", "6", "5"});
    addCombo(firstRow, "Parity", parityBox, {"None", "Even", "Odd", "Mark", "Space"});
    addCombo(secondRow, "Stop bits", stopBox, {"1", "2"});
    addCombo(secondRow, "Flow control", flowBox, {"None", "Hardware RTS/CTS", "Software XON/XOFF"});
    dataBox->setObjectName("dataBits");
    parityBox->setObjectName("parity");
    stopBox->setObjectName("stopBits");
    flowBox->setObjectName("flowControl");
    for (auto *row : {firstRow, secondRow})
    {
        row->setStretch(0, 1);
        row->setStretch(1, 1);
        advancedLayout->addLayout(row);
    }
    advanced->setMinimumWidth(460);
    auto *serialAction = new QWidgetAction(serialMenu);
    serialAction->setDefaultWidget(advanced);
    serialMenu->addAction(serialAction);
    connect(serialMenu, &QMenu::aboutToHide, settings, [settings]()
    {
        settings->setChecked(false);
    });
    connect(settings, &QPushButton::clicked, this, [this, settings, serialMenu](bool checked)
    {
        if (!checked)
        {
            serialMenu->hide();
            return;
        }
        serialMenu->ensurePolished();
        const int width = serialMenu->sizeHint().width();
        const QPoint edge = settings->mapToGlobal(QPoint(settings->width(), settings->height()));
        const int left = mapToGlobal(QPoint(0, 0)).x();
        const int right = mapToGlobal(QPoint(this->width(), 0)).x();
        serialMenu->popup(QPoint(qBound(left, edge.x() - width, right - width), edge.y() + 8));
        dataBox->setFocus(Qt::PopupFocusReason);
    });
    for (auto *combo : {dataBox, parityBox, stopBox, flowBox})
    {
        connect(combo, &QComboBox::currentIndexChanged, this, [this]()
        {
            updateSerialSummary();
            saveSettings();
        });
    }
    updateSerialSummary();
    uart->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    uart->setMaximumWidth(820);
    uart->setFixedHeight(uartLayout->sizeHint().height());
    auto *workspace = new QVBoxLayout;
    workspace->setSpacing(8);
    heading->addStretch();
    heading->addWidget(uart, 0, Qt::AlignRight | Qt::AlignTop);
    workspace->addLayout(heading);
    root->addLayout(workspace, 1);

    auto *dashboards = new QSplitter(Qt::Horizontal);
    dashboards->setObjectName("dashboards");
    dashboards->setChildrenCollapsible(false);
    dashboards->setHandleWidth(12);
    auto *txGroup = new QFrame;
    txGroup->setMinimumWidth(340);
    txGroup->setObjectName("dashboardPanel");
    auto *txLayout = new QVBoxLayout(txGroup);
    txLayout->setContentsMargins(16, 16, 16, 12);
    txLayout->setSpacing(12);
    auto *txHeading = new QGridLayout;
    txHeading->setContentsMargins(0, 0, 0, 0);
    txHeading->setHorizontalSpacing(8);
    txHeading->setVerticalSpacing(10);

    auto *txTitle = new QLabel("TX Dashboard");
    txTitle->setObjectName("panelTitle");
    txHeading->addWidget(txTitle, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);

    txSummary = new TxSummaryWidget;
    txSummary->setObjectName("txSummary");
    txHeading->addWidget(txSummary, 0, 1, 1, 3, Qt::AlignLeft | Qt::AlignVCenter);

    auto *batchActions = new QHBoxLayout;
    batchActions->setContentsMargins(0, 0, 0, 0);
    batchActions->setSpacing(4);

    runAllButton = button("", "runAll", batchActions);
    runAllButton->setProperty("role", "success");
    runAllButton->setFixedSize(28, 28);
    runAllButton->setToolTip("Run all");
    runAllButton->setAccessibleName("Run all messages");

    stopAllButton = button("", "stopAll", batchActions);
    stopAllButton->setProperty("role", "danger");
    stopAllButton->setFixedSize(28, 28);
    stopAllButton->setToolTip("Stop all");
    stopAllButton->setAccessibleName("Stop all messages");

    batchActions->addStretch();

    deleteAllButton = button("", "deleteAll", batchActions);
    deleteAllButton->setProperty("role", "danger");
    deleteAllButton->setFixedSize(28, 28);
    deleteAllButton->setToolTip("Delete all");
    deleteAllButton->setAccessibleName("Delete all messages");

    txHeading->addLayout(batchActions, 1, 0);

    txHeading->setColumnStretch(2, 1);

    auto *addButton = new QPushButton("Add message");
    addButton->setObjectName("addMessage");
    addButton->setProperty("role", "primary");
    txHeading->addWidget(addButton, 1, 3, Qt::AlignRight | Qt::AlignVCenter);

    txLayout->addLayout(txHeading);

    trashDropCard = new TrashDropCard(txGroup);
    trashDropCard->hide();
    auto *txDivider = new QFrame;
    txDivider->setObjectName("txDivider");
    txDivider->setFixedHeight(1);
    txLayout->addWidget(txDivider);
    txTable = table({"ID", "Message", "Sys / Comp", "Target Hz", "Actual Hz", "Queued", "Status"}, true);
    txTable->setObjectName("txTable");
    txTable->setItemDelegateForColumn(1, new TxCardDelegate(txTable));
    txTable->horizontalHeader()->hide();
    txTable->horizontalHeader()->setStretchLastSection(false);
    txTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    for (int column : {0, 2, 3, 4, 5, 6})
    {
        txTable->hideColumn(column);
    }
    txTable->verticalHeader()->setDefaultSectionSize(74);
    txTable->setAlternatingRowColors(false);
    txTable->setMouseTracking(true);
    txTable->viewport()->setMouseTracking(true);
    txTable->viewport()->installEventFilter(this);
    txTable->installEventFilter(this);
    dragAutoScrollTimer = new QTimer(this);
    connect(dragAutoScrollTimer, &QTimer::timeout, this, [this]()
    {
        if (!isDraggingTx || !txTable)
        {
            if (dragAutoScrollTimer)
            {
                dragAutoScrollTimer->stop();
            }
            return;
        }
        auto *scrollBar = txTable->verticalScrollBar();
        if (!scrollBar)
        {
            return;
        }
        const int prev = scrollBar->value();
        scrollBar->setValue(prev + dragAutoScrollSpeed * 15);
        if (scrollBar->value() != prev)
        {
            updateTxDrag(lastDragGlobalPos, lastDragVpPos);
        }
    });
    dragAnimTimer = new QTimer(this);
    dragAnimTimer->setTimerType(Qt::PreciseTimer);
    connect(dragAnimTimer, &QTimer::timeout, this, [this]()
    {
        if (!isDraggingTx || !txTable)
        {
            if (dragAnimTimer)
            {
                dragAnimTimer->stop();
            }
            return;
        }
        double offset = txTable->property("dragAnimOffset").toDouble();
        if (std::abs(offset) > 0.5)
        {
            offset *= 0.74;
            if (std::abs(offset) <= 0.5)
            {
                offset = 0.0;
                dragAnimTimer->stop();
            }
            txTable->setProperty("dragAnimOffset", offset);
            txTable->viewport()->update();
        }
        else
        {
            txTable->setProperty("dragAnimOffset", 0.0);
            dragAnimTimer->stop();
            txTable->viewport()->update();
        }
    });
    txTable->setAccessibleName("Transmit message cards");
    txLayout->addWidget(txTable, 1);
    txMenu = new WorkbenchMenu(txTable);
    txMenu->setObjectName("txMessageMenu");
    configureTranslucentMenu(txMenu);
    txMenu->installEventFilter(this);
    const auto addTxAction = [this](const QString &text, const char *name)
    {
        auto *action = txMenu->addAction(text);
        action->setObjectName(name);
        return action;
    };
    onceAction = addTxAction("Send once", "sendOnce");
    startAction = addTxAction("Start", "startTx");
    stopAction = addTxAction("Stop", "stopTx");
    txMenu->addSeparator();
    editAction = addTxAction("Edit message…", "editMessage");
    duplicateAction = addTxAction("Duplicate message", "duplicateMessage");
    removeAction = addTxAction("Remove message", "removeMessage");
    txMenu->setDefaultAction(removeAction);
    txTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(txTable, &QWidget::customContextMenuRequested, this, [this](const QPoint &position)
    {
        const bool keyboard = position.x() < 0 || position.y() < 0;
        const int row = keyboard ? txTable->currentRow() : txTable->indexAt(position).row();
        if (row < 0 || !txTable->item(row, 1))
        {
            return;
        }
        txTable->setCurrentCell(row, 1);
        updateActions();
        const QPoint anchor = keyboard ? txTable->visualItemRect(txTable->item(row, 1)).center() : position;
        txMenu->popup(txTable->viewport()->mapToGlobal(anchor));
    });
    dashboards->addWidget(txGroup);

    auto *rxGroup = new QFrame;
    rxGroup->setMinimumWidth(540);
    rxGroup->setObjectName("dashboardPanel");
    auto *rxLayout = new QVBoxLayout(rxGroup);
    rxLayout->setContentsMargins(16, 16, 16, 12);
    rxLayout->setSpacing(12);
    auto *rxHeader = new QHBoxLayout;
    auto *rxTitle = new QLabel("RX Dashboard");
    rxTitle->setObjectName("panelTitle");
    rxHeader->addWidget(rxTitle);
    rxSummary = new QLabel("0 streams");
    rxSummary->setObjectName("summaryBadge");
    rxSummary->setFixedHeight(24);
    rxSummary->setAlignment(Qt::AlignCenter);
    rxHeader->addWidget(rxSummary);
    rxThroughput = new QLabel;
    rxThroughput->setObjectName("rxThroughput");
    rxThroughput->setFixedHeight(24);
    rxThroughput->setAlignment(Qt::AlignCenter);
    rxThroughput->hide();
    rxHeader->addWidget(rxThroughput);
    rxHeader->addStretch();
    rxLayout->addLayout(rxHeader);
    auto *rxActions = new QHBoxLayout;
    rxActions->setContentsMargins(0, 0, 0, 0);
    rxActions->setSpacing(4);
    rxFilter = new CatalogSearch;
    rxFilter->setObjectName("rxFilter");
    rxFilter->setFixedHeight(32);
    rxFilter->setPlaceholderText("Search messages or sender…");
    rxFilter->setClearButtonEnabled(true);
    rxFilter->setMinimumWidth(140);
    rxFilter->setAccessibleName("Filter received MAVLink streams");
    rxActions->addWidget(rxFilter);
    auto *pause = button("", "pauseDisplay", rxActions);
    pause->setCheckable(true);
    pause->setFixedSize(32, 32);
    pause->setAccessibleName("Pause display");
    pause->setToolTip("Pause display (Ctrl+P)");
    auto *clear = button("", "clearStatistics", rxActions);
    clear->setFixedSize(32, 32);
    clear->setAccessibleName("Clear received messages");
    clear->setToolTip("Clear received messages and statistics (Ctrl+L)");
    rxLayout->addLayout(rxActions);

    auto *pauseAction = viewMenu->addAction("Pause Display", this, [pause]()
    {
        pause->toggle();
    }, QKeySequence("Ctrl+P"));
    pauseAction->setObjectName("actionPauseDisplay");
    pauseAction->setCheckable(true);
    connect(pause, &QPushButton::toggled, pauseAction, &QAction::setChecked);

    auto *clearAction = viewMenu->addAction("Clear Received Messages", clear, &QPushButton::click, QKeySequence("Ctrl+L"));
    clearAction->setObjectName("actionClearDisplay");
    auto *rxSplit = new QSplitter(Qt::Vertical);
    rxSplit->setObjectName("rxSplit");
    rxSplit->setChildrenCollapsible(false);
    rxSplit->setHandleWidth(16);
    rxTree = new RxTreeWidget;
    rxTree->setObjectName("rxTree");
    rxTree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rxTree->setHeader(new RxHeader(rxTree));
    rxTree->setHeaderLabels({"Message", "Rate (Hz)", "Count"});
    rxTree->header()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    rxTree->headerItem()->setTextAlignment(0, Qt::AlignLeft | Qt::AlignVCenter);
    rxTree->headerItem()->setTextAlignment(1, Qt::AlignCenter);
    rxTree->headerItem()->setTextAlignment(2, Qt::AlignCenter);
    rxTree->setRootIsDecorated(true);
    rxTree->setIndentation(16);
    rxTree->setUniformRowHeights(true);
    rxTree->setAlternatingRowColors(false);
    rxTree->setSelectionMode(QAbstractItemView::SingleSelection);
    static_cast<RxHeader *>(rxTree->header())->resetColumns();
    rxTree->setItemDelegate(new RxItemDelegate(rxTree));
    inspector = table({"Parameter", "Type", "Value", "Unit"});
    inspector->setObjectName("inspector");
    inspector->setItemDelegateForColumn(0, new PayloadIdentityDelegate(inspector));
    inspector->setItemDelegateForColumn(1, new PayloadIdentityDelegate(inspector));
    inspector->setAlternatingRowColors(false);
    inspector->setSelectionMode(QAbstractItemView::NoSelection);
    inspector->setFocusPolicy(Qt::NoFocus);
    inspector->setMouseTracking(true);
    inspector->verticalHeader()->setDefaultSectionSize(40);
    inspector->horizontalHeader()->setFixedHeight(36);
    inspector->horizontalHeader()->setMinimumSectionSize(90);
    inspector->setContextMenuPolicy(Qt::CustomContextMenu);
    auto *valueMenu = new WorkbenchMenu(inspector);
    valueMenu->setObjectName("inspectorMenu");
    configureTranslucentMenu(valueMenu);
    valueMenu->installEventFilter(this);
    auto *copyValue = valueMenu->addAction("Copy value");
    copyValue->setObjectName("copyInspectorValue");
    connect(copyValue, &QAction::triggered, this, [this, copyValue]()
    {
        const QString text = copyValue->data().toString();
        QApplication::clipboard()->setText(text);
        int row = copyValue->property("targetRow").isValid() ? copyValue->property("targetRow").toInt() : -1;
        if (row < 0 || row >= inspector->rowCount())
        {
            row = inspector->currentRow();
        }
        if (row < 0 || row >= inspector->rowCount())
        {
            for (int i = 0; i < inspector->rowCount(); ++i)
            {
                if (auto *item = inspector->item(i, 2))
                {
                    if (item->text() == text)
                    {
                        row = i;
                        break;
                    }
                }
            }
        }
        showCopiedFeedback(row);
    });
    auto *editParamAction = valueMenu->addAction("Edit parameter…");
    editParamAction->setObjectName("editParamAction");
    connect(editParamAction, &QAction::triggered, this, [this, editParamAction]()
    {
        const int row = editParamAction->property("targetRow").toInt();
        editParameter(row);
    });
    plotFieldAction = valueMenu->addAction("Plot this field");
    plotFieldAction->setObjectName("plotFieldAction");
    connect(plotFieldAction, &QAction::triggered, this, [this]()
    {
        const int row = plotFieldAction->property("targetRow").toInt();
        if (row < 0 || row >= inspector->rowCount())
        {
            return;
        }
        if (isParamView)
        {
            const QString paramName = inspector->item(row, 0)->text();
            togglePlotField(activeParamSysId, activeParamCompId, 22, paramName, paramName);
        }
        else
        {
            auto *item = rxTree->currentItem();
            if (!item || !item->parent())
            {
                return;
            }
            const uint32_t msgid = item->data(0, Qt::UserRole).toUInt();
            const uint32_t sysid = item->parent()->data(0, Qt::UserRole).toUInt();
            const uint8_t compid = uint8_t(item->parent()->data(0, Qt::UserRole + 1).toUInt());
            const auto *info = mavlink_get_message_info_by_id(msgid);
            if (info && row < int(info->num_fields))
            {
                const QString fieldName = QString::fromLatin1(info->fields[row].name);
                const QString label = QString("%1.%2").arg(messageName(msgid), fieldName);
                togglePlotField(sysid, compid, msgid, fieldName, label);
            }
        }
    });
    connect(inspector, &QWidget::customContextMenuRequested, this, [this, valueMenu, copyValue, editParamAction](const QPoint &position)
    {
        const bool keyboard = position.x() < 0 || position.y() < 0;
        const int row = keyboard ? inspector->currentRow() : inspector->indexAt(position).row();
        auto *value = inspector->item(row, 2);
        if (!value)
        {
            return;
        }
        copyValue->setData(value->text());
        copyValue->setProperty("targetRow", row);
        editParamAction->setVisible(isParamView);
        editParamAction->setProperty("targetRow", row);

        bool isPlottable = false;
        bool isPlotted = false;
        if (isParamView)
        {
            isPlottable = true;
            const QString paramName = inspector->item(row, 0)->text();
            isPlotted = telemetryPlotter && telemetryPlotter->hasChannel(activeParamSysId, activeParamCompId, 22, paramName);
        }
        else
        {
            auto *treeItem = rxTree->currentItem();
            if (treeItem && treeItem->parent())
            {
                const uint32_t msgid = treeItem->data(0, Qt::UserRole).toUInt();
                const uint32_t sysid = treeItem->parent()->data(0, Qt::UserRole).toUInt();
                const uint8_t compid = uint8_t(treeItem->parent()->data(0, Qt::UserRole + 1).toUInt());
                const auto *info = mavlink_get_message_info_by_id(msgid);
                if (info && row < int(info->num_fields))
                {
                    isPlottable = (info->fields[row].type != MAVLINK_TYPE_CHAR);
                    const QString fieldName = QString::fromLatin1(info->fields[row].name);
                    isPlotted = telemetryPlotter && telemetryPlotter->hasChannel(sysid, compid, msgid, fieldName);
                }
            }
        }
        plotFieldAction->setVisible(isPlottable);
        plotFieldAction->setProperty("targetRow", row);
        plotFieldAction->setText(isPlotted ? "Unplot this field" : "Plot this field");

        const QPoint anchor = keyboard ? inspector->visualItemRect(value).center() : position;
        valueMenu->popup(inspector->viewport()->mapToGlobal(anchor));
    });
    connect(inspector, &QTableWidget::cellDoubleClicked, this, [this](int row, int col)
    {
        Q_UNUSED(col);
        if (isParamView)
        {
            editParameter(row);
        }
    });
    connect(inspector->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]()
    {
        if (copyFeedbackBadge)
        {
            copyFeedbackBadge->hide();
        }
    });
    connect(inspector->horizontalScrollBar(), &QScrollBar::valueChanged, this, [this]()
    {
        if (copyFeedbackBadge)
        {
            copyFeedbackBadge->hide();
        }
    });
    inspector->setColumnWidth(0, 190);
    inspector->setColumnWidth(1, 105);
    inspector->horizontalHeader()->setStretchLastSection(false);
    inspector->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    inspector->hideColumn(3);
    const auto addInspectorPane = [rxSplit](const QString &title, QWidget *content)
    {
        auto *pane = new QFrame;
        pane->setObjectName("dataPane");
        auto *paneLayout = new QVBoxLayout(pane);
        paneLayout->setContentsMargins(1, 1, 1, 1);
        paneLayout->setSpacing(0);
        auto *caption = new QLabel(title);
        caption->setObjectName("dataPaneTitle");
        caption->setWordWrap(true);
        paneLayout->addWidget(caption);
        paneLayout->addWidget(content, 1);
        rxSplit->addWidget(pane);
        return caption;
    };
    addInspectorPane("RECEIVED MESSAGES", rxTree);

    auto *inspectorPane = new QFrame;
    inspectorPane->setObjectName("dataPane");
    auto *inspectorLayout = new QVBoxLayout(inspectorPane);
    inspectorLayout->setContentsMargins(1, 1, 1, 1);
    inspectorLayout->setSpacing(0);

    auto *inspectorHeader = new QFrame;
    inspectorHeader->setObjectName("inspectorHeader");
    auto *headerLayout = new QHBoxLayout(inspectorHeader);
    headerLayout->setContentsMargins(10, 8, 10, 8);
    headerLayout->setSpacing(10);

    inspectorTitle = new QLabel("DECODED FIELDS");
    inspectorTitle->setObjectName("inspectorMessageTitle");
    headerLayout->addWidget(inspectorTitle);

    inspectorSource = new QLabel;
    inspectorSource->setObjectName("inspectorSource");
    inspectorSource->hide();
    headerLayout->addWidget(inspectorSource);

    paramFilter = new QLineEdit;
    paramFilter->setObjectName("paramFilter");
    paramFilter->setPlaceholderText("Search parameters…");
    paramFilter->setFixedWidth(160);
    paramFilter->setFixedHeight(26);
    paramFilter->setClearButtonEnabled(true);
    if (auto *clearBtn = paramFilter->findChild<QToolButton *>())
    {
        clearBtn->setIcon(clearButtonIcon(palette().color(QPalette::Window).lightness() < 128));
    }
    paramFilter->setAttribute(Qt::WA_InputMethodEnabled, false);
    paramFilter->setInputMethodHints(Qt::ImhLatinOnly | Qt::ImhNoPredictiveText);
    paramFilter->hide();
    headerLayout->addWidget(paramFilter);

    connect(paramFilter, &QLineEdit::textChanged, this, [this]()
    {
        updateInspector();
    });

    headerLayout->addStretch(1);

    pausedLabel = new QLabel("Display paused");
    pausedLabel->setObjectName("displayPaused");
    pausedLabel->hide();
    headerLayout->addWidget(pausedLabel);

    plotToggleBtn = new QPushButton("Plotter");
    plotToggleBtn->setObjectName("plotToggleBtn");
    plotToggleBtn->setCheckable(true);
    plotToggleBtn->setChecked(false);
    plotToggleBtn->setFixedHeight(24);
    plotToggleBtn->setToolTip("Toggle real-time telemetry plotter");
    headerLayout->addWidget(plotToggleBtn);

    inspectorContentSplitter = new QSplitter(Qt::Horizontal);
    inspectorContentSplitter->setObjectName("inspectorContentSplitter");
    inspectorContentSplitter->setHandleWidth(8);
    inspectorContentSplitter->setChildrenCollapsible(false);

    inspectorContentSplitter->addWidget(inspector);

    telemetryPlotter = new TelemetryPlotWidget(this);
    telemetryPlotter->setObjectName("telemetryPlotter");
    telemetryPlotter->hide();
    inspectorContentSplitter->addWidget(telemetryPlotter);

    inspectorLayout->addWidget(inspectorHeader);
    inspectorLayout->addWidget(inspectorContentSplitter, 1);

    connect(plotToggleBtn, &QPushButton::toggled, this, [this](bool checked)
    {
        if (checked)
        {
            telemetryPlotter->show();
            if (telemetryPlotter->isFloating())
            {
                telemetryPlotter->raise();
                telemetryPlotter->activateWindow();
            }
            else
            {
                inspectorContentSplitter->setSizes({350, 550});
            }
        }
        else
        {
            telemetryPlotter->hide();
        }
    });

    telemetryPlotter->onFloatingChanged = [this](bool floating)
    {
        if (floating)
        {
            telemetryPlotter->setWindowIcon(windowIcon());
        }
        else
        {
            inspectorContentSplitter->addWidget(telemetryPlotter);
            if (plotToggleBtn && plotToggleBtn->isChecked())
            {
                telemetryPlotter->show();
                inspectorContentSplitter->setSizes({350, 550});
            }
        }
    };

    telemetryPlotter->onClosed = [this]()
    {
        telemetryPlotter->hide();
        if (plotToggleBtn)
        {
            plotToggleBtn->setChecked(false);
        }
    };

    telemetryPlotter->onChannelsChanged = [this]()
    {
        if (plotToggleBtn)
        {
            const size_t count = telemetryPlotter->channelCount();
            if (count > 0)
            {
                plotToggleBtn->setText(QString("Plotter (%1)").arg(count));
            }
            else
            {
                plotToggleBtn->setText("Plotter");
            }
        }
    };
    rxSplit->addWidget(inspectorPane);
    rxSplit->setSizes({300, 300});
    rxLayout->addWidget(rxSplit, 1);
    dashboards->addWidget(rxGroup);
    dashboards->setSizes({467, 929});
    dashboards->setStretchFactor(0, 0);
    dashboards->setStretchFactor(1, 1);
    workspace->addWidget(dashboards, 1);
    metricsWidget = new StatusBarMetricsWidget;
    metricsWidget->setObjectName("statusBarMetrics");
    lastError = new StatusBarMessageWidget("Ready", this);
    lastError->setObjectName("statusMessage");
    lastError->hide();

    statusBarCard = new QFrame;
    statusBarCard->setObjectName("statusBarCard");
    auto *statusLayout = new QHBoxLayout(statusBarCard);
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->addStretch(1);
    statusLayout->addWidget(metricsWidget);
    workspace->addWidget(statusBarCard);

    setStatusBar(nullptr);

    qRegisterMetaType<PortConfig>();
    qRegisterMetaType<Mav::TxEntry>();
    qRegisterMetaType<SnapshotPtr>();
    worker = new SerialWorker;
    worker->moveToThread(&thread);
    connect(&thread, &QThread::started, worker, &SerialWorker::initialize);
    connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &SerialWorker::stateChanged, this, [this, refreshPortsButton](const QString &state, bool open)
    {
        if (connectionStatusPill)
        {
            connectionStatusPill->setStatus(state, open);
        }
        connected = open;
        if (auto *tree = dynamic_cast<RxTreeWidget *>(rxTree))
        {
            tree->setConnectionInfo(open,
                                    portBox ? portBox->currentText() : QString(),
                                    baudBox ? baudBox->currentText() : QString());
        }
        if (rxThroughput)
        {
            if (!open)
            {
                rxThroughput->hide();
            }
            else
            {
                if (latest)
                {
                    setRxThroughputStats(latest->rxBytesPerSecond, latest->rxUtilizationPercent, latest->baud);
                }
                else
                {
                    setRxThroughputStats(0.0, 0.0, baudBox ? baudBox->currentText().toInt() : 115200);
                }
            }
        }
        connecting = state == "Connecting";
        reconnecting = state == "Reconnecting";
        if (reconnecting)
        {
            connectButton->setText("Cancel (Reconnecting...)");
            connectButton->setEnabled(true);
        }
        else
        {
            connectButton->setText(open ? "Disconnect" : "Connect");
            connectButton->setEnabled(!connecting);
        }
        connectButton->setProperty("connected", open);
        connectButton->setProperty("reconnecting", reconnecting);
        connectButton->style()->unpolish(connectButton);
        connectButton->style()->polish(connectButton);
        refreshPortsButton->setEnabled(!open && !connecting && !reconnecting);
        for (auto *combo : {portBox, baudBox, dataBox, parityBox, stopBox, flowBox})
        {
            combo->setEnabled(!open && !connecting && !reconnecting);
        }
        if (open)
        {
            connectingByUser = false;
            // Persist the serial port, baudrate and advanced parameters of the last successful connection
            QSettings settings("MAVLinkSimulator", "Preferences");
            if (portBox)
            {
                settings.setValue("serial/port", portBox->currentText());
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

            rxTree->clear();
            inspector->setRowCount(0);
        }
        updateActions();
    });
    connect(worker, &SerialWorker::profileImported, this, [this](int count, bool append, quint64 firstUnusedKey)
    {
        nextKey = std::max<uint64_t>(nextKey, firstUnusedKey);
        profileBusy = false;
        updateActions();
        lastError->setText(QString("Imported %1 TX messages (%2)").arg(count).arg(append ? "Append" : "Replace"));
    });
    connect(worker, &SerialWorker::profileImportFailed, this, [this](const QString &error)
    {
        profileBusy = false;
        updateActions();
        showError(error);
    });
    connect(worker, &SerialWorker::profileExportFailed, this, [this](const QString &error)
    {
        profileBusy = false;
        profileExportPath.clear();
        updateActions();
        showError(error);
    });
    connect(worker, &SerialWorker::profileExported, this, [this](const QByteArray &data)
    {
        QSaveFile file(profileExportPath);
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        {
            showError("Could not export profile: " + file.errorString());
        }
        else
        {
            lastError->setText("Profile exported: " + QFileInfo(profileExportPath).fileName());
        }
        profileExportPath.clear();
        profileBusy = false;
        updateActions();
    });
    connect(worker, &SerialWorker::failure, this, &MainWindow::showError);
    connect(worker, &SerialWorker::snapshotReady, this, &MainWindow::applySnapshot);
    connect(refreshPortsButton, &QPushButton::clicked, this, &MainWindow::refreshPorts);
    connect(connectButton, &QPushButton::clicked, this, &MainWindow::toggleConnection);
    connect(addButton, &QPushButton::clicked, this, [this]()
    {
        editMessage(false);
    });
    connect(editAction, &QAction::triggered, this, [this]()
    {
        editMessage(true);
    });
    connect(duplicateAction, &QAction::triggered, this, [this]()
    {
        const uint64_t currentKey = selectedTx();
        if (currentKey == 0)
        {
            return;
        }
        const uint64_t newKey = nextKey++;
        pendingSelectionKey = newKey;
        pendingDuplicateKey = newKey;
        QMetaObject::invokeMethod(worker, "duplicateEntry", Qt::QueuedConnection,
                                  Q_ARG(quint64, currentKey), Q_ARG(quint64, newKey));
        saveTxMessages();
    });
    connect(removeAction, &QAction::triggered, this, [this]()
    {
        deleteTxCardAt(txTable->currentRow());
    });
    connect(onceAction, &QAction::triggered, this, [this]()
    {
        QMetaObject::invokeMethod(worker, "sendOnce", Qt::QueuedConnection, Q_ARG(quint64, selectedTx()));
    });
    connect(startAction, &QAction::triggered, this, [this]()
    {
        QMetaObject::invokeMethod(worker, "setActive", Qt::QueuedConnection,
                                  Q_ARG(quint64, selectedTx()), Q_ARG(bool, true));
    });
    connect(stopAction, &QAction::triggered, this, [this]()
    {
        QMetaObject::invokeMethod(worker, "setActive", Qt::QueuedConnection,
                                  Q_ARG(quint64, selectedTx()), Q_ARG(bool, false));
    });
    connect(runAllButton, &QPushButton::clicked, worker, &SerialWorker::runAll);
    connect(stopAllButton, &QPushButton::clicked, worker, &SerialWorker::stopAll);
    connect(deleteAllButton, &QPushButton::clicked, this, [this]()
    {
        txUndoStack.clear();
        lastDeletedTxEntry = Mav::TxEntry{};
        lastDeletedTxRowIndex = -1;
        if (txUndoToast)
        {
            txUndoToast->dismiss();
        }
        QMetaObject::invokeMethod(worker, "deleteAll", Qt::QueuedConnection);
    });
    connect(clear, &QPushButton::clicked, this, [this]()
    {
        QMetaObject::invokeMethod(worker, "clearStatistics", Qt::QueuedConnection);
        rxTree->clear();
        inspector->setRowCount(0);
        inspectorTitle->setText("DECODED FIELDS");
        inspectorSource->clear();
        inspectorSource->hide();
        filterRx();
    });
    connect(pause, &QPushButton::toggled, this, [this, pause](bool checked)
    {
        paused = checked;
        pause->setToolTip(checked ? "Resume display (Ctrl+P)" : "Pause display (Ctrl+P)");
        pause->setAccessibleName(checked ? "Resume display" : "Pause display");
        pause->setProperty("paused", checked);
        pause->style()->unpolish(pause);
        pause->style()->polish(pause);
        const bool dark = palette().color(QPalette::Window).lightness() < 128;
        const QColor activeColor = checked
            ? (dark ? QColor("#fbbf24") : QColor("#d97706"))
            : (dark ? QColor("#e2e8f0") : QColor("#334155"));
        pause->setIcon(actionIcon(checked ? "resumeDisplay" : "pauseDisplay", activeColor));
        pausedLabel->setVisible(checked);
    });
    connect(rxFilter, &QLineEdit::textChanged, this, &MainWindow::filterRx);
    connect(rxTree, &QTreeWidget::itemSelectionChanged, this, &MainWindow::updateInspector);
    connect(txTable, &QTableWidget::itemSelectionChanged, this, &MainWindow::updateActions);
    connect(txTable, &QTableWidget::cellDoubleClicked, this, [this]()
    {
        editMessage(true);
    });
    connect(themeBox, &QComboBox::currentIndexChanged, this, [this]()
    {
        applyTheme();
        saveSettings();
    });
    connect(defaultSysIdBox, &QSpinBox::valueChanged, this, [this]()
    {
        saveSettings();
    });
    connect(defaultCompIdBox, &QSpinBox::valueChanged, this, [this]()
    {
        saveSettings();
    });
    connect(defaultVersionBox, &QComboBox::currentIndexChanged, this, [this]()
    {
        saveSettings();
    });
    connect(autoReconnectBox, &QCheckBox::toggled, this, [this](bool enabled)
    {
        saveSettings();
        if (worker)
        {
            QMetaObject::invokeMethod(worker, "setAutoReconnect", Qt::QueuedConnection, Q_ARG(bool, enabled));
        }
    });
    connect(showDebugBarBox, &QCheckBox::toggled, this, [this](bool visible)
    {
        if (statusBarCard)
        {
            statusBarCard->setVisible(visible);
        }
        if (statsRateUnitBox)
        {
            statsRateUnitBox->setEnabled(visible);
        }
        if (statsRateUnitLabel)
        {
            statsRateUnitLabel->setEnabled(visible);
        }
        saveSettings();
    });
    connect(statsRateUnitBox, &QComboBox::currentIndexChanged, this, [this](int index)
    {
        if (metricsWidget)
        {
            metricsWidget->setRateUnit(index);
        }
        saveSettings();
    });
    connect(restoreTxBox, &QCheckBox::toggled, this, [this]()
    {
        saveSettings();
    });
    connect(singleActiveStreamBox, &QCheckBox::toggled, this, [this](bool enabled)
    {
        if (worker)
        {
            QMetaObject::invokeMethod(worker, "setSingleActiveStream", Qt::QueuedConnection,
                                      Q_ARG(bool, enabled));
        }
        saveSettings();
    });
    connect(deassertControlLinesBox, &QCheckBox::toggled, this, [this](bool enabled)
    {
        saveSettings();
        if (worker)
        {
            QMetaObject::invokeMethod(worker, "setDeassertControlLines", Qt::QueuedConnection,
                                      Q_ARG(bool, enabled));
        }
    });
    connect(resetLayoutButton, &QPushButton::clicked, this, [this]()
    {
        resetLayout();
    });
    qApp->installEventFilter(this);
    connect(&refresh, &QTimer::timeout, this, [this]()
    {
        if (!snapshotPending)
        {
            snapshotPending = true;
            QMetaObject::invokeMethod(worker, "requestSnapshot", Qt::QueuedConnection);
        }
    });
    thread.start();
    refresh.start(200);
    refreshPorts();
    restoreSettings();
    applyTheme();
    updateActions();
    connect(txTable->horizontalHeader(), &QHeaderView::sectionResized, this, &MainWindow::updateTxHeights);
    restoreLayout();
    restoreTxMessages();
    // Start with neutral focus instead of selecting the first editable control.
    setFocus(Qt::OtherFocusReason);
}

/** @brief Fit TX card titles after model updates or viewport width changes. */

void MainWindow::updateTxHeights()
{
    const int scroll = txTable->verticalScrollBar()->value();
    QFont font = txTable->font();
    font.setPixelSize(13);
    font.setWeight(QFont::Normal);
    for (int row = 0; row < txTable->rowCount(); ++row)
    {
        if (!txTable->item(row, 0) || !txTable->item(row, 1))
        {
            continue;
        }
        const QString title = QString("[%1] %2").arg(txTable->item(row, 0)->text(), txTable->item(row, 1)->text());
        const int height = txTitleLines(title, font, txTable->columnWidth(1) - 120).size() > 1 ? 90 : 74;
        if (txTable->rowHeight(row) != height)
        {
            txTable->setRowHeight(row, height);
        }
    }
    txTable->verticalScrollBar()->setValue(scroll);
}

/** @brief Restore presentation state without restoring ports, payloads or transmission state. */
void MainWindow::restoreLayout()
{
    QSettings settings("MAVLinkSimulator", "Layout");
    if (settings.value("version").toInt() != 1)
    {
        return;
    }
    restoreGeometry(settings.value("geometry").toByteArray());
    for (const QString &name : {QString("dashboards"), QString("rxSplit")})
    {
        findChild<QSplitter *>(name)->restoreState(settings.value(name).toByteArray());
    }
    static_cast<RxHeader *>(rxTree->header())->resetColumns();
    inspector->horizontalHeader()->restoreState(settings.value("fieldColumns").toByteArray());
    // Hidden metadata is not part of the user-configurable table presentation.
    inspector->hideColumn(3);
}

/** @brief Reset window geometry, splitters and column presentations to defaults. */
void MainWindow::resetLayout()
{
    QSettings settings("MAVLinkSimulator", "Layout");
    settings.clear();
    settings.sync();

    resize(1440, 900);
    auto *dashboards = findChild<QSplitter *>("dashboards");
    if (dashboards)
    {
        dashboards->setSizes({467, 929});
    }
    auto *rxSplit = findChild<QSplitter *>("rxSplit");
    if (rxSplit)
    {
        rxSplit->setSizes({300, 300});
    }
    if (rxTree)
    {
        static_cast<RxHeader *>(rxTree->header())->resetColumns();
    }
    if (inspector)
    {
        inspector->setColumnWidth(0, 190);
        inspector->setColumnWidth(1, 105);
        inspector->hideColumn(3);
    }
    if (inspectorContentSplitter)
    {
        inspectorContentSplitter->setSizes({350, 550});
    }
    if (lastError)
    {
        lastError->setText("Layout reset to defaults");
    }
}


/** @brief Cancel drag-and-drop operations on Escape and dispatch smart TX shortcuts. */
void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (isDraggingTx && event->key() == Qt::Key_Escape)
    {
        cancelTxDrag();
        return;
    }
    if (handleTxKeyPress(event))
    {
        return;
    }
    QMainWindow::keyPressEvent(event);
}

/** @brief Keep overlays such as the undo toast properly positioned on resize. */
void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    repositionTxUndoToast();
}


/** @brief Save only geometry, splitters and column presentation on a normal close. */
void MainWindow::closeEvent(QCloseEvent *event)
{
    finalizeTxDeleteAnimation();
    saveSettings();
    if (restoreTxBox && restoreTxBox->isChecked())
    {
        saveTxMessages();
    }
    else
    {
        QSettings workspace("MAVLinkSimulator", "Workspace");
        workspace.remove("txMessages");
        workspace.sync();
    }
    QSettings settings("MAVLinkSimulator", "Layout");
    settings.setValue("version", 1);
    settings.setValue("geometry", saveGeometry());
    for (const QString &name : {QString("dashboards"), QString("rxSplit")})
    {
        settings.setValue(name, findChild<QSplitter *>(name)->saveState());
    }
    settings.remove("rxColumns");
    settings.setValue("fieldColumns", inspector->horizontalHeader()->saveState());
    if (telemetryPlotter && telemetryPlotter->isFloating())
    {
        telemetryPlotter->close();
    }
    // Ensure periodic transmissions are stopped and the serial port is closed immediately on window exit.
    QMetaObject::invokeMethod(worker, "closePort", Qt::BlockingQueuedConnection);
    QMainWindow::closeEvent(event);
}

MainWindow::~MainWindow()
{
    refresh.stop();
    QMetaObject::invokeMethod(worker, "closePort", Qt::BlockingQueuedConnection);
    thread.quit();
    thread.wait();
}



bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (auto *popup = qobject_cast<QMenu *>(watched))
    {
        if (event->type() == QEvent::ShowToParent || event->type() == QEvent::Show || event->type() == QEvent::Resize)
        {
            configureTranslucentMenu(popup);
            popup->clearMask();
        }
    }
    if (watched == qApp && event->type() == QEvent::ApplicationPaletteChange &&
        themeBox && themeBox->currentIndex() == 0)
    {
        applyTheme();
    }
    if (event->type() == QEvent::KeyPress)
    {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (handleTxKeyPress(keyEvent))
        {
            return true;
        }
    }
    if (event->type() == QEvent::MouseButtonPress)
    {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton && txTable)
        {
            if (watched == txTable->viewport())
            {
                const QModelIndex index = txTable->indexAt(mouseEvent->pos());
                if (!index.isValid())
                {
                    txTable->clearSelection();
                    txTable->setCurrentItem(nullptr);
                    txTable->setCurrentCell(-1, -1);
                    updateActions();
                }
                else if (index.column() == 1)
                {
                    const QRect rect = txTable->visualRect(index);
                    const QRect card = txCardRect(rect);
                    const QRect handleRect = txDragHandleRect(card);
                    if (handleRect.contains(mouseEvent->pos()))
                    {
                        dragHandlePressed = true;
                        dragStartPos = mouseEvent->pos();
                        dragSourceRow = index.row();
                        dragTargetRow = index.row();
                        return true;
                    }
                }
            }
            else
            {
                const auto *widget = qobject_cast<const QWidget *>(watched);
                const bool isScroll = widget && (widget == txTable->verticalScrollBar() ||
                                                 widget == txTable->horizontalScrollBar() ||
                                                 txTable->verticalScrollBar()->isAncestorOf(widget) ||
                                                 txTable->horizontalScrollBar()->isAncestorOf(widget));
                const bool isTxMenu = txMenu && (txMenu == watched || txMenu->isAncestorOf(widget));
                const bool isMenu = qobject_cast<const QMenu *>(watched) || qobject_cast<const QMenuBar *>(watched);
                const bool hasModal = QApplication::activeModalWidget() != nullptr;

                // Native QWindow events precede QWidget delivery; classify only widget clicks.
                if (widget && !isScroll && !isTxMenu && !isMenu && !hasModal)
                {
                    if (txTable->currentRow() >= 0 || !txTable->selectedItems().isEmpty())
                    {
                        txTable->clearSelection();
                        txTable->setCurrentItem(nullptr);
                        txTable->setCurrentCell(-1, -1);
                        updateActions();
                    }
                }
            }
        }
    }
    if (txTable && (watched == txTable->viewport() || watched == txTable))
    {
        if (event->type() == QEvent::KeyPress)
        {
            auto *keyEvent = static_cast<QKeyEvent *>(event);
            if (isDraggingTx && keyEvent->key() == Qt::Key_Escape)
            {
                cancelTxDrag();
                return true;
            }
        }
    }
    if (txTable && watched == txTable->viewport())
    {
        if (event->type() == QEvent::MouseButtonRelease)
        {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton)
            {
                if (isDraggingTx)
                {
                    finishTxDrag(true);
                    return true;
                }
                if (dragHandlePressed)
                {
                    dragHandlePressed = false;
                    return true;
                }
                const QModelIndex index = txTable->indexAt(mouseEvent->pos());
                if (index.isValid() && index.column() == 1)
                {
                    const QRect rect = txTable->visualRect(index);
                    const QRect card = txCardRect(rect);
                    const QRect badge(card.right() - 88, card.top() + 8, 80, 24);
                    const QRect ratePill = txRatePillRect(card, txTable->item(index.row(), 0)->text(),
                                                          txTable->item(index.row(), 1)->text(),
                                                          txTable->item(index.row(), 3)->text(),
                                                          txTable->font());
                    if (ratePill.contains(mouseEvent->pos()))
                    {
                        const auto key = txTable->item(index.row(), 0)->data(Qt::UserRole).toULongLong();
                        const double currentHz = txTable->item(index.row(), 3)->text().toDouble();
                        showQuickRateMenu(key, currentHz, txTable->viewport()->mapToGlobal(ratePill.bottomLeft() + QPoint(0, 2)));
                        return true;
                    }
                    if (badge.contains(mouseEvent->pos()) && connected)
                    {
                        const auto key = txTable->item(index.row(), 0)->data(Qt::UserRole).toULongLong();
                        const bool running = txTable->item(index.row(), 6)->text() == "Running";
                        QMetaObject::invokeMethod(worker, "setActive", Qt::QueuedConnection,
                                                  Q_ARG(quint64, key), Q_ARG(bool, !running));
                        return true;
                    }
                }
            }
        }
        else if (event->type() == QEvent::MouseButtonDblClick)
        {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton)
            {
                const QModelIndex index = txTable->indexAt(mouseEvent->pos());
                if (index.isValid() && index.column() == 1)
                {
                    const QRect rect = txTable->visualRect(index);
                    const QRect card = txCardRect(rect);
                    const QRect handleRect = txDragHandleRect(card);
                    const QRect badge(card.right() - 88, card.top() + 8, 80, 24);
                    const QRect ratePill = txRatePillRect(card, txTable->item(index.row(), 0)->text(),
                                                          txTable->item(index.row(), 1)->text(),
                                                          txTable->item(index.row(), 3)->text(),
                                                          txTable->font());
                    if (handleRect.contains(mouseEvent->pos()) || badge.contains(mouseEvent->pos()) || ratePill.contains(mouseEvent->pos()))
                    {
                        return true;
                    }
                }
            }
        }
        else if (event->type() == QEvent::MouseMove)
        {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (dragHandlePressed)
            {
                if (!isDraggingTx)
                {
                    if ((mouseEvent->pos() - dragStartPos).manhattanLength() >= QApplication::startDragDistance())
                    {
                        startTxDrag(dragSourceRow, mouseEvent->globalPosition().toPoint());
                        updateTxDrag(mouseEvent->globalPosition().toPoint(), mouseEvent->pos());
                    }
                }
                else
                {
                    updateTxDrag(mouseEvent->globalPosition().toPoint(), mouseEvent->pos());
                }
                return true;
            }
            const QModelIndex index = txTable->indexAt(mouseEvent->pos());
            if (index.isValid() && index.column() == 1)
            {
                const QRect rect = txTable->visualRect(index);
                const QRect card = txCardRect(rect);
                const QRect handleRect = txDragHandleRect(card);
                const QRect badge(card.right() - 88, card.top() + 8, 80, 24);
                const QRect ratePill = txRatePillRect(card, txTable->item(index.row(), 0)->text(),
                                                      txTable->item(index.row(), 1)->text(),
                                                      txTable->item(index.row(), 3)->text(),
                                                      txTable->font());
                if (handleRect.contains(mouseEvent->pos()))
                {
                    txTable->viewport()->setCursor(Qt::OpenHandCursor);
                }
                else if (ratePill.contains(mouseEvent->pos()) || (badge.contains(mouseEvent->pos()) && connected))
                {
                    txTable->viewport()->setCursor(Qt::PointingHandCursor);
                }
                else
                {
                    txTable->viewport()->setCursor(Qt::ArrowCursor);
                }
            }
            else
            {
                txTable->viewport()->setCursor(Qt::ArrowCursor);
            }
        }
    }
    auto *control = qobject_cast<QPushButton *>(watched);
    if (control && isAncestorOf(control))
    {
        bool keyboardFocus = control->property("keyboardFocus").toBool();
        if (event->type() == QEvent::FocusIn)
        {
            const auto reason = static_cast<QFocusEvent *>(event)->reason();
            keyboardFocus = reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason ||
                            reason == Qt::ShortcutFocusReason;
        }
        else if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::FocusOut)
        {
            keyboardFocus = false;
        }
        else if (event->type() == QEvent::KeyPress)
        {
            const int key = static_cast<QKeyEvent *>(event)->key();
            keyboardFocus |= key == Qt::Key_Space || key == Qt::Key_Return || key == Qt::Key_Enter;
        }
        if (control->property("keyboardFocus").toBool() != keyboardFocus)
        {
            control->setProperty("keyboardFocus", keyboardFocus);
            control->style()->unpolish(control);
            control->style()->polish(control);
            control->update();
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

