#include "window.h"
#include <QApplication>
#include <QClipboard>
#include <QWidgetAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QFrame>
#include <QHeaderView>
#include <QBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QSerialPortInfo>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QStyleOptionSpinBox>
#include <QPainter>
#include <QPainterPath>
#include <QRegion>
#include <QStyledItemDelegate>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <algorithm>

/**
 * @brief Check whether a serial port represents an actively connected hardware device.
 * @param info Serial port metadata.
 * @return True if the device is a plugged USB converter, active onboard UART, or genuine hardware port.
 */
static bool isPluggedPort(const QSerialPortInfo &info)
{
    const QString name = info.portName();
    // Exclude legacy/virtual PC serial ports
    if (name.startsWith("ttyS"))
    {
        return false;
    }
    // Hotplugged USB serial devices (FTDI, CP210x, CH340, CDC-ACM, etc.)
    if (name.startsWith("ttyUSB") || name.startsWith("ttyACM") || name.startsWith("rfcomm"))
    {
        return true;
    }
    if (info.hasVendorIdentifier() || info.hasProductIdentifier())
    {
        return true;
    }
    // Embedded onboard hardware UARTs (Raspberry Pi, NVIDIA Jetson, etc.)
    if (name.startsWith("ttyAMA") || name.startsWith("ttyTHS"))
    {
        return true;
    }
    return false;
}

/**
 * @brief Generate a small vector dot indicator for stream freshness.
 * @param age Elapsed seconds since the last frame.
 * @param dark True if rendering under a dark palette.
 * @return Theme-aware icon representing stream activity.
 */
static QIcon activityLed(double age, bool dark)
{
    QPixmap pixmap(10, 10);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    QColor color;
    if (age <= 0.5)
    {
        color = dark ? QColor("#3fb950") : QColor("#1a7f37");
    }
    else if (age <= 2.0)
    {
        color = dark ? QColor("#d29922") : QColor("#9a6700");
    }
    else
    {
        color = dark ? QColor("#6e7681") : QColor("#8c959f");
    }
    p.setBrush(color);
    p.setPen(Qt::NoPen);
    p.drawEllipse(1, 1, 8, 8);
    return QIcon(pixmap);
}

/**
 * @brief Provide human-readable enum descriptions for common MAVLink fields.
 * @param msgid MAVLink message identity.
 * @param field Field name string.
 * @param val Decimal string value.
 * @return Concise description or empty string.
 */
static QString mavlinkEnumHint(uint32_t msgid, const QString &field, const QString &val)
{
    bool ok = false;
    const uint64_t v = val.toULongLong(&ok);
    if (!ok)
    {
        return {};
    }
    if (msgid == 0)
    {
        if (field == "autopilot")
        {
            switch (v)
            {
                case 0: return "GENERIC";
                case 3: return "ARDUPILOTMEGA";
                case 12: return "PX4";
                default: break;
            }
        }
        else if (field == "type")
        {
            switch (v)
            {
                case 1: return "FIXED_WING";
                case 2: return "QUADROTOR";
                case 3: return "COAXIAL";
                case 4: return "HELICOPTER";
                case 10: return "GROUND_ROVER";
                case 12: return "SUBMARINE";
                default: break;
            }
        }
        else if (field == "system_status")
        {
            switch (v)
            {
                case 0: return "UNINIT";
                case 1: return "BOOT";
                case 2: return "CALIBRATING";
                case 3: return "STANDBY";
                case 4: return "ACTIVE";
                case 5: return "CRITICAL";
                case 6: return "EMERGENCY";
                default: break;
            }
        }
    }
    return {};
}

/** @brief Keep native combo interaction with a theme-aware vector chevron. */
class WorkbenchComboBox : public QComboBox
{
public:
    /** @brief Use a standard item delegate so popup selection follows theme tokens. */
    explicit WorkbenchComboBox(QWidget *parent = nullptr) : QComboBox(parent)
    {
        setItemDelegate(new QStyledItemDelegate(this));
    }
protected:
    /** @brief Paint a consistent dropdown indicator without an image dependency. */
    void paintEvent(QPaintEvent *event) override
    {
        QComboBox::paintEvent(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const auto group = isEnabled() ? QPalette::Active : QPalette::Disabled;
        painter.setPen(QPen(palette().color(group, QPalette::ButtonText), 1.5,
                            Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const QPointF center(width() - 12.0, height() / 2.0);
        painter.drawLine(center + QPointF(-3, -1.5), center + QPointF(0, 1.5));
        painter.drawLine(center + QPointF(0, 1.5), center + QPointF(3, -1.5));
    }
};

/** @brief Preserve native numeric editing with visible theme-aware step indicators. */
template<typename SpinBox>
class WorkbenchSpinBox : public SpinBox
{
public:
    using SpinBox::SpinBox;
protected:
    /** @brief Draw vector chevrons in the same subcontrols that receive native clicks. */
    void paintEvent(QPaintEvent *event) override
    {
        SpinBox::paintEvent(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QStyleOptionSpinBox option;
        this->initStyleOption(&option);
        for (const auto control : {QStyle::SC_SpinBoxUp, QStyle::SC_SpinBoxDown})
        {
            const bool up = control == QStyle::SC_SpinBoxUp;
            const auto flag = up ? QAbstractSpinBox::StepUpEnabled : QAbstractSpinBox::StepDownEnabled;
            const bool enabled = this->isEnabled() && (this->stepEnabled() & flag);
            const QColor color = this->palette().color(enabled ? QPalette::Active : QPalette::Disabled,
                                                       QPalette::ButtonText);
            painter.setPen(QPen(color, 1.25, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            const QPointF center = this->style()->subControlRect(QStyle::CC_SpinBox, &option, control, this).center();
            const qreal direction = up ? -1 : 1;
            painter.drawLine(center + QPointF(-3, -direction), center + QPointF(0, direction));
            painter.drawLine(center + QPointF(0, direction), center + QPointF(3, -direction));
        }
    }
};

/** @brief Present each TX entry as a readable card backed by the existing row model. */
class TxCardDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    /** @brief Paint identity, sender, counters, rates and state without creating per-row widgets. */
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const auto value = [&index](int column)
        {
            return index.sibling(index.row(), column).data().toString();
        };
        const bool dark = option.widget ? option.widget->property("darkTheme").toBool() : false;
        const bool selected = option.state & QStyle::State_Selected;
        const bool hovered = option.state & QStyle::State_MouseOver;
        const QString text = value(6);
        const bool running = text == "Running";
        const bool error = text != "Stopped" && !running;
        const QColor foreground(dark ? "#edf0fa" : "#20263b");
        const QColor muted(dark ? "#aab4cd" : "#626c84");
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const QRect card = option.rect.adjusted(4, 6, -8, -6);
        painter->setPen(QPen(selected ? QColor("#939bf0") : QColor(dark ? "#465770" : "#c6d1e1"), selected ? 2 : 1));
        painter->setBrush(selected ? QColor(dark ? "#282b4b" : "#f1f2ff") :
                          hovered ? QColor(dark ? "#242c40" : "#f8f9ff") : QColor(dark ? "#1c2435" : "#ffffff"));
        painter->drawRoundedRect(card, 10, 10);
        painter->setPen(QPen(running ? QColor(dark ? "#75cdb0" : "#2e947e") :
                            selected ? QColor("#939bf0") : QColor(dark ? "#647593" : "#a9b8cc"), 3, Qt::SolidLine, Qt::RoundCap));
        painter->drawLine(card.left() + 6, card.top() + 16, card.left() + 6, card.bottom() - 16);
        QFont font = option.font;
        font.setPixelSize(12);
        painter->setFont(font);
        const QRect badge(card.right() - 94, card.top() + 8, 82, 24);
        const QColor badgeBg = error ? QColor(dark ? "#482e3f" : "#fff0f1") :
                               running ? QColor(dark ? "#1d3d36" : "#e7f6ee") : QColor(dark ? "#263042" : "#edf0f6");
        const QColor badgeBorder = error ? QColor(dark ? "#7a3749" : "#f8b4bc") :
                                   running ? QColor(dark ? "#2e685a" : "#bfe6d3") : QColor(dark ? "#3d4c66" : "#d2dae6");
        const QColor badgeFg = error ? QColor(dark ? "#ffb3bd" : "#b42340") :
                               running ? QColor(dark ? "#94e5bd" : "#17613e") : muted;
        painter->setPen(QPen(badgeBorder, 1));
        painter->setBrush(badgeBg);
        painter->drawRoundedRect(badge, 6, 6);
        if (error)
        {
            painter->setPen(badgeFg);
            painter->drawText(badge, Qt::AlignCenter, "Error");
        }
        else if (running)
        {
            painter->fillRect(QRect(badge.left() + 9, badge.center().y() - 3, 7, 7), badgeFg);
            painter->setPen(badgeFg);
            painter->drawText(badge.adjusted(12, 0, 0, 0), Qt::AlignCenter, "Running");
        }
        else
        {
            QPolygonF play;
            play << QPointF(badge.left() + 9, badge.center().y() - 4)
                 << QPointF(badge.left() + 16, badge.center().y())
                 << QPointF(badge.left() + 9, badge.center().y() + 4);
            painter->setPen(Qt::NoPen);
            painter->setBrush(badgeFg);
            painter->drawPolygon(play);
            painter->setPen(badgeFg);
            painter->drawText(badge.adjusted(12, 0, 0, 0), Qt::AlignCenter, "Stopped");
        }
        font.setPixelSize(15);
        font.setBold(true);
        painter->setFont(font);
        painter->setPen(foreground);
        painter->drawText(card.adjusted(16, 9, -104, 0), Qt::AlignTop | Qt::AlignLeft,
                          QFontMetrics(font).elidedText(value(1), Qt::ElideRight, card.width() - 120));
        font.setPixelSize(12);
        font.setBold(false);
        painter->setFont(font);
        painter->setPen(muted);
        painter->drawText(card.adjusted(16, 36, -14, 0), Qt::AlignTop | Qt::AlignLeft,
                          QString("ID %1 · SYS / COMP %2").arg(value(0), value(2)));
        painter->setPen(QPen(QColor(dark ? "#35445f" : "#dde4ef"), 1));
        painter->drawLine(card.left() + 16, card.top() + 59, card.right() - 14, card.top() + 59);
        font.setPixelSize(11);
        painter->setFont(font);
        painter->setPen(foreground);
        const QRect rates = card.adjusted(16, 69, -14, 0);
        const int third = rates.width() / 3;
        painter->drawText(QRect(rates.left(), rates.top(), third, 18), Qt::AlignLeft,
                          "Target " + value(3) + " Hz");
        if (running)
        {
            font.setBold(true);
            painter->setFont(font);
            painter->setPen(QColor(dark ? "#75cdb0" : "#2e947e"));
        }
        painter->drawText(QRect(rates.left() + third, rates.top(), third, 18), Qt::AlignCenter,
                          "Actual " + value(4) + " Hz");
        font.setBold(false);
        painter->setFont(font);
        painter->setPen(muted);
        painter->drawText(QRect(rates.left() + third * 2, rates.top(), rates.width() - third * 2, 18),
                          Qt::AlignRight, "Frames " + value(5));
        painter->restore();
    }
};

/** @brief Give decoded and editable values a distinct visual column without changing data. */
class ValueDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
protected:
    /** @brief Emphasize values while leaving native selection, focus and editing intact. */
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        const bool dark = option->widget ? option->widget->property("darkTheme").toBool() : false;
        option->font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        option->font.setBold(false);
        option->palette.setColor(QPalette::Text, QColor(dark ? "#b2c8e2" : "#3f628b"));
        option->backgroundBrush = QColor(dark ? "#1b2b3e" : "#f4f7fc");
    }
};

/**
 * @brief Create a labeled button with a stable automation identity.
 * @param text Button label text.
 * @param name Automation object name.
 * @param layout Target layout to add the button to.
 * @return Created QPushButton pointer.
 */
static QPushButton *button(const QString &text, const char *name, QBoxLayout *layout)
{
    auto *control = new QPushButton(text);
    control->setObjectName(name);
    layout->addWidget(control);
    return control;
}

/** @brief Configure a read-only, single-row-selection dashboard table. */
static QTableWidget *table(const QStringList &headers)
{
    auto *result = new QTableWidget(0, headers.size());
    result->setHorizontalHeaderLabels(headers);
    result->setEditTriggers(QAbstractItemView::NoEditTriggers);
    result->setSelectionBehavior(QAbstractItemView::SelectRows);
    result->setSelectionMode(QAbstractItemView::SingleSelection);
    result->setAlternatingRowColors(true);
    result->setShowGrid(false);
    result->verticalHeader()->setDefaultSectionSize(32);
    result->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    result->verticalHeader()->hide();
    result->horizontalHeader()->setStretchLastSection(true);
    result->horizontalHeader()->setDefaultSectionSize(110);
    if (headers.value(2) == "Value")
    {
        result->setItemDelegateForColumn(2, new ValueDelegate(result));
    }
    return result;
}

/** @brief Draw a small consistent line icon using local theme colors. */
static QIcon actionIcon(const QString &name, const QColor &color)
{
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(2, 2);
    painter.setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (name == "addMessage")
    {
        painter.drawLine(8, 3, 8, 13);
        painter.drawLine(3, 8, 13, 8);
    }
    else if (name == "startTx" || name == "sendOnce")
    {
        QPainterPath path;
        path.moveTo(5, 3);
        path.lineTo(12, 8);
        path.lineTo(5, 13);
        path.closeSubpath();
        painter.drawPath(path);
    }
    else if (name == "stopTx" || name == "stopAll")
    {
        painter.drawRoundedRect(QRectF(4, 4, 8, 8), 1, 1);
    }
    else if (name == "editMessage")
    {
        QPolygonF pencil;
        pencil << QPointF(3, 10) << QPointF(10, 3) << QPointF(13, 6)
               << QPointF(6, 13) << QPointF(3, 13);
        painter.drawPolygon(pencil);
        painter.drawLine(9, 4, 12, 7);
    }
    else if (name == "removeMessage")
    {
        painter.drawLine(3, 4, 13, 4);
        painter.drawLine(6, 2, 10, 2);
        painter.drawRoundedRect(QRectF(4, 4, 8, 10), 1, 1);
        painter.drawLine(7, 7, 7, 11);
        painter.drawLine(10, 7, 10, 11);
    }
    else if (name == "serialSettings")
    {
        painter.drawLine(QPointF(3, 2), QPointF(3, 4));
        painter.drawLine(QPointF(3, 8), QPointF(3, 14));
        painter.drawRoundedRect(QRectF(1.5, 4, 3, 4), 1, 1);
        painter.drawLine(QPointF(8, 2), QPointF(8, 8));
        painter.drawLine(QPointF(8, 12), QPointF(8, 14));
        painter.drawRoundedRect(QRectF(6.5, 8, 3, 4), 1, 1);
        painter.drawLine(QPointF(13, 2), QPointF(13, 4));
        painter.drawLine(QPointF(13, 8), QPointF(13, 14));
        painter.drawRoundedRect(QRectF(11.5, 4, 3, 4), 1, 1);
    }
    else if (name == "refreshPorts")
    {
        painter.drawArc(QRectF(3, 3, 10, 10), 40 * 16, 280 * 16);
        painter.drawLine(12, 2, 12, 6);
        painter.drawLine(12, 6, 8, 6);
    }
    return QIcon(pixmap);
}

/** @brief Update a cell without destroying selection or replacing its item. */
static void cell(QTableWidget *widget, int row, int column, const QString &text)
{
    auto *item = widget->item(row, column);
    if (!item)
    {
        item = new QTableWidgetItem;
        item->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        widget->setItem(row, column, item);
    }
    if (item->text() != text)
    {
        item->setText(text);
    }
}

/** @brief Resolve a generated message name without fabricating unknown identities. */
static QString messageName(uint32_t id)
{
    const auto *info = mavlink_get_message_info_by_id(id);
    return info ? QString::fromLatin1(info->name) : "Unknown dialect message";
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle("MAVLink Simulator");
    setMinimumSize(1000, 720);
    resize(1440, 900);
    auto *central = new QWidget;
    setCentralWidget(central);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(20, 10, 20, 8);
    root->setSpacing(16);
    auto *heading = new QHBoxLayout;
    auto *title = new QLabel("MAVLink Simulator");
    title->setObjectName("heading");
    title->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    title->setFixedHeight(40);
    heading->addWidget(title, 0, Qt::AlignTop);
    heading->setSpacing(12);

    settingsDialog = new QDialog(this);
    settingsDialog->setObjectName("settingsDialog");
    settingsDialog->setWindowTitle("Settings");
    settingsDialog->setModal(true);
    settingsDialog->setMinimumWidth(380);
    auto *settingsDialogLayout = new QVBoxLayout(settingsDialog);
    settingsDialogLayout->setContentsMargins(20, 20, 20, 16);
    settingsDialogLayout->setSpacing(16);

    auto *dialogTitle = new QLabel("Application Settings", settingsDialog);
    dialogTitle->setObjectName("panelTitle");
    settingsDialogLayout->addWidget(dialogTitle);

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
    themeBox->setFixedWidth(130);
    themeRow->addWidget(themeBox);
    appearanceLayout->addLayout(themeRow);

    settingsDialogLayout->addWidget(appearanceCard);
    settingsDialogLayout->addStretch();

    auto *settingsButtonBox = new QDialogButtonBox(settingsDialog);
    settingsButtonBox->setObjectName("settingsButtonBox");
    auto *closeButton = settingsButtonBox->addButton("Close", QDialogButtonBox::RejectRole);
    closeButton->setObjectName("closeSettings");
    closeButton->setIcon(QIcon());
    connect(settingsButtonBox, &QDialogButtonBox::rejected, settingsDialog, &QDialog::accept);
    settingsDialogLayout->addWidget(settingsButtonBox);

    auto *appMenuBar = menuBar();
    appMenuBar->setObjectName("appMenuBar");

    auto *fileMenu = appMenuBar->addMenu("File");
    fileMenu->setObjectName("fileMenu");
    auto *exitAction = fileMenu->addAction("Exit", this, &QWidget::close, QKeySequence::Quit);
    exitAction->setObjectName("actionExit");

    auto *viewMenu = appMenuBar->addMenu("View");
    viewMenu->setObjectName("viewMenu");

    auto *toolMenu = appMenuBar->addMenu("Tool");
    toolMenu->setObjectName("toolMenu");
    auto *settingsAction = toolMenu->addAction("Settings...", settingsDialog, &QDialog::exec, QKeySequence::Preferences);
    settingsAction->setObjectName("actionSettings");

    auto *helpMenu = appMenuBar->addMenu("Help");
    helpMenu->setObjectName("helpMenu");
    auto *aboutAction = helpMenu->addAction("About MAVLink Simulator", this, [this]()
    {
        QMessageBox::about(this, "About MAVLink Simulator",
                           "<h3>MAVLink Simulator</h3>"
                           "<p>Desktop serial workbench for Ubuntu/Linux.</p>"
                           "<p>Dialect: ArduPilotMega / Common</p>");
    });
    aboutAction->setObjectName("actionAbout");
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
    portBox->setEditable(true);
    portBox->lineEdit()->setPlaceholderText("Select or enter /dev/…");
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
    baudBox->setFixedWidth(116);
    baudBox->addItems({"9600", "19200", "38400", "57600", "115200", "230400", "460800", "921600"});
    baudBox->setObjectName("baudRate");
    baudBox->setEditable(true);
    baudBox->setCurrentText("115200");
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

    auto *connectionCard = new QFrame;
    connectionCard->setObjectName("connectionCard");
    auto *cardLayout = new QVBoxLayout(connectionCard);
    cardLayout->setContentsMargins(8, 0, 0, 0);
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
    serialMenu->setAttribute(Qt::WA_TranslucentBackground);
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
        connect(combo, &QComboBox::currentIndexChanged, this, &MainWindow::updateSerialSummary);
    }
    updateSerialSummary();
    uart->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    uart->setMaximumWidth(700);
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
    txGroup->setMinimumWidth(360);
    txGroup->setObjectName("dashboardPanel");
    auto *txLayout = new QVBoxLayout(txGroup);
    txLayout->setContentsMargins(16, 16, 16, 12);
    txLayout->setSpacing(12);
    auto *txHeader = new QHBoxLayout;
    auto *txTitle = new QLabel("TX Dashboard");
    txTitle->setObjectName("panelTitle");
    txHeader->addWidget(txTitle);
    txHeader->addStretch();
    auto *addButton = button("Add message", "addMessage", txHeader);
    addButton->setProperty("role", "primary");
    txLayout->addLayout(txHeader);
    txSummary = new QLabel("0 messages  ·  0 running");
    txSummary->setObjectName("secondaryText");
    auto *txOverview = new QHBoxLayout;
    txOverview->addWidget(txSummary);
    txOverview->addStretch();
    stopAllButton = button("Stop all", "stopAll", txOverview);
    stopAllButton->setProperty("role", "danger");
    txLayout->addLayout(txOverview);
    auto *txDivider = new QFrame;
    txDivider->setObjectName("txDivider");
    txDivider->setFixedHeight(1);
    txLayout->addWidget(txDivider);
    txTable = table({"ID", "Message", "Sys / Comp", "Target Hz", "Actual Hz", "Queued", "Status"});
    txTable->setObjectName("txTable");
    txTable->setItemDelegateForColumn(1, new TxCardDelegate(txTable));
    txTable->horizontalHeader()->hide();
    txTable->horizontalHeader()->setStretchLastSection(false);
    txTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    for (int column : {0, 2, 3, 4, 5, 6})
    {
        txTable->hideColumn(column);
    }
    txTable->verticalHeader()->setDefaultSectionSize(108);
    txTable->setAlternatingRowColors(false);
    txTable->setMouseTracking(true);
    txTable->viewport()->setMouseTracking(true);
    txTable->viewport()->installEventFilter(this);
    txTable->setAccessibleName("Transmit message cards");
    txLayout->addWidget(txTable, 1);
    auto *sendActions = new QHBoxLayout;
    sendActions->setSpacing(6);
    onceButton = button("Send once", "sendOnce", sendActions);
    startButton = button("Start", "startTx", sendActions);
    startButton->setProperty("role", "success");
    stopButton = button("Stop", "stopTx", sendActions);
    stopButton->setProperty("role", "danger");
    sendActions->addStretch();
    editButton = button("", "editMessage", sendActions);
    removeButton = button("", "removeMessage", sendActions);
    for (auto *control : {editButton, removeButton})
    {
        control->setFixedSize(32, 32);
    }
    editButton->setAccessibleName("Edit selected message");
    editButton->setToolTip("Edit selected message");
    removeButton->setAccessibleName("Remove selected message");
    removeButton->setToolTip("Remove selected message");
    txLayout->addLayout(sendActions);
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
    rxHeader->addWidget(rxSummary);
    rxHeader->addStretch();
    rxLayout->addLayout(rxHeader);
    auto *rxActions = new QHBoxLayout;
    rxFilter = new QLineEdit;
    rxFilter->setObjectName("rxFilter");
    rxFilter->setPlaceholderText("Search messages or sender…");
    rxFilter->setClearButtonEnabled(true);
    rxFilter->setMinimumWidth(140);
    rxFilter->setAccessibleName("Filter received MAVLink streams");
    rxActions->addWidget(rxFilter);
    auto *pause = button("Pause display", "pauseDisplay", rxActions);
    pause->setCheckable(true);
    auto *clear = button("Clear", "clearStatistics", rxActions);
    clear->setToolTip("Clear received messages and statistics");
    rxLayout->addLayout(rxActions);

    auto *pauseAction = viewMenu->addAction("Pause Display", this, [pause]()
    {
        pause->toggle();
    });
    pauseAction->setObjectName("actionPauseDisplay");
    pauseAction->setCheckable(true);
    connect(pause, &QPushButton::toggled, pauseAction, &QAction::setChecked);

    auto *clearAction = viewMenu->addAction("Clear Received Messages", clear, &QPushButton::click);
    clearAction->setObjectName("actionClearDisplay");
    auto *rxSplit = new QSplitter(Qt::Vertical);
    rxSplit->setObjectName("rxSplit");
    rxSplit->setChildrenCollapsible(false);
    rxSplit->setHandleWidth(16);
    rxTree = new QTreeWidget;
    rxTree->setObjectName("rxTree");
    rxTree->setHeaderLabels({"Message / Source", "ID", "Count", "Hz", "Activity"});
    rxTree->header()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    rxTree->headerItem()->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
    rxTree->headerItem()->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
    rxTree->headerItem()->setTextAlignment(3, Qt::AlignRight | Qt::AlignVCenter);
    rxTree->headerItem()->setTextAlignment(4, Qt::AlignLeft | Qt::AlignVCenter);
    rxTree->setUniformRowHeights(true);
    rxTree->setAlternatingRowColors(false);
    rxTree->setSelectionMode(QAbstractItemView::SingleSelection);
    rxTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    rxTree->setColumnWidth(1, 52);
    rxTree->setColumnWidth(2, 75);
    rxTree->setColumnWidth(3, 58);
    rxTree->setColumnWidth(4, 115);
    inspector = table({"Field", "Type", "Value", "Unit"});
    inspector->setObjectName("inspector");
    inspector->setContextMenuPolicy(Qt::CustomContextMenu);
    auto *valueMenu = new QMenu(inspector);
    valueMenu->setObjectName("inspectorMenu");
    auto *copyValue = valueMenu->addAction("Copy value");
    copyValue->setObjectName("copyInspectorValue");
    connect(copyValue, &QAction::triggered, this, [copyValue]()
    {
        QApplication::clipboard()->setText(copyValue->data().toString());
    });
    connect(inspector, &QWidget::customContextMenuRequested, this, [this, valueMenu, copyValue](const QPoint &position)
    {
        const bool keyboard = position.x() < 0 || position.y() < 0;
        const int row = keyboard ? inspector->currentRow() : inspector->indexAt(position).row();
        auto *value = inspector->item(row, 2);
        if (!value)
        {
            return;
        }
        inspector->setCurrentCell(row, 2);
        copyValue->setData(value->text());
        const QPoint anchor = keyboard ? inspector->visualItemRect(value).center() : position;
        valueMenu->popup(inspector->viewport()->mapToGlobal(anchor));
    });
    inspector->setColumnWidth(0, 150);
    inspector->setColumnWidth(1, 90);
    inspector->setColumnWidth(2, 220);
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
    inspectorTitle = addInspectorPane("DECODED FIELDS", inspector);
    rxSplit->setSizes({300, 300});
    rxLayout->addWidget(rxSplit, 1);
    dashboards->addWidget(rxGroup);
    dashboards->setSizes({400, 940});
    dashboards->setStretchFactor(0, 0);
    dashboards->setStretchFactor(1, 1);
    workspace->addWidget(dashboards, 1);
    metrics = new QLabel("RX 0 B/s &nbsp;|&nbsp; TX 0 B/s &nbsp;|&nbsp; CRC errors 0 &nbsp;|&nbsp; Pending 0 B &nbsp;|&nbsp; RX overflow 0");
    metrics->setTextFormat(Qt::RichText);
    metrics->setContentsMargins(18, 0, 0, 0);
    statusBar()->addWidget(metrics);
    lastError = new QLabel("Ready");
    lastError->setMaximumWidth(560);
    lastError->setWordWrap(true);
    statusBar()->addPermanentWidget(lastError, 1);

    qRegisterMetaType<PortConfig>();
    qRegisterMetaType<Mav::TxEntry>();
    qRegisterMetaType<SnapshotPtr>();
    worker = new SerialWorker;
    worker->moveToThread(&thread);
    connect(&thread, &QThread::started, worker, &SerialWorker::initialize);
    connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &SerialWorker::stateChanged, this, [this, refreshPortsButton](const QString &state, bool open)
    {
        connected = open;
        connecting = state == "Connecting";
        connectButton->setText(open ? "Disconnect" : "Connect");
        connectButton->setProperty("connected", open);
        connectButton->style()->unpolish(connectButton);
        connectButton->style()->polish(connectButton);
        connectButton->setEnabled(!connecting);
        refreshPortsButton->setEnabled(!open && !connecting);
        for (auto *combo : {portBox, baudBox, dataBox, parityBox, stopBox, flowBox})
        {
            combo->setEnabled(!open && !connecting);
        }
        if (open)
        {
            rxTree->clear();
            inspector->setRowCount(0);
            lastError->setText("Connected");
        }
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
    connect(editButton, &QPushButton::clicked, this, [this]()
    {
        editMessage(true);
    });
    connect(removeButton, &QPushButton::clicked, this, [this]()
    {
        QMetaObject::invokeMethod(worker, "removeEntry", Qt::QueuedConnection, Q_ARG(quint64, selectedTx()));
    });
    connect(onceButton, &QPushButton::clicked, this, [this]()
    {
        QMetaObject::invokeMethod(worker, "sendOnce", Qt::QueuedConnection, Q_ARG(quint64, selectedTx()));
    });
    connect(startButton, &QPushButton::clicked, this, [this]()
    {
        QMetaObject::invokeMethod(worker, "setActive", Qt::QueuedConnection,
                                  Q_ARG(quint64, selectedTx()), Q_ARG(bool, true));
    });
    connect(stopButton, &QPushButton::clicked, this, [this]()
    {
        QMetaObject::invokeMethod(worker, "setActive", Qt::QueuedConnection,
                                  Q_ARG(quint64, selectedTx()), Q_ARG(bool, false));
    });
    connect(stopAllButton, &QPushButton::clicked, worker, &SerialWorker::stopAll);
    connect(clear, &QPushButton::clicked, this, [this]()
    {
        QMetaObject::invokeMethod(worker, "clearStatistics", Qt::QueuedConnection);
        rxTree->clear();
        inspector->setRowCount(0);
        inspectorTitle->setText("DECODED FIELDS");
        rxSummary->setText("0 streams");
    });
    connect(pause, &QPushButton::toggled, this, [this, pause](bool checked)
    {
        paused = checked;
        pause->setText(checked ? "Resume display" : "Pause display");
    });
    connect(rxFilter, &QLineEdit::textChanged, this, &MainWindow::filterRx);
    connect(rxTree, &QTreeWidget::itemSelectionChanged, this, &MainWindow::updateInspector);
    connect(txTable, &QTableWidget::itemSelectionChanged, this, &MainWindow::updateActions);
    connect(txTable, &QTableWidget::cellDoubleClicked, this, [this]()
    {
        editMessage(true);
    });
    connect(themeBox, &QComboBox::currentIndexChanged, this, &MainWindow::applyTheme);
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
    applyTheme();
    updateActions();
}

MainWindow::~MainWindow()
{
    refresh.stop();
    QMetaObject::invokeMethod(worker, "closePort", Qt::BlockingQueuedConnection);
    thread.quit();
    thread.wait();
}

void MainWindow::refreshPorts()
{
    if (connected || connecting)
    {
        return;
    }
    const QString previous = portBox->currentText();
    portBox->clear();
    for (const auto &info : QSerialPortInfo::availablePorts())
    {
        if (isPluggedPort(info))
        {
            portBox->addItem(info.systemLocation());
            portBox->setItemData(portBox->count() - 1,
                                 info.description() + " · " + info.manufacturer(), Qt::ToolTipRole);
        }
    }
    if (!previous.isEmpty())
    {
        portBox->setCurrentText(previous);
    }
}

void MainWindow::toggleConnection()
{
    if (connected)
    {
        QMetaObject::invokeMethod(worker, "closePort", Qt::QueuedConnection);
        return;
    }
    bool ok = false;
    PortConfig config;
    config.port = portBox->currentText().trimmed();
    config.baud = baudBox->currentText().toInt(&ok);
    if (!ok || config.baud <= 0 || config.baud > 12000000 || config.port.isEmpty())
    {
        showError("Select a port and a baud rate from 1 to 12000000");
        return;
    }
    config.data = static_cast<QSerialPort::DataBits>(dataBox->currentText().toInt());
    const QSerialPort::Parity parities[] = {QSerialPort::NoParity, QSerialPort::EvenParity,
                                          QSerialPort::OddParity, QSerialPort::MarkParity, QSerialPort::SpaceParity};
    config.parity = parities[parityBox->currentIndex()];
    config.stop = stopBox->currentIndex() == 0 ? QSerialPort::OneStop :
                  stopBox->currentIndex() == 1 ? QSerialPort::TwoStop : QSerialPort::OneAndHalfStop;
    config.flow = static_cast<QSerialPort::FlowControl>(flowBox->currentIndex());
    connectButton->setEnabled(false);
    QMetaObject::invokeMethod(worker, "openPort", Qt::QueuedConnection, Q_ARG(PortConfig, config));
}

uint64_t MainWindow::selectedTx() const
{
    const auto row = txTable->currentRow();
    return row < 0 || !txTable->item(row, 0) ? 0 : txTable->item(row, 0)->data(Qt::UserRole).toULongLong();
}

void MainWindow::updateActions()
{
    const bool selected = selectedTx() != 0;
    editButton->setEnabled(selected);
    removeButton->setEnabled(selected);
    onceButton->setEnabled(selected && connected);
    startButton->setEnabled(selected && connected);
    stopButton->setEnabled(selected && connected);
    stopAllButton->setEnabled(connected);
    const int row = txTable->currentRow();
    const bool running = selected && txTable->item(row, 6) && txTable->item(row, 6)->text() == "Running";
    startButton->setVisible(!running);
    stopButton->setVisible(running);

}

void MainWindow::updateSerialSummary()
{
    const QString parity = parityBox->currentIndex() == 0 ? "N" : parityBox->currentText().left(1);
    serialSummary->setText(dataBox->currentText() + parity + stopBox->currentText() + " · No flow");
    serialSummary->setToolTip(QString("%1 data bits · %2 parity · %3 stop bit(s) · %4 flow control")
                              .arg(dataBox->currentText(), parityBox->currentText(),
                                   stopBox->currentText(), flowBox->currentText()));
    if (flowBox->currentIndex() != 0)
    {
        serialSummary->setText(dataBox->currentText() + parity + stopBox->currentText() + " · " +
                              (flowBox->currentIndex() == 1 ? "RTS/CTS" : "XON/XOFF"));
    }
}

void MainWindow::editMessage(bool edit)
{
    Mav::TxEntry entry;
    if (edit)
    {
        if (!latest)
        {
            return;
        }
        const auto key = selectedTx();
        const auto found = std::find_if(latest->tx.begin(), latest->tx.end(), [&](const auto &item)
        {
            return item.entry.key == key;
        });
        if (found == latest->tx.end())
        {
            return;
        }
        entry = found->entry;
    }
    else
    {
        entry.key = nextKey++;
    }
    QDialog dialog(this);
    dialog.setObjectName("messageDialog");
    dialog.setWindowTitle(edit ? "Edit message · Saving stops this entry" : "Add MAVLink message");
    dialog.setMinimumSize(880, 600);
    dialog.resize(1000, 680);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(12);
    auto *title = new QLabel(edit ? "Edit message" : "Add message");
    title->setObjectName("heading");
    layout->addWidget(title);
    auto *description = new QLabel(edit ? "Saving stops this TX entry. Review its payload before restarting." :
                                  "Choose a MAVLink message, set its sender and rate, then edit the payload.");
    description->setObjectName("secondaryText");
    layout->addWidget(description);
    auto *body = new QHBoxLayout;
    body->setSpacing(20);
    auto *catalogPane = new QVBoxLayout;
    auto *catalogTitle = new QLabel("MESSAGE CATALOG");
    catalogTitle->setObjectName("sectionTitle");
    catalogPane->addWidget(catalogTitle);
    auto *search = new QLineEdit;
    search->setPlaceholderText("Search message ID or name…");
    search->setAccessibleName("Search MAVLink catalog");
    catalogPane->addWidget(search);
    auto *catalogList = new QListWidget;
    catalogList->setObjectName("messageCatalog");
    catalogList->setMinimumWidth(255);
    catalogList->setMaximumWidth(290);
    for (const auto &info : Mav::catalog())
    {
        auto *item = new QListWidgetItem(QString("%1  ·  %2").arg(info.msgid).arg(info.name), catalogList);
        item->setData(Qt::UserRole, info.msgid);
        item->setToolTip(item->text());
    }
    catalogPane->addWidget(catalogList, 1);
    body->addLayout(catalogPane);
    auto *editor = new QVBoxLayout;
    editor->setSpacing(10);
    auto *senderTitle = new QLabel("SENDER & TIMING");
    senderTitle->setObjectName("sectionTitle");
    editor->addWidget(senderTitle);
    auto *form = new QHBoxLayout;
    auto *sysid = new WorkbenchSpinBox<QSpinBox>;
    auto *compid = new WorkbenchSpinBox<QSpinBox>;
    sysid->setRange(1, 255);
    compid->setRange(0, 255);
    sysid->setValue(entry.sysid);
    compid->setValue(entry.compid);
    auto *version = new WorkbenchComboBox;
    version->addItems({"MAVLink 2", "MAVLink 1"});
    version->setCurrentIndex(entry.version1 ? 1 : 0);
    auto *rate = new WorkbenchSpinBox<QDoubleSpinBox>;
    rate->setRange(0.1, 1000);
    rate->setDecimals(1);
    rate->setValue(entry.hz);
    const auto fieldControl = [&](const QString &name, QWidget *control)
    {
        auto *field = new QFormLayout;
        field->setRowWrapPolicy(QFormLayout::WrapAllRows);
        field->addRow(name, control);
        control->setAccessibleName(name);
        form->addLayout(field);
    };
    fieldControl("System ID", sysid);
    fieldControl("Component ID", compid);
    fieldControl("Protocol", version);
    fieldControl("Rate (Hz)", rate);
    editor->addLayout(form);
    auto *payloadTitle = new QLabel("PAYLOAD · Click a value to edit");
    payloadTitle->setObjectName("secondaryText");
    editor->addWidget(payloadTitle);
    auto *fields = table({"Field", "Type", "Value"});
    fields->setObjectName("payloadFields");
    fields->setEditTriggers(QAbstractItemView::AllEditTriggers);
    fields->setColumnWidth(0, 190);
    fields->setColumnWidth(1, 105);
    editor->addWidget(fields, 1);
    body->addLayout(editor, 1);
    layout->addLayout(body, 1);
    auto *hint = new QLabel("Numeric arrays: comma-separated values, exact element count. char arrays: UTF-8 text. Integers: decimal. Units are unavailable in generated metadata.");
    hint->setWordWrap(true);
    hint->setObjectName("secondaryText");
    layout->addWidget(hint);
    auto *error = new QLabel;
    error->setObjectName("validationError");
    error->setWordWrap(true);
    layout->addWidget(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setProperty("role", "primary");
    buttons->button(QDialogButtonBox::Save)->setMinimumWidth(100);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    const mavlink_message_info_t *selectedInfo = nullptr;
    connect(catalogList, &QListWidget::currentItemChanged, &dialog, [&](QListWidgetItem *item)
    {
        selectedInfo = item ? mavlink_get_message_info_by_id(item->data(Qt::UserRole).toUInt()) : nullptr;
        fields->setRowCount(0);
        if (!selectedInfo)
        {
            return;
        }
        mavlink_message_t source{};
        source.msgid = selectedInfo->msgid;
        if (edit && source.msgid == entry.id)
        {
            source.len = uint8_t(entry.payload.size());
            std::copy(entry.payload.begin(), entry.payload.end(), reinterpret_cast<uint8_t *>(_MAV_PAYLOAD_NON_CONST(&source)));
        }
        fields->setRowCount(int(selectedInfo->num_fields));
        for (unsigned i = 0; i < selectedInfo->num_fields; ++i)
        {
            const auto &field = selectedInfo->fields[i];
            cell(fields, int(i), 0, field.name);
            QString type = QString::fromStdString(Mav::typeName(field.type));
            if (field.array_length)
            {
                type += QString("[%1]").arg(field.array_length);
            }
            cell(fields, int(i), 1, type);
            cell(fields, int(i), 2, QString::fromStdString(Mav::readField(source, field)));
            if (selectedInfo->msgid == MAVLINK_MSG_ID_HEARTBEAT && qstrcmp(field.name, "mavlink_version") == 0)
            {
                fields->item(int(i), 2)->setText("3");
                fields->item(int(i), 2)->setFlags(fields->item(int(i), 2)->flags() & ~Qt::ItemIsEditable);
                fields->item(int(i), 2)->setToolTip("Protocol constant supplied by the official HEARTBEAT packer");
            }
            for (int column = 0; column < 2; ++column)
            {
                fields->item(int(i), column)->setFlags(fields->item(int(i), column)->flags() & ~Qt::ItemIsEditable);
            }
        }
    });
    connect(search, &QLineEdit::textChanged, &dialog, [=](const QString &query)
    {
        for (int i = 0; i < catalogList->count(); ++i)
        {
            auto *item = catalogList->item(i);
            item->setHidden(!item->text().contains(query, Qt::CaseInsensitive));
        }
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]()
    {
        // Commit an active table editor before reading its value.
        buttons->setFocus();
        if (!selectedInfo)
        {
            error->setText("Select a message");
            return;
        }
        const auto *metadata = mavlink_get_msg_entry(selectedInfo->msgid);
        entry.id = selectedInfo->msgid;
        entry.payload.assign(metadata->max_msg_len, 0);
        entry.sysid = uint8_t(sysid->value());
        entry.compid = uint8_t(compid->value());
        entry.version1 = version->currentIndex() == 1;
        entry.hz = rate->value();
        for (unsigned i = 0; i < selectedInfo->num_fields; ++i)
        {
            std::string reason;
            if (!Mav::setField(entry.payload, selectedInfo->fields[i],
                              fields->item(int(i), 2)->text().toUtf8().toStdString(), reason))
            {
                error->setText(QString("%1: %2").arg(selectedInfo->fields[i].name, QString::fromStdString(reason)));
                fields->setCurrentCell(int(i), 2);
                fields->setFocus();
                return;
            }
        }
        if (!Mav::validTx(entry))
        {
            error->setText("MAVLink 1 supports message IDs 0–255 only");
            return;
        }
        dialog.accept();
    });
    int initial = 0;
    for (int i = 0; edit && i < catalogList->count(); ++i)
    {
        if (catalogList->item(i)->data(Qt::UserRole).toUInt() == entry.id)
        {
            initial = i;
            break;
        }
    }
    catalogList->setCurrentRow(initial);
    if (dialog.exec() == QDialog::Accepted)
    {
        QMetaObject::invokeMethod(worker, "saveEntry", Qt::QueuedConnection, Q_ARG(Mav::TxEntry, entry));
    }
}

void MainWindow::applySnapshot(SnapshotPtr snapshot)
{
    snapshotPending = false;
    latest = std::move(snapshot);
    const auto running = std::count_if(latest->tx.begin(), latest->tx.end(), [](const auto &view)
    {
        return view.entry.active;
    });
    txSummary->setText(QString("%1 %2  ·  %3 running").arg(latest->tx.size())
                       .arg(latest->tx.size() == 1 ? "message" : "messages").arg(running));
    if (!paused)
    {
        rxSummary->setText(QString("%1 %2").arg(latest->rx.size())
                           .arg(latest->rx.size() == 1 ? "stream" : "streams"));
    }
    const uint64_t selected = selectedTx();
    txTable->setRowCount(int(latest->tx.size()));
    for (size_t i = 0; i < latest->tx.size(); ++i)
    {
        const auto &view = latest->tx[i];
        const auto &entry = view.entry;
        const int row = int(i);
        cell(txTable, row, 0, QString::number(entry.id));
        txTable->item(row, 0)->setData(Qt::UserRole, qulonglong(entry.key));
        cell(txTable, row, 1, messageName(entry.id));
        cell(txTable, row, 2, QString("%1 / %2").arg(entry.sysid).arg(entry.compid));
        cell(txTable, row, 3, QString::number(entry.hz, 'f', 1));
        cell(txTable, row, 4, QString::number(view.measuredHz, 'f', 1));
        cell(txTable, row, 5, QString::number(qulonglong(entry.count)));
        cell(txTable, row, 6, entry.error.empty() ? (entry.active ? "Running" : "Stopped") : QString::fromStdString(entry.error));
        txTable->item(row, 6)->setToolTip(txTable->item(row, 6)->text());
        const QString description = QString("%1 · ID %2\nSystem / Component %3\nTarget %4 Hz · Actual %5 Hz\n%6 frames handed to Qt output\n%7")
                                    .arg(messageName(entry.id), QString::number(entry.id), txTable->item(row, 2)->text(),
                                         txTable->item(row, 3)->text(), txTable->item(row, 4)->text(),
                                         txTable->item(row, 5)->text(), txTable->item(row, 6)->text());
        txTable->item(row, 1)->setToolTip(description);
        txTable->item(row, 1)->setData(Qt::AccessibleTextRole, description);
        if (entry.key == selected && txTable->currentRow() != row)
        {
            const int scroll = txTable->verticalScrollBar()->value();
            txTable->setCurrentCell(row, 1);
            txTable->verticalScrollBar()->setValue(scroll);
        }
    }
    const bool dark = themeBox->currentIndex() == 2 ||
                      (themeBox->currentIndex() == 0 && qApp->palette().color(QPalette::Window).lightness() < 128);
    const QString alertColor = dark ? "#f87171" : "#dc2626";
    const QString warnColor = dark ? "#fbbf24" : "#d97706";
    const QString textCrc = latest->crcErrors > 0 ?
        QString("<span style='color:%1; font-weight:bold;'>CRC errors %2</span>").arg(alertColor).arg(qulonglong(latest->crcErrors)) :
        QString("CRC errors %1").arg(qulonglong(latest->crcErrors));
    const QString textPending = latest->pending > 4096 ?
        QString("<span style='color:%1; font-weight:bold;'>Pending %2 B</span>").arg(warnColor).arg(qulonglong(latest->pending)) :
        QString("Pending %1 B").arg(qulonglong(latest->pending));
    const QString textOverflow = latest->overflow > 0 ?
        QString("<span style='color:%1; font-weight:bold;'>RX overflow %2</span>").arg(alertColor).arg(qulonglong(latest->overflow)) :
        QString("RX overflow %1").arg(qulonglong(latest->overflow));
    metrics->setText(QString("RX %1 B/s &nbsp;|&nbsp; TX %2 B/s &nbsp;|&nbsp; %3 &nbsp;|&nbsp; %4 &nbsp;|&nbsp; %5")
                    .arg(latest->rxBytesPerSecond, 0, 'f', 0)
                    .arg(latest->txBytesPerSecond, 0, 'f', 0)
                    .arg(textCrc, textPending, textOverflow));
    if (!paused)
    {
        for (const auto &entry : latest->rx)
        {
            const auto &message = entry.message;
            const QString systemKey = QString::number(message.sysid);
            QTreeWidgetItem *system = nullptr;
            for (int i = 0; i < rxTree->topLevelItemCount(); ++i)
            {
                if (rxTree->topLevelItem(i)->data(0, Qt::UserRole).toString() == systemKey)
                {
                    system = rxTree->topLevelItem(i);
                    break;
                }
            }
            if (!system)
            {
                system = new QTreeWidgetItem(rxTree, {"System " + systemKey});
                system->setData(0, Qt::UserRole, systemKey);
                system->setExpanded(true);
            }
            QTreeWidgetItem *component = nullptr;
            for (int i = 0; i < system->childCount(); ++i)
            {
                if (system->child(i)->data(0, Qt::UserRole).toUInt() == message.compid)
                {
                    component = system->child(i);
                    break;
                }
            }
            if (!component)
            {
                component = new QTreeWidgetItem(system, {QString("Component %1").arg(message.compid)});
                component->setData(0, Qt::UserRole, message.compid);
                component->setExpanded(true);
            }
            const QColor groupColor(dark ? "#aab4cd" : "#626c84");
            system->setForeground(0, groupColor);
            component->setForeground(0, groupColor);
            QTreeWidgetItem *item = nullptr;
            for (int i = 0; i < component->childCount(); ++i)
            {
                if (component->child(i)->data(0, Qt::UserRole).toUInt() == message.msgid)
                {
                    item = component->child(i);
                    break;
                }
            }
            if (!item)
            {
                item = new QTreeWidgetItem(component);
                item->setData(0, Qt::UserRole, message.msgid);
                const auto fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
                for (int col = 0; col < 5; ++col)
                {
                    item->setFont(col, fixedFont);
                }
                item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
                item->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
                item->setTextAlignment(3, Qt::AlignRight | Qt::AlignVCenter);
                item->setTextAlignment(4, Qt::AlignLeft | Qt::AlignVCenter);
            }
            item->setIcon(4, activityLed(entry.age, dark));
            item->setText(0, messageName(message.msgid));
            item->setText(1, QString::number(message.msgid));
            item->setText(2, QString::number(qulonglong(entry.count)));
            item->setText(3, QString::number(entry.hz, 'f', 1));
            item->setText(4, QString("%1 s · %2").arg(entry.age, 0, 'f', 1).arg(entry.age > 2 ? "Stale" : "Recent"));
            item->setToolTip(0, (message.incompat_flags & MAVLINK_IFLAG_SIGNED) ?
                              "Signed frame; signature NOT verified" : "Unsigned frame; no authentication");
        }
        filterRx();
        updateInspector();
    }
    updateActions();
}

void MainWindow::updateInspector()
{
    if (paused || !latest)
    {
        return;
    }
    inspectorTitle->setText("DECODED FIELDS");
    auto *item = rxTree->currentItem();
    if (!item || !item->parent() || !item->parent()->parent())
    {
        inspector->setRowCount(0);
        return;
    }
    const uint32_t id = item->data(0, Qt::UserRole).toUInt();
    const uint8_t component = uint8_t(item->parent()->data(0, Qt::UserRole).toUInt());
    const uint32_t system = item->parent()->parent()->data(0, Qt::UserRole).toUInt();
    for (const auto &view : latest->rx)
    {
        const auto &message = view.message;
        if (message.sysid != system || message.compid != component || message.msgid != id)
        {
            continue;
        }
        const auto *info = mavlink_get_message_info_by_id(id);
        if (!info)
        {
            inspector->setRowCount(0);
            return;
        }
        inspector->setRowCount(int(info->num_fields));
        inspectorTitle->setText(QString("%1 · ID %2 · SYS %3 / COMP %4")
                                .arg(messageName(id)).arg(id).arg(system).arg(component));
        const bool dark = themeBox->currentIndex() == 2 ||
                          (themeBox->currentIndex() == 0 && qApp->palette().color(QPalette::Window).lightness() < 128);
        for (unsigned i = 0; i < info->num_fields; ++i)
        {
            const auto &field = info->fields[i];
            cell(inspector, int(i), 0, field.name);
            QString type = QString::fromStdString(Mav::typeName(field.type));
            if (field.array_length)
            {
                type += QString("[%1]").arg(field.array_length);
            }
            cell(inspector, int(i), 1, type);
            const QString valStr = QString::fromStdString(Mav::readField(message, field));
            const QString fieldKey = QString("%1.%2.%3.%4").arg(system).arg(component).arg(id).arg(field.name);
            const auto prevIt = lastFieldValues.find(fieldKey);
            const bool changed = (prevIt != lastFieldValues.end()) && (prevIt->second != valStr);
            lastFieldValues[fieldKey] = valStr;
            cell(inspector, int(i), 2, valStr);
            auto *valItem = inspector->item(int(i), 2);
            if (changed)
            {
                valItem->setForeground(QColor(dark ? "#58a6ff" : "#0969da"));
            }
            else
            {
                valItem->setForeground(QColor(dark ? "#b2c8e2" : "#3f628b"));
            }
            const QString hint = mavlinkEnumHint(id, QString::fromLatin1(field.name), valStr);
            valItem->setToolTip(hint.isEmpty() ? valStr : valStr + " · " + hint);
            cell(inspector, int(i), 3, "Unavailable");
        }
        return;
    }
}

void MainWindow::filterRx()
{
    const auto query = rxFilter->text().trimmed();
    for (int i = 0; i < rxTree->topLevelItemCount(); ++i)
    {
        auto *system = rxTree->topLevelItem(i);
        bool systemVisible = false;
        for (int j = 0; j < system->childCount(); ++j)
        {
            auto *component = system->child(j);
            bool componentVisible = false;
            for (int k = 0; k < component->childCount(); ++k)
            {
                auto *item = component->child(k);
                const QString identity = system->text(0) + " " + component->text(0) + " " + item->text(0) + " " + item->text(1);
                const bool visible = identity.contains(query, Qt::CaseInsensitive);
                item->setHidden(!visible);
                componentVisible |= visible;
            }
            component->setHidden(!componentVisible);
            systemVisible |= componentVisible;
        }
        system->setHidden(!systemVisible);
    }
}

void MainWindow::showError(const QString &message)
{
    lastError->setText("Error: " + message);
    lastError->setToolTip(message);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched->objectName() == "serialMenu" &&
        (event->type() == QEvent::Resize || event->type() == QEvent::Show))
    {
        auto *popup = qobject_cast<QMenu *>(watched);
        if (popup)
        {
            QPainterPath outline;
            outline.addRoundedRect(QRectF(popup->rect()), 7, 7);
            popup->setMask(QRegion(outline.toFillPolygon().toPolygon()));
        }
    }
    if (watched == qApp && event->type() == QEvent::ApplicationPaletteChange &&
        themeBox && themeBox->currentIndex() == 0)
    {
        applyTheme();
    }
    if (txTable && watched == txTable->viewport())
    {
        if (event->type() == QEvent::MouseButtonRelease)
        {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton && connected)
            {
                const QModelIndex index = txTable->indexAt(mouseEvent->pos());
                if (index.isValid() && index.column() == 1)
                {
                    const QRect rect = txTable->visualRect(index);
                    const QRect card = rect.adjusted(4, 6, -8, -6);
                    const QRect badge(card.right() - 94, card.top() + 8, 82, 24);
                    if (badge.contains(mouseEvent->pos()))
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
        else if (event->type() == QEvent::MouseMove)
        {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            const QModelIndex index = txTable->indexAt(mouseEvent->pos());
            if (index.isValid() && index.column() == 1)
            {
                const QRect rect = txTable->visualRect(index);
                const QRect card = rect.adjusted(4, 6, -8, -6);
                const QRect badge(card.right() - 94, card.top() + 8, 82, 24);
                txTable->viewport()->setCursor(badge.contains(mouseEvent->pos()) && connected ?
                                               Qt::PointingHandCursor : Qt::ArrowCursor);
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

void MainWindow::applyTheme()
{
    const bool dark = themeBox->currentIndex() == 2 ||
                      (themeBox->currentIndex() == 0 && qApp->palette().color(QPalette::Window).lightness() < 128);
    if (txTable)
    {
        txTable->setProperty("darkTheme", dark);
    }
    if (inspector)
    {
        inspector->setProperty("darkTheme", dark);
    }
    const QString background = dark ? "#0d1422" : "#edf1f8";
    const QString surface = dark ? "#1b2333" : "#ffffff";
    const QString foreground = dark ? "#edf0fa" : "#20263b";
    const QString muted = dark ? "#aab4cd" : "#626c84";
    const QString border = dark ? "#40506a" : "#d0d9e7";
    const QString alternate = dark ? "#212c40" : "#f7f8fc";
    const QString controlBorder = dark ? "#667595" : "#929db3";
    QPalette colors = qApp->palette();
    colors.setColor(QPalette::Window, QColor(background));
    colors.setColor(QPalette::WindowText, QColor(foreground));
    colors.setColor(QPalette::Base, QColor(surface));
    colors.setColor(QPalette::AlternateBase, QColor(alternate));
    colors.setColor(QPalette::Text, QColor(foreground));
    colors.setColor(QPalette::Button, QColor(surface));
    colors.setColor(QPalette::ButtonText, QColor(foreground));
    colors.setColor(QPalette::Highlight, QColor(dark ? "#30355d" : "#eeedff"));
    colors.setColor(QPalette::HighlightedText, QColor(foreground));
    colors.setColor(QPalette::Disabled, QPalette::Text, QColor(muted));
    colors.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(muted));
    setPalette(colors);
    qApp->setPalette(colors);
    const QString baseStyle = QString(
        "QMainWindow,QDialog { background:%1; color:%3; }"
        "QMenuBar { background:%1; color:%3; border-bottom:1px solid %4; padding:2px 8px; font-size:13px; }"
        "QMenuBar::item { background:transparent; padding:4px 8px; border-radius:4px; }"
        "QMenuBar::item:selected { background:%6; color:%3; }"
        "QMenuBar::item:pressed { background:%6; }"
        "QMenu { background:%2; color:%3; border:1px solid %4; border-radius:6px; padding:4px; font-size:13px; }"
        "QMenu::item { padding:6px 24px 6px 12px; border-radius:4px; }"
        "QMenu::item:selected { background:#4f46e5; color:white; }"
        "QMenu::separator { height:1px; background:%4; margin:4px 6px; }"
        "QWidget { font-size:14px; } QLabel { color:%3; }"
        "QLabel#appMark { font-size:24px; font-weight:700; color:white; background:#4f46e5; border-radius:12px; }"
        "QLabel#heading { font-size:22px; font-weight:700; }"
        "QLabel#panelTitle { font-size:18px; font-weight:700; }"
        "QLabel#sectionTitle { font-size:11px; font-weight:700; color:%5; }"
        "QLabel#secondaryText { color:%5; font-size:12px; }"
        "QLabel#summaryBadge { color:%5; background:%6; border-radius:8px; padding:5px 10px; font-size:12px; }"
        "QFrame#connectionBar { background:transparent; border:0; }"
        "QFrame#connectionCard { background:transparent; border:0; }"
        "QFrame#connectionCardDivider { background:%4; border:0; }"
        "QLabel#serialSummary { color:%5; background:%6; border:1px solid %4; border-radius:7px; padding:0 10px; font-size:12px; font-weight:500; }"
        "QLabel#serialFieldLabel { color:%5; font-size:12px; }"
        "QFrame#serialDivider,QFrame#txDivider { background:%4; border:0; }"
        "QMenu#serialMenu { background:%6; border:1px solid %4; border-radius:7px; padding:0; }"
        "QFrame#serialAdvanced { background:transparent; border:0; }"
        "QFrame#dashboardPanel { background:%2; border:1px solid %4; border-radius:14px; }"
        "QFrame#settingsCard { background:%2; border:1px solid %4; border-radius:8px; }"
        "QLabel#settingsGroupTitle { font-size:13px; font-weight:700; color:%3; }"
        "QDialogButtonBox QPushButton { qproperty-icon: none; }"
        "QPushButton { background:%2; color:%3; border:1px solid %4; border-radius:7px; padding:7px 10px; font-weight:500; outline:none; }"
        "QPushButton:hover { background:%6; border-color:#818cf8; }"
        "QPushButton:pressed,QPushButton:checked { background:%6; border-color:%8; }"
        "QPushButton[role=primary] { background:#4f46e5; color:white; border-color:#4f46e5; }"
        "QPushButton[role=primary]:hover { background:#4338ca; border-color:#4338ca; }"
        "QPushButton[role=primary]:pressed { background:#3730a3; border-color:#3730a3; }"
        "QPushButton[role=success] { background:#087f6e; color:white; border-color:#087f6e; }"
        "QPushButton[role=success]:hover { background:#066758; border-color:#066758; }"
        "QPushButton[role=success]:pressed { background:#065446; border-color:#065446; }"
        "QPushButton[role=danger] { color:%7; }"
        "QPushButton:disabled,QPushButton[role=primary]:disabled,QPushButton[role=success]:disabled,QPushButton[role=danger]:disabled { color:%5; background:%6; border-color:%4; }"
        "QLineEdit:focus,QComboBox:focus,QSpinBox:focus,QDoubleSpinBox:focus { border:1px solid #4f46e5; outline:1px solid #818cf8; }"
        "QPushButton[keyboardFocus=true],QPushButton[role=primary][keyboardFocus=true],QPushButton[role=success][keyboardFocus=true] { border:2px solid #818cf8; padding:6px 9px; outline:none; }"
        "QLineEdit,QComboBox,QSpinBox,QDoubleSpinBox { background:%2; color:%3; border:1px solid %8; border-radius:7px; padding:7px; }"
        "QComboBox:disabled { color:%5; background:%6; border-color:%4; }"
        "QComboBox::drop-down { border:0; width:22px; }"
        "QComboBox::down-arrow { image:none; }"
        "QFrame#connectionBar QComboBox { padding:4px 24px 4px 8px; }"
        "QFrame#connectionBar QComboBox QLineEdit { padding:0; border:0; background:transparent; }"
        "QPushButton#refreshPorts { padding:0; }"
        "QPushButton#refreshPorts[keyboardFocus=true] { padding:0; }"
        "QSpinBox::up-button,QDoubleSpinBox::up-button { subcontrol-origin:border; subcontrol-position:top right; width:20px; border-left:1px solid %4; border-bottom:1px solid %4; }"
        "QSpinBox::down-button,QDoubleSpinBox::down-button { subcontrol-origin:border; subcontrol-position:bottom right; width:20px; border-left:1px solid %4; }"
        "QSpinBox::up-arrow,QSpinBox::down-arrow,QDoubleSpinBox::up-arrow,QDoubleSpinBox::down-arrow { image:none; }"
        "QComboBox QAbstractItemView,QListWidget { background:%2; color:%3; selection-background-color:#4f46e5; selection-color:white; padding:3px; }"
        "QComboBox QAbstractItemView::item { min-height:24px; padding:3px 6px; }"
        "QComboBox QAbstractItemView::item:selected { background:#4f46e5; color:white; }"
        "QListWidget::item { padding:5px 6px; border-radius:3px; }"
        "QListWidget::item:selected { background:#4f46e5; color:white; }"
        "QListWidget::item:hover:!selected { background:%6; }"
        "QAbstractItemView { background:%2; color:%3; alternate-background-color:%6; border:0; border-radius:0; gridline-color:%4; }"
        "QTableView::item { padding:0 6px; border:0; }"
        "QTableWidget#txTable::item,QTableWidget#txTable::item:selected,QTableWidget#txTable::item:hover { padding:0; background:transparent; }"
        "QTreeView::item { padding:5px 4px; border:0; }"
        "QHeaderView::section { background:%6; color:%5; padding:9px 8px; border:0; border-bottom:1px solid %4; font-weight:600; font-size:12px; }"
        "QSplitter::handle { background:transparent; }"
        "QStatusBar { color:%5; background:transparent; border:0; }"
        "QStatusBar QLabel { color:%5; font-size:12px; } QStatusBar::item { border:0; }"
        "QScrollBar:vertical { background:%6; width:10px; } QScrollBar:horizontal { background:%6; height:10px; }"
        "QScrollBar::handle { background:%8; border-radius:4px; min-width:24px; min-height:24px; }"
        "QScrollBar::handle:hover { background:#818cf8; } QScrollBar::add-line,QScrollBar::sub-line { width:0px; height:0px; }"
        "QLabel#validationError { color:%7; }"
    ).arg(background, surface, foreground, border, muted, alternate,
          dark ? "#fca5a5" : "#b91c1c", controlBorder);
    const QString connectionStyle = QString(
        "QPushButton#connect[connected=true] { background:%1; color:%2; border:1px solid %3; font-weight:600; }"
        "QPushButton#connect[connected=true]:hover { background:%4; border-color:#ef4444; color:%5; }"
        "QPushButton#connect[connected=true]:pressed { background:%6; }"
    ).arg(dark ? "#3b1e26" : "#fef2f2",
          dark ? "#fca5a5" : "#b91c1c",
          dark ? "#7f2d3f" : "#fca5a5",
          dark ? "#4e2531" : "#fee2e2",
          dark ? "#ffffff" : "#991b1b",
          dark ? "#2d151c" : "#fecaca");
    const QString selectionStyle = QString(
        "QTableView::item:selected,QTreeView::item:selected { background:%1; color:%2; }"
        "QTreeView::branch:selected { background:%1; }"
        "QTableView::item:hover:!selected,QTreeView::item:hover:!selected { background:%3; }"
    ).arg(dark ? "#333a5c" : "#e8e8fb", foreground, alternate);
    const QString dataStyle = QString(
        "QFrame#dataPane { background:%1; border:1px solid %2; border-radius:5px; }"
        "QLabel#dataPaneTitle { background:%3; color:%4; border-bottom:1px solid %2; padding:9px 10px; font-size:12px; font-weight:600; }"
        "QTreeWidget#rxTree,QTableWidget#inspector { background:%1; alternate-background-color:%5; }"
        "QTableWidget#txTable { background:transparent; }"
        "QTreeWidget#rxTree::item { border-bottom:1px solid %5; }"
        "QHeaderView::section { background:%6; color:%4; border-bottom:1px solid %2; }"
        "QSplitter#rxSplit::handle { background:transparent; border:0; }"
    ).arg(dark ? "#162134" : "#ffffff", dark ? "#35445d" : "#d1dbea",
          dark ? "#202d42" : "#eef2f9", dark ? "#d6dfee" : "#40536e",
          dark ? "#1d2b41" : "#f4f7fb", dark ? "#202e44" : "#f2f5fa");
    setStyleSheet(baseStyle + connectionStyle + selectionStyle + dataStyle);
    for (int row = 0; row < rxTree->topLevelItemCount(); ++row)
    {
        auto *system = rxTree->topLevelItem(row);
        system->setForeground(0, QColor(muted));
        for (int child = 0; child < system->childCount(); ++child)
        {
            system->child(child)->setForeground(0, QColor(muted));
        }
    }
    for (const auto &name : {"addMessage", "startTx", "sendOnce", "stopTx", "stopAll", "refreshPorts", "serialSettings", "editMessage", "removeMessage"})
    {
        auto *control = findChild<QPushButton *>(name);
        if (!control)
        {
            continue;
        }
        const QString role = control->property("role").toString();
        const QColor iconColor(role == "primary" || role == "success" ? "#ffffff" :
                               role == "danger" ? (dark ? "#fca5a5" : "#b91c1c") : foreground);
        QIcon icon = actionIcon(name, iconColor);
        icon.addPixmap(actionIcon(name, QColor(muted)).pixmap(16, 16), QIcon::Disabled);
        control->setIcon(icon);
        control->setIconSize(QSize(16, 16));
    }
}
