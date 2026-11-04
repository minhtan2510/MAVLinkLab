#include "window.h"
#include "mavlink_enums.h"
#include <QApplication>
#include <QCloseEvent>
#include <QCompleter>
#include <QSettings>
#include <QTextLayout>
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
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <map>
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
#include <QHelpEvent>
#include <QIconEngine>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QToolTip>
#include <QWindow>
#include <algorithm>
#include <array>
#include <limits>

/** @brief Provide a themeable dialog title bar with native system dragging where available. */
class DialogTitleBar : public QFrame
{
public:
    /** @brief Add the dialog title and an accessible close action. */
    explicit DialogTitleBar(QDialog *dialog) : QFrame(dialog)
    {
        setObjectName("dialogTitleBar");
        auto *row = new QHBoxLayout(this);
        row->setContentsMargins(20, 16, 16, 8);
        row->setSpacing(12);
        auto *title = new QLabel(dialog->windowTitle(), this);
        title->setObjectName("dialogWindowTitle");
        title->setAttribute(Qt::WA_TransparentForMouseEvents);
        row->addWidget(title, 1);
        connect(dialog, &QWidget::windowTitleChanged, title, &QLabel::setText);
        auto *close = new QPushButton(QString::fromUtf8("×"), this);
        close->setObjectName("closeDialog");
        close->setAccessibleName("Close dialog");
        close->setFixedSize(28, 28);
        close->setAutoDefault(false);
        row->addWidget(close);
        connect(close, &QPushButton::clicked, dialog, &QDialog::reject);
    }

protected:
    /** @brief Start compositor-managed movement, retaining a fallback for unsupported platforms. */
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
        {
            offset = event->globalPosition().toPoint() - window()->frameGeometry().topLeft();
            dragging = !window()->windowHandle() || !window()->windowHandle()->startSystemMove();
            event->accept();
            return;
        }
        QFrame::mousePressEvent(event);
    }

    /** @brief Move the dialog when native system movement is unavailable. */
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (dragging && (event->buttons() & Qt::LeftButton))
        {
            window()->move(event->globalPosition().toPoint() - offset);
            event->accept();
            return;
        }
        QFrame::mouseMoveEvent(event);
    }

    /** @brief End fallback movement on pointer release. */
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        dragging = false;
        QFrame::mouseReleaseEvent(event);
    }

private:
    QPoint offset;
    bool dragging = false;
};

/**
 * @brief Create a full-width themed header above an inset dialog body.
 * @param dialog Dialog whose native decorations are replaced.
 * @param layout Empty outer layout containing the header and body.
 * @return Body layout retaining the original content margins and spacing.
 */
static QVBoxLayout *addDialogTitleBar(QDialog *dialog, QVBoxLayout *layout)
{
    dialog->setWindowFlag(Qt::FramelessWindowHint);
    dialog->setSizeGripEnabled(true);
    const QMargins margins = layout->contentsMargins();
    const int spacing = layout->spacing();
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);
    layout->addWidget(new DialogTitleBar(dialog));
    auto *dividerRow = new QHBoxLayout;
    dividerRow->setContentsMargins(margins.left(), 0, margins.right(), 0);
    auto *divider = new QFrame(dialog);
    divider->setObjectName("dialogTitleDivider");
    divider->setFixedHeight(1);
    dividerRow->addWidget(divider);
    layout->addLayout(dividerRow);
    auto *body = new QWidget(dialog);
    auto *content = new QVBoxLayout(body);
    content->setContentsMargins(margins.left(), 8, margins.right(), margins.bottom());
    content->setSpacing(spacing);
    layout->addWidget(body, 1);
    return content;
}

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
    const auto name = Mav::lookupEnumName(msgid, field.toStdString(), v);
    if (!name.empty())
    {
        QString res = QString::fromStdString(name);
        if (msgid == 0)
        {
            if (field == "autopilot" && res.startsWith("MAV_AUTOPILOT_"))
            {
                return res.mid(14);
            }
            if (field == "type" && res.startsWith("MAV_TYPE_"))
            {
                return res.mid(9);
            }
            if (field == "system_status" && res.startsWith("MAV_STATE_"))
            {
                return res.mid(10);
            }
        }
        return res;
    }
    return {};
}

class WorkbenchComboBox;

/**
 * @brief Custom delegate for combo popup items rendering selection highlight, hover, and vector checkmark.
 */
class WorkbenchComboDelegate : public QStyledItemDelegate
{
public:
    /**
     * @brief Construct delegate bound to a WorkbenchComboBox.
     * @param combo Parent combo box pointer.
     */
    explicit WorkbenchComboDelegate(WorkbenchComboBox *combo);

    /**
     * @brief Render popup items with persistent active highlight and an optional checkmark.
     * @param painter Active painter.
     * @param option Style options for the item.
     * @param index Model index being drawn.
     */
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    /**
     * @brief Provide adequate item height for clean touch and desktop readability.
     * @param option View options.
     * @param index Item model index.
     * @return Minimum required size.
     */
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(std::max(size.height(), 28));
        return size;
    }

    /**
     * @brief Render tooltip displaying item value upon hovering popup entries.
     * @param event Help event from Qt.
     * @param view Parent abstract item view.
     * @param option Style options for the item.
     * @param index Model index being hovered.
     * @return True if tooltip was displayed, false otherwise.
     */
    bool helpEvent(QHelpEvent *event, QAbstractItemView *view,
                   const QStyleOptionViewItem &option, const QModelIndex &index) override
    {
        if (event && event->type() == QEvent::ToolTip && index.isValid())
        {
            const QString tip = index.data(Qt::ToolTipRole).toString();
            if (!tip.isEmpty())
            {
                QToolTip::showText(event->globalPos(), tip, view, option.rect);
                return true;
            }
        }
        return QStyledItemDelegate::helpEvent(event, view, option, index);
    }

private:
    WorkbenchComboBox *m_combo = nullptr;
};

/** @brief Keep native combo interaction with a theme-aware vector chevron and persistent item highlight. */
class WorkbenchComboBox : public QComboBox
{
public:
    /**
     * @brief Construct combo box with vector chevron and theme-aware delegate.
     * @param parent Optional parent widget.
     */
    explicit WorkbenchComboBox(QWidget *parent = nullptr)
        : QComboBox(parent)
    {
        setItemDelegate(new WorkbenchComboDelegate(this));
        setAttribute(Qt::WA_Hover, true);
        if (view())
        {
            view()->installEventFilter(this);
            view()->setAttribute(Qt::WA_AlwaysShowToolTips, true);
            if (view()->viewport())
            {
                view()->viewport()->setAttribute(Qt::WA_AlwaysShowToolTips, true);
            }
            if (QWidget *container = view()->window())
            {
                container->setAttribute(Qt::WA_TranslucentBackground, true);
            }
        }
    }

    /**
     * @brief Find item index matching current text or numeric ID.
     * @return Matching index or -1 if not found.
     */
    int activeIndex() const
    {
        QString text = currentText().trimmed();
        const int paren = text.indexOf('(');
        if (paren > 0)
        {
            text = text.left(paren).trimmed();
        }
        if (text.isEmpty())
        {
            return isEditable() ? -1 : currentIndex();
        }

        int idx = findText(text, Qt::MatchFixedString | Qt::MatchCaseSensitive);
        if (idx < 0)
        {
            idx = findText(text, Qt::MatchFixedString);
        }
        if (idx < 0)
        {
            bool ok = false;
            const qulonglong num = text.toULongLong(&ok, 0);
            if (ok)
            {
                idx = findData(num);
            }
        }
        if (idx < 0 && !isEditable())
        {
            idx = currentIndex();
        }
        return idx;
    }

    /**
     * @brief Show popup with rounded mask, synchronized active item highlight, and center scroll.
     */
    void showPopup() override
    {
        if (isEditable())
        {
            const int idx = activeIndex();
            if (idx >= 0 && idx != currentIndex())
            {
                const QString preservedText = currentText();
                const int preservedCursor = lineEdit() ? lineEdit()->cursorPosition() : -1;
                const QSignalBlocker blocker(this);
                setCurrentIndex(idx);
                if (currentText() != preservedText)
                {
                    setEditText(preservedText);
                }
                if (lineEdit() && preservedCursor >= 0)
                {
                    lineEdit()->setCursorPosition(preservedCursor);
                }
            }
            if (view())
            {
                if (idx >= 0)
                {
                    view()->setCurrentIndex(model()->index(idx, modelColumn()));
                    if (view()->selectionModel())
                    {
                        view()->selectionModel()->select(model()->index(idx, modelColumn()),
                                                         QItemSelectionModel::ClearAndSelect);
                    }
                    view()->scrollTo(model()->index(idx, modelColumn()), QAbstractItemView::PositionAtCenter);
                }
                else if (view()->selectionModel())
                {
                    view()->selectionModel()->clearSelection();
                    view()->setCurrentIndex(QModelIndex());
                }
            }
        }
        else if (view() && currentIndex() >= 0)
        {
            view()->scrollTo(model()->index(currentIndex(), modelColumn()), QAbstractItemView::PositionAtCenter);
        }

        QComboBox::showPopup();
        if (QWidget *container = view() ? view()->window() : nullptr)
        {
            container->setAttribute(Qt::WA_TranslucentBackground, true);
            QPainterPath outline;
            outline.addRoundedRect(QRectF(container->rect()), 8.0, 8.0);
            container->setMask(QRegion(outline.toFillPolygon().toPolygon()));
        }
        update();
    }

    /**
     * @brief Hide popup and trigger repaint for closed state indicator.
     */
    void hidePopup() override
    {
        QComboBox::hidePopup();
        update();
    }

    /**
     * @brief Filter events from popup view to refresh chevron state on show/hide.
     * @param watched Object receiving the event.
     * @param event The Qt event.
     * @return True if event was filtered, false otherwise.
     */
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == view())
        {
            if (event->type() == QEvent::Show || event->type() == QEvent::Hide)
            {
                update();
            }
        }
        return QComboBox::eventFilter(watched, event);
    }

protected:
    /**
     * @brief Trigger repaint upon hover entry.
     * @param event Enter event from Qt.
     */
    void enterEvent(QEnterEvent *event) override
    {
        QComboBox::enterEvent(event);
        update();
    }

    /**
     * @brief Trigger repaint upon hover departure.
     * @param event Leave event from Qt.
     */
    void leaveEvent(QEvent *event) override
    {
        QComboBox::leaveEvent(event);
        update();
    }

    /**
     * @brief Paint a consistent dropdown indicator with state feedback and rotation.
     * @param event Paint event from Qt.
     */
    void paintEvent(QPaintEvent *event) override
    {
        QComboBox::paintEvent(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const bool isOpen = view() && view()->isVisible();
        const bool isHovered = underMouse() && isEnabled();
        const bool isFocused = hasFocus() && isEnabled();
        const bool dark = property("darkTheme").isValid()
            ? property("darkTheme").toBool()
            : (window() && window()->property("darkTheme").isValid()
               ? window()->property("darkTheme").toBool()
               : (qApp && qApp->property("darkTheme").isValid()
                  ? qApp->property("darkTheme").toBool()
                  : (palette().color(QPalette::Window).lightness() < 128)));

        const QColor chevronColor = (isOpen || isHovered || isFocused)
            ? QColor("#818cf8")
            : (dark ? QColor("#aab4cd") : QColor("#626c84"));

        const qreal direction = isOpen ? -1.0 : 1.0;
        const QPointF center(width() - 14.0, height() / 2.0);
        painter.setPen(QPen(chevronColor, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(center + QPointF(-4.0, -2.0 * direction), center + QPointF(0, 2.0 * direction));
        painter.drawLine(center + QPointF(0, 2.0 * direction), center + QPointF(4.0, -2.0 * direction));
    }
};

WorkbenchComboDelegate::WorkbenchComboDelegate(WorkbenchComboBox *combo)
    : QStyledItemDelegate(reinterpret_cast<QObject *>(combo)), m_combo(combo)
{
}

void WorkbenchComboDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    const int active = m_combo ? m_combo->activeIndex() : -1;
    const bool isCurrent = (index.row() == active);
    const bool showCheckmark = isCurrent && !(m_combo && m_combo->property("hideSelectionCheckmark").toBool());
    const bool isHovered = (option.state & QStyle::State_Selected) || (option.state & QStyle::State_MouseOver);

    const bool dark = m_combo && m_combo->property("darkTheme").isValid()
        ? m_combo->property("darkTheme").toBool()
        : (m_combo && m_combo->window() && m_combo->window()->property("darkTheme").isValid()
           ? m_combo->window()->property("darkTheme").toBool()
           : (qApp && qApp->property("darkTheme").isValid()
              ? qApp->property("darkTheme").toBool()
              : (m_combo ? m_combo->palette().color(QPalette::Window).lightness() < 128 : false)));

    const QRect itemRect = option.rect.adjusted(2, 1, -2, -1);

    if (isCurrent)
    {
        painter->setPen(Qt::NoPen);
        painter->setBrush(isHovered ? QColor("#4338ca") : QColor("#4f46e5"));
        painter->drawRoundedRect(itemRect, 5.0, 5.0);
    }
    else if (isHovered)
    {
        painter->setPen(Qt::NoPen);
        painter->setBrush(dark ? QColor("#212c40") : QColor("#f7f8fc"));
        painter->drawRoundedRect(itemRect, 5.0, 5.0);
    }

    QFont font = option.font;
    if (isCurrent)
    {
        font.setBold(true);
    }
    painter->setFont(font);

    const QColor textColor = isCurrent
        ? QColor("#ffffff")
        : (dark ? QColor("#edf0fa") : QColor("#20263b"));
    painter->setPen(textColor);

    const QRect textRect = itemRect.adjusted(10, 0, showCheckmark ? -26 : -10, 0);
    const QString text = option.fontMetrics.elidedText(index.data(Qt::DisplayRole).toString(),
                                                       Qt::ElideRight, textRect.width());
    painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text);

    if (showCheckmark)
    {
        painter->setPen(QPen(QColor("#ffffff"), 1.75, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const QPointF checkPos(itemRect.right() - 14, itemRect.center().y());
        QPainterPath check;
        check.moveTo(checkPos + QPointF(-4.5, 0.0));
        check.lineTo(checkPos + QPointF(-1.5, 3.0));
        check.lineTo(checkPos + QPointF(4.5, -3.5));
        painter->drawPath(check);
    }

    painter->restore();
}

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

/** @brief Custom theme-aware checkbox with a crisp vector checkmark and focus indicator. */
class WorkbenchCheckBox : public QCheckBox
{
public:
    /**
     * @brief Construct a theme-aware checkbox with label text.
     * @param text Checkbox label string.
     * @param parent Optional parent widget.
     */
    explicit WorkbenchCheckBox(const QString &text, QWidget *parent = nullptr) : QCheckBox(text, parent)
    {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
    }

    /**
     * @brief Construct a theme-aware checkbox without initial label.
     * @param parent Optional parent widget.
     */
    explicit WorkbenchCheckBox(QWidget *parent = nullptr) : QCheckBox(parent)
    {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
    }

    /**
     * @brief Calculate recommended size matching vector indicator and label text.
     * @return Recommended size hint for layout.
     */
    QSize sizeHint() const override
    {
        const QFontMetrics fm(font());
        const int boxSize = 18;
        const int textW = text().isEmpty() ? 0 : fm.horizontalAdvance(text()) + 10;
        return QSize(boxSize + textW + 6, std::max(boxSize + 4, fm.height() + 4));
    }

protected:
    /**
     * @brief Trigger repaint upon hover entry.
     * @param event Enter event from Qt.
     */
    void enterEvent(QEnterEvent *event) override
    {
        QCheckBox::enterEvent(event);
        update();
    }

    /**
     * @brief Trigger repaint upon hover departure.
     * @param event Leave event from Qt.
     */
    void leaveEvent(QEvent *event) override
    {
        QCheckBox::leaveEvent(event);
        update();
    }

    /**
     * @brief Paint vector checkbox with a refined border and state feedback.
     * @param event Paint event from Qt.
     */
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const int boxSize = 18;
        const int y = (height() - boxSize) / 2;
        const QRect boxRect(2, y, boxSize, boxSize);

        const bool checked = isChecked();
        const bool hovered = underMouse() && isEnabled();
        const bool focused = hasFocus() && isEnabled();
        const bool pressed = isDown() && isEnabled();
        const bool dark = property("darkTheme").isValid() ?
                          property("darkTheme").toBool() :
                          (window() && window()->property("darkTheme").isValid() ?
                           window()->property("darkTheme").toBool() :
                           (qApp && qApp->property("darkTheme").isValid() ?
                            qApp->property("darkTheme").toBool() :
                            (palette().color(QPalette::Window).lightness() < 128)));

        QColor borderColor = (hovered || focused) ? QColor("#818cf8") : (dark ? QColor("#667595") : QColor("#929db3"));
        QColor bgColor = dark ? QColor("#1b2333") : QColor("#ffffff");

        if (pressed && !checked)
        {
            bgColor = dark ? QColor("#222c3e") : QColor("#f1f5f9");
        }

        if (checked)
        {
            bgColor = isEnabled() ? (pressed ? QColor("#3730a3") : (hovered ? QColor("#4338ca") : QColor("#4f46e5")))
                                  : (dark ? QColor("#3730a3") : QColor("#818cf8"));
            borderColor = (focused || hovered) ? QColor("#818cf8") : bgColor;
        }

        painter.setPen(QPen(borderColor, 1.0));
        painter.setBrush(bgColor);
        painter.drawRoundedRect(boxRect, 4.0, 4.0);

        if (checked)
        {
            QPainterPath path;
            path.moveTo(boxRect.left() + 4.5, boxRect.top() + 9.5);
            path.lineTo(boxRect.left() + 7.5, boxRect.top() + 12.5);
            path.lineTo(boxRect.left() + 13.5, boxRect.top() + 5.5);

            painter.setPen(QPen(Qt::white, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(path);
        }

        if (!text().isEmpty())
        {
            const QRect textRect(boxSize + 12, 0, width() - boxSize - 12, height());
            const QColor textColor = isEnabled() ? (dark ? QColor("#edf0fa") : QColor("#20263b"))
                                                : (dark ? QColor("#aab4cd") : QColor("#626c84"));
            painter.setPen(textColor);
            painter.setFont(font());
            painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text());
        }
    }
};

/** @brief Wrap a TX identity into at most two lines, eliding only the final overflow. */
static QStringList txTitleLines(const QString &text, const QFont &font, int width)
{
    width = std::max(1, width);
    const QFontMetrics metrics(font);
    if (metrics.horizontalAdvance(text) <= width)
    {
        return {text};
    }
    int split = -1;
    int balance = std::numeric_limits<int>::max();
    for (int at = text.indexOf('_'); at >= 0; at = text.indexOf('_', at + 1))
    {
        const int left = metrics.horizontalAdvance(text.left(at + 1));
        const int right = metrics.horizontalAdvance(text.mid(at + 1));
        if (left <= width && right <= width && qAbs(left - right) < balance)
        {
            split = at + 1;
            balance = qAbs(left - right);
        }
    }
    if (split > 0)
    {
        return {text.left(split), text.mid(split)};
    }
    QTextLayout layout(text, font);
    QTextOption wrapping;
    wrapping.setWrapMode(QTextOption::WrapAnywhere);
    layout.setTextOption(wrapping);
    layout.beginLayout();
    QTextLine first = layout.createLine();
    first.setLineWidth(width);
    const int length = first.textLength();
    layout.endLayout();
    QStringList lines{text.left(length).trimmed()};
    if (length < text.size())
    {
        lines.append(QFontMetrics(font).elidedText(text.mid(length).trimmed(), Qt::ElideRight, width));
    }
    return lines;
}

/** @brief Present each TX entry as a readable card backed by the existing row model. */
class TxCardDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    /** @brief Paint state-specific cards with a compact target-rate badge and distinct actions. */
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
        const QColor panelBg(dark ? "#1b2333" : "#ffffff");
        painter->save();
        painter->fillRect(option.rect, panelBg);
        painter->setRenderHint(QPainter::Antialiasing);
        const QRect card = option.rect.adjusted(1, 5, -2, -5);
        const QColor stateAccent = error ? QColor(dark ? "#ef8799" : "#bd405b") :
                                   running ? QColor(dark ? "#57c99a" : "#249569") : QColor(dark ? "#4a5d7c" : "#b0c0d4");
        QColor surface = panelBg;
        if (hovered)
        {
            surface = dark ? surface.lighter(112) : QColor("#f8fafc");
        }
        const QColor cardBorder = selected ? QColor("#818cf8") : QColor(dark ? "#3d506e" : "#c2d0e2");
        painter->setPen(QPen(cardBorder, selected ? 1.5 : 1.2));
        painter->setBrush(surface);
        painter->drawRoundedRect(card, 8, 8);

        // Accent strip on the left edge
        painter->setPen(Qt::NoPen);
        painter->setBrush(stateAccent);
        painter->drawRoundedRect(QRect(card.left() + 8, card.top() + 8, 5, card.height() - 16), 3, 3);

        // Status badge button on top right
        const QRect badge(card.right() - 88, card.top() + 8, 80, 24);
        const QColor badgeBg = error ? QColor(dark ? "#482e3f" : "#fff0f1") :
                               running ? QColor(dark ? "#1d3d36" : "#e7f6ee") : QColor(dark ? "#253144" : "#edf1f7");
        const QColor badgeBorder = error ? QColor(dark ? "#7a3749" : "#f8b4bc") :
                                   running ? QColor(dark ? "#2e685a" : "#bfe6d3") : QColor(dark ? "#3b4d6a" : "#d0d9e6");
        const QColor badgeFg = error ? QColor(dark ? "#ffb3bd" : "#b42340") :
                               running ? QColor(dark ? "#94e5bd" : "#17613e") : muted;
        painter->setPen(QPen(badgeBorder, 1));
        painter->setBrush(badgeBg);
        painter->drawRoundedRect(badge, 5, 5);
        if (error)
        {
            painter->setPen(badgeFg);
            QFont badgeFont = option.font;
            badgeFont.setPixelSize(11);
            badgeFont.setWeight(QFont::Medium);
            painter->setFont(badgeFont);
            painter->drawText(badge, Qt::AlignCenter, "Error");
        }
        else if (running)
        {
            painter->fillRect(QRect(badge.left() + 8, badge.center().y() - 3, 7, 7), badgeFg);
            painter->setPen(badgeFg);
            QFont badgeFont = option.font;
            badgeFont.setPixelSize(11);
            badgeFont.setWeight(QFont::Medium);
            painter->setFont(badgeFont);
            painter->drawText(badge.adjusted(12, 0, 0, 0), Qt::AlignCenter, "Running");
        }
        else
        {
            QPolygonF play;
            play << QPointF(badge.left() + 9, badge.center().y() - 4)
                 << QPointF(badge.left() + 15, badge.center().y())
                 << QPointF(badge.left() + 9, badge.center().y() + 4);
            painter->setPen(Qt::NoPen);
            painter->setBrush(badgeFg);
            painter->drawPolygon(play);
            painter->setPen(badgeFg);
            QFont badgeFont = option.font;
            badgeFont.setPixelSize(11);
            badgeFont.setBold(false);
            painter->setFont(badgeFont);
            painter->drawText(badge.adjusted(12, 0, 0, 0), Qt::AlignCenter, "Stopped");
        }

        // Line 1: Title and Target Rate pill
        const int textLeft = card.left() + 24;
        QFont titleFont = option.font;
        titleFont.setPixelSize(13);
        titleFont.setWeight(QFont::Normal);
        painter->setFont(titleFont);
        painter->setPen(foreground);
        const int maxTitleWidth = badge.left() - textLeft - 8;
        const QString title = QString("[%1] %2").arg(value(0), value(1));
        const QStringList lines = txTitleLines(title, titleFont, maxTitleWidth);
        const int extra = lines.size() > 1 ? 16 : 0;
        if (lines.size() > 1)
        {
            painter->drawText(QRect(textLeft, badge.top(), maxTitleWidth, badge.height() + extra),
                              Qt::AlignVCenter | Qt::AlignLeft, lines.join('\n'));
        }
        else
        {
            const int titleAdvance = QFontMetrics(titleFont).horizontalAdvance(title);
            painter->drawText(QRect(textLeft, badge.top(), titleAdvance, badge.height()),
                              Qt::AlignVCenter | Qt::AlignLeft, title);

            // Draw compact Target Hz pill next to title if space allows
            const QString rateText = value(3) + " Hz";
            QFont pillFont = option.font;
            pillFont.setPixelSize(11);
            pillFont.setWeight(QFont::Medium);
            const int rateAdvance = QFontMetrics(pillFont).horizontalAdvance(rateText);
            const int pillWidth = rateAdvance + 12;
            const int pillLeft = textLeft + titleAdvance + 8;
            if (pillLeft + pillWidth <= badge.left() - 8)
            {
                const QRect pillRect(pillLeft, badge.top() + 3, pillWidth, 18);
                painter->setPen(QPen(QColor(dark ? "#384a65" : "#cbd7e8"), 1));
                painter->setBrush(QColor(dark ? "#233144" : "#e6effa"));
                painter->drawRoundedRect(pillRect, 4, 4);
                painter->setFont(pillFont);
                painter->setPen(QColor(dark ? "#b0cbef" : "#2b5687"));
                painter->drawText(pillRect, Qt::AlignCenter, rateText);
            }
        }

        // Line 2: Compact metrics row (Sys · Comp · Actual · Frames)
        const int line2Y = card.top() + 36 + extra;
        const int line2Height = 18;
        int curX = textLeft;
        QFont metaFont = option.font;
        metaFont.setPixelSize(12);
        metaFont.setBold(false);
        QFont emphasisFont = metaFont;
        emphasisFont.setWeight(QFont::Medium);

        const auto drawPart = [&](const QString &txt, const QColor &col, bool emphasized)
        {
            painter->setFont(emphasized ? emphasisFont : metaFont);
            painter->setPen(col);
            const int adv = QFontMetrics(painter->font()).horizontalAdvance(txt);
            if (curX + adv <= card.right() - 8)
            {
                painter->drawText(QRect(curX, line2Y, adv, line2Height), Qt::AlignVCenter | Qt::AlignLeft, txt);
                curX += adv;
            }
        };

        const QColor dotColor(dark ? "#4a5a74" : "#adbcd0");
        const QString sysComp = QString("Sys %1 · Comp %2").arg(value(2).section('/', 0, 0).trimmed(),
                                                                value(2).section('/', 1, 1).trimmed());
        drawPart(sysComp, muted, false);
        drawPart("   ·   ", dotColor, false);
        drawPart("Actual: ", muted, false);
        drawPart(value(4) + " Hz", running ? QColor(dark ? "#57c99a" : "#178255") : foreground, true);
        drawPart("   ·   ", dotColor, false);
        drawPart("Frames: ", muted, false);
        drawPart(value(5), foreground, true);
        painter->restore();
    }
};

/** @brief Draw a theme-aware search glyph while retaining native clear and keyboard actions. */
class CatalogSearch : public QLineEdit
{
public:
    /** @brief Reserve space for the search glyph and enable the standard clear action. */
    explicit CatalogSearch(QWidget *parent = nullptr) : QLineEdit(parent)
    {
        setTextMargins(22, 0, 0, 0);
        setClearButtonEnabled(true);
        updatePlaceholderColor();
    }

protected:
    /** @brief Keep search hints subdued when the application theme changes. */
    void changeEvent(QEvent *event) override
    {
        QLineEdit::changeEvent(event);
        if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange ||
            event->type() == QEvent::StyleChange)
        {
            updatePlaceholderColor();
        }
    }

    /** @brief Change only the placeholder role, preserving normal input text colors. */
    void updatePlaceholderColor()
    {
        QPalette colors = palette();
        const QColor hint(colors.color(QPalette::Window).lightness() < 128 ? "#7e899f" : "#8992a6");
        if (colors.color(QPalette::PlaceholderText) != hint)
        {
            colors.setResolveMask(0);
            colors.setColor(QPalette::PlaceholderText, hint);
            setPalette(colors);
        }
    }

    /** @brief Paint the glyph using the current palette, including live theme changes. */
    void paintEvent(QPaintEvent *event) override
    {
        QLineEdit::paintEvent(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(palette().color(QPalette::PlaceholderText), 1.4));
        const QPointF center(15, height() / 2.0 - 1);
        painter.drawEllipse(center, 4, 4);
        painter.drawLine(center + QPointF(3, 3), center + QPointF(7, 7));
    }
};

/** @brief Sort catalog matches by relevance, then by numeric message ID. */
class CatalogItem : public QListWidgetItem
{
public:
    using QListWidgetItem::QListWidgetItem;
    /** @brief Keep exact and prefix matches ahead of substring matches without changing identity. */
    bool operator<(const QListWidgetItem &other) const override
    {
        const int rank = data(Qt::UserRole + 4).toInt();
        const int otherRank = other.data(Qt::UserRole + 4).toInt();
        return rank != otherRank ? rank < otherRank : data(Qt::UserRole).toUInt() < other.data(Qt::UserRole).toUInt();
    }
};

/** @brief Align catalog IDs and names on a soft selected surface in either theme. */
class CatalogDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    /** @brief Draw two text columns with independent metadata and selection emphasis. */
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const bool dark = option.palette.color(QPalette::Window).lightness() < 128;
        const bool selected = option.state & QStyle::State_Selected;
        const bool hovered = option.state & QStyle::State_MouseOver;
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->fillRect(option.rect, option.palette.color(QPalette::Base));
        if (selected || hovered)
        {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(selected ? (dark ? "#303454" : "#eeedff") : (dark ? "#252d40" : "#f3f5fa")));
            painter->drawRoundedRect(option.rect.adjusted(2, 1, -2, -1), 4, 4);
        }
        QFont font = option.font;
        font.setPixelSize(12);
        painter->setFont(font);
        painter->setPen(QColor(dark ? "#aab4cd" : "#626c84"));
        painter->drawText(option.rect.adjusted(9, 0, -4, 0), Qt::AlignLeft | Qt::AlignVCenter,
                          QString::number(index.data(Qt::UserRole).toUInt()));
        font.setPixelSize(13);
        painter->setFont(font);
        painter->setPen(QColor(selected ? (dark ? "#dddafe" : "#4338a5") : (dark ? "#edf0fa" : "#20263b")));
        const QRect nameRect = option.rect.adjusted(65, 0, -8, 0);
        painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
                          QFontMetrics(font).elidedText(index.data(Qt::UserRole + 3).toString(), Qt::ElideRight, nameRect.width()));
        if (option.state & QStyle::State_HasFocus)
        {
            painter->setPen(QPen(QColor("#939bf0"), 1, Qt::DotLine));
            painter->setBrush(Qt::NoBrush);
            painter->drawRoundedRect(option.rect.adjusted(2, 1, -2, -1), 4, 4);
        }
        painter->restore();
    }

    /** @brief Provide compact desktop rows with a stable keyboard target. */
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return QSize(180, 30);
    }
};

/** @brief Paint payload identities with quieter type metadata and consistent row separators. */
class PayloadIdentityDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    /** @brief Keep parameter names readable and type annotations visually secondary. */
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem styled(option);
        initStyleOption(&styled, index);
        const bool isInspector = option.widget && option.widget->objectName() == "inspector";
        const bool dark = styled.palette.color(QPalette::Window).lightness() < 128;
        const bool selected = !isInspector && (option.state & QStyle::State_Selected);
        painter->save();
        painter->fillRect(option.rect, selected ? styled.palette.color(QPalette::Highlight) :
                          styled.palette.color(QPalette::Base));
        painter->setPen(QColor(dark ? "#2d384b" : "#e8ecf3"));
        painter->drawLine(option.rect.bottomLeft(), option.rect.bottomRight());
        painter->setFont(styled.font);
        painter->setPen(index.column() == 1 ? QColor(dark ? "#aab4cd" : "#626c84") :
                        styled.palette.color(QPalette::Text));
        const QRect textRect = option.rect.adjusted(10, 0, -10, 0);
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                          QFontMetrics(styled.font).elidedText(styled.text, Qt::ElideRight, textRect.width()));
        if (!isInspector && (option.state & QStyle::State_HasFocus))
        {
            painter->setPen(QPen(QColor("#939bf0"), 1, Qt::DotLine));
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(option.rect.adjusted(1, 1, -2, -2));
        }
        painter->restore();
    }
};

/** @brief Give decoded and editable values a distinct visual column without changing data. */
class ValueDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    /** @brief Keep editable payload values and decoded view-only cells visible and cleanly styled. */
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const bool isPayload = option.widget && option.widget->objectName() == "payloadFields";
        const bool isInspector = option.widget && option.widget->objectName() == "inspector";
        if (!isPayload && !isInspector)
        {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        QStyleOptionViewItem styled(option);
        initStyleOption(&styled, index);
        const bool dark = styled.palette.color(QPalette::Window).lightness() < 128;
        const bool invalid = isPayload && index.data(Qt::UserRole + 1).toBool();
        const bool selected = isPayload && (option.state & QStyle::State_Selected);
        const bool editable = isPayload && (index.flags() & Qt::ItemIsEditable);
        const bool hovered = isPayload && (option.state & QStyle::State_MouseOver);
        const bool changed = isInspector && index.data(Qt::UserRole + 5).toBool();
        const QColor rowSurface = styled.palette.color(QPalette::Base);
        const QColor border(dark ? "#2d3a4e" : "#e5eaf2");
        const QRect input(option.rect.left() + 8, option.rect.top() + 6, option.rect.width() - 16, 28);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->fillRect(option.rect, rowSurface);
        painter->setPen(QPen(QColor(dark ? "#2d384b" : "#e8ecf3"), 1));
        painter->drawLine(option.rect.bottomLeft(), option.rect.bottomRight());

        // Draw input box (either editable for payload or clean view-only pill for inspector/readonly)
        const QColor outline = invalid ? QColor(dark ? "#ef8799" : "#bd405b") :
                               changed ? QColor(dark ? "#388bfd" : "#0969da") :
                               selected || hovered ? QColor("#939bf0") : border;
        painter->setPen(QPen(outline, selected || invalid || changed ? 1.5 : 1.0));
        const QColor boxBg = invalid ? QColor(dark ? "#482e3f" : "#fff0f1") :
                             selected ? QColor(dark ? "#293451" : "#f2f1ff") :
                             editable ? QColor(dark ? "#1e293b" : "#fafbfd") :
                             QColor(dark ? "#1b283d" : "#f4f7fb");
        painter->setBrush(boxBg);
        painter->drawRoundedRect(input, 5, 5);

        painter->setFont(styled.font);
        const QColor textColor = changed ? QColor(dark ? "#58a6ff" : "#0969da") :
                                 isInspector ? QColor(dark ? "#c9d8eb" : "#24324a") :
                                 styled.palette.color(QPalette::Text);
        painter->setPen(textColor);
        const QRect textRect = input.adjusted(8, 0, -8, 0);
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                          QFontMetrics(styled.font).elidedText(styled.text, Qt::ElideRight, textRect.width()));

        if (invalid)
        {
            QFont errorFont = styled.font;
            errorFont.setPixelSize(11);
            painter->setFont(errorFont);
            painter->drawText(option.rect.adjusted(16, 35, -10, -2), Qt::AlignLeft | Qt::AlignVCenter,
                              QFontMetrics(errorFont).elidedText(index.data(Qt::UserRole + 4).toString(), Qt::ElideRight,
                                                                option.rect.width() - 26));
        }
        if (!editable && !isInspector && (option.state & QStyle::State_HasFocus))
        {
            painter->setPen(QPen(QColor("#939bf0"), 1, Qt::DotLine));
            painter->setBrush(Qt::NoBrush);
            painter->drawRoundedRect(input, 5, 5);
        }
        painter->restore();
    }

    /** @brief Align the native payload editor with the painted input surface. */
    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option,
                              const QModelIndex &index) const override
    {
        if (option.widget && option.widget->objectName() == "payloadFields")
        {
            editor->setGeometry(QRect(option.rect.left() + 8, option.rect.top() + 4, option.rect.width() - 16, 32));
            return;
        }
        QStyledItemDelegate::updateEditorGeometry(editor, option, index);
    }

    /** @brief Forward return/enter key from combo line edit to commit the active editor. */
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() == QEvent::KeyPress)
        {
            auto *keyEvent = static_cast<QKeyEvent *>(event);
            if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter)
            {
                if (auto *line = qobject_cast<QLineEdit *>(object))
                {
                    if (auto *combo = qobject_cast<QComboBox *>(line->parentWidget()))
                    {
                        emit commitData(combo);
                        emit closeEditor(combo, QAbstractItemDelegate::SubmitModelCache);
                        return true;
                    }
                }
            }
        }
        return QStyledItemDelegate::eventFilter(object, event);
    }

    /** @brief Create a standard text editor or editable dropdown for enum fields. */
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override
    {
        if (option.widget && option.widget->objectName() == "inspector")
        {
            return nullptr;
        }
        if (option.widget && option.widget->objectName() == "payloadFields")
        {
            const QStringList suggestions = index.data(Qt::UserRole + 7).toStringList();
            if (!suggestions.isEmpty())
            {
                auto *combo = new WorkbenchComboBox(parent);
                combo->setObjectName("payloadValueCombo");
                combo->setAccessibleName(index.sibling(index.row(), 0).data().toString());

                QFont comboFont = parent ? parent->window()->font() : QApplication::font();
                if (parent && parent->window())
                {
                    if (const auto *compid = parent->window()->findChild<QComboBox *>("messageCompId"))
                    {
                        comboFont = compid->font();
                    }
                }
                combo->setFont(comboFont);
                combo->setEditable(true);
                const QVariantList tooltips = index.data(Qt::UserRole + 8).toList();
                for (int sIdx = 0; sIdx < suggestions.size(); ++sIdx)
                {
                    const QString &sName = suggestions[sIdx];
                    combo->addItem(sName);
                    if (sIdx < tooltips.size())
                    {
                        combo->setItemData(sIdx, tooltips[sIdx].toString(), Qt::ToolTipRole);
                    }
                    else
                    {
                        const auto valOpt = Mav::lookupEnumValue(sName.toStdString());
                        if (valOpt.has_value())
                        {
                            combo->setItemData(sIdx, QString::number(*valOpt), Qt::ToolTipRole);
                        }
                    }
                }
                if (auto *line = combo->lineEdit())
                {
                    line->setObjectName("payloadValueEditor");
                    line->setAccessibleName(index.sibling(index.row(), 0).data().toString());
                    line->setFont(comboFont);
                    line->setTextMargins(0, 0, 18, 0);
                    auto *completer = new QCompleter(suggestions, line);
                    completer->setCaseSensitivity(Qt::CaseInsensitive);
                    completer->setFilterMode(Qt::MatchContains);
                    line->setCompleter(completer);
                    line->installEventFilter(const_cast<ValueDelegate *>(this));
                }
                if (combo->view())
                {
                    combo->view()->setFont(comboFont);
                }
                QObject::connect(combo, QOverload<int>::of(&QComboBox::activated), combo, [this, combo](int)
                {
                    const_cast<ValueDelegate *>(this)->commitData(combo);
                    const_cast<ValueDelegate *>(this)->closeEditor(combo, QAbstractItemDelegate::SubmitModelCache);
                });
                return combo;
            }

            QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
            font.setPixelSize(13);
            auto *editor = QStyledItemDelegate::createEditor(parent, option, index);
            if (editor)
            {
                editor->setObjectName("payloadValueEditor");
                editor->setAccessibleName(index.sibling(index.row(), 0).data().toString());
                editor->setFont(font);
            }
            return editor;
        }
        return QStyledItemDelegate::createEditor(parent, option, index);
    }

    /** @brief Prepopulate the editor with the current table value. */
    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        auto *combo = qobject_cast<QComboBox *>(editor);
        if (!combo && editor)
        {
            combo = qobject_cast<QComboBox *>(editor->parentWidget());
        }
        if (combo)
        {
            const QString current = index.data(Qt::EditRole).toString().trimmed();
            int found = combo->findText(current, Qt::MatchFixedString);
            if (found < 0)
            {
                for (int i = 0; i < combo->count(); ++i)
                {
                    const QString itemText = combo->itemText(i);
                    if (itemText == current || itemText.startsWith(current + " ") || itemText.startsWith(current + "("))
                    {
                        found = i;
                        break;
                    }
                }
            }
            if (found >= 0)
            {
                combo->setCurrentIndex(found);
            }
            else
            {
                combo->setEditText(current);
            }
            if (combo->lineEdit())
            {
                combo->lineEdit()->selectAll();
            }
            return;
        }
        QStyledItemDelegate::setEditorData(editor, index);
    }

    /** @brief Write the chosen or typed value back to the payload model. */
    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override
    {
        auto *combo = qobject_cast<QComboBox *>(editor);
        if (!combo && editor)
        {
            combo = qobject_cast<QComboBox *>(editor->parentWidget());
        }
        if (combo)
        {
            QString text = combo->currentText().trimmed();
            const int paren = text.indexOf('(');
            if (paren > 0)
            {
                const QString symbol = text.left(paren).trimmed();
                if (Mav::lookupEnumValue(symbol.toStdString()).has_value())
                {
                    text = symbol;
                }
            }

            // Auto-convert numeric value to enum symbol so the table cell displays pure enum names
            const QStringList suggestions = index.data(Qt::UserRole + 7).toStringList();
            if (!suggestions.isEmpty() && !text.isEmpty())
            {
                bool ok = false;
                const qulonglong numVal = text.toULongLong(&ok, 0);
                if (ok)
                {
                    for (const auto &item : suggestions)
                    {
                        const QString symbol = item.section('(', 0, 0).trimmed();
                        const auto valOpt = Mav::lookupEnumValue(symbol.toStdString());
                        if (valOpt.has_value() && *valOpt == numVal)
                        {
                            text = symbol;
                            break;
                        }
                    }
                }
            }

            model->setData(index, text, Qt::EditRole);
            return;
        }
        QStyledItemDelegate::setModelData(editor, model, index);
    }

protected:
    /** @brief Emphasize values while leaving native selection, focus and editing intact. */
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        const bool dark = option->palette.color(QPalette::Window).lightness() < 128;
        option->font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        option->font.setBold(false);
        option->font.setPixelSize(13);
        const bool changed = option->widget && option->widget->objectName() == "inspector" &&
                             index.data(Qt::UserRole + 5).toBool();
        option->palette.setColor(QPalette::Text, QColor(changed ? (dark ? "#58a6ff" : "#0969da") :
                                                                 (dark ? "#b2c8e2" : "#3f628b")));
        option->backgroundBrush = QColor(dark ? "#1b2b3e" : "#f4f7fc");
        if (option->widget && option->widget->objectName() == "payloadFields")
        {
            option->font.setPixelSize(13);
            option->text.replace('\n', QString::fromUtf8(" ↵ "));
            if (!(index.flags() & Qt::ItemIsEditable))
            {
                option->text += "  · Read-only";
                option->palette.setColor(QPalette::Text, QColor(dark ? "#aab4cd" : "#626c84"));
                option->backgroundBrush = option->palette.color(QPalette::AlternateBase);
            }
            if (index.data(Qt::UserRole + 1).toBool())
            {
                option->palette.setColor(QPalette::Text, QColor(dark ? "#ffb3bd" : "#b42340"));
                option->palette.setColor(QPalette::HighlightedText, QColor(dark ? "#ffb3bd" : "#b42340"));
                option->backgroundBrush = QColor(dark ? "#482e3f" : "#fff0f1");
            }
        }
    }
};

/** @brief Map receive age to a readable status without exposing a countdown. */
static QString rxStatus(double age)
{
    return age <= 1.2 ? QString("Active") : age <= 3.0 ? QString("Stale") : QString("Inactive");
}

/** @brief Paint a status dot as a scalable vector decoration. */
class RxStatusIcon : public QIconEngine
{
public:
    /** @brief Retain the theme-aware dot color. */
    explicit RxStatusIcon(const QColor &color) : color(color)
    {
    }

    /** @brief Clone the icon engine for Qt icon ownership. */
    QIconEngine *clone() const override
    {
        return new RxStatusIcon(color);
    }

    /** @brief Draw a small antialiased dot at the requested icon size. */
    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State) override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        QColor fill = color;
        if (mode == QIcon::Disabled)
        {
            fill.setAlpha(100);
        }
        painter->setPen(Qt::NoPen);
        painter->setBrush(fill);
        const qreal radius = qMin(rect.width(), rect.height()) / 3.0;
        painter->drawEllipse(QPointF(rect.center()), radius, radius);
        painter->restore();
    }

private:
    QColor color;
};

/**
 * @brief Delegate for RX tree items preventing focus rectangle outlines.
 */
class RxItemDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    /**
     * @brief Paint cells while suppressing dotted focus outlines.
     * @param painter Active painter.
     * @param option Style options.
     * @param index Model index.
     */
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt(option);
        opt.state &= ~QStyle::State_HasFocus;
        QStyledItemDelegate::paint(painter, opt, index);
    }

protected:
    /** @brief Decorate Message with a theme-aware status dot while retaining native text layout. */
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        if (index.column() != 0 || !index.data(Qt::UserRole + 2).isValid())
        {
            return;
        }
        static const QIcon icons[] =
        {
            QIcon(new RxStatusIcon(QColor("#1a7f37"))),
            QIcon(new RxStatusIcon(QColor("#9a6700"))),
            QIcon(new RxStatusIcon(QColor("#6e7781"))),
            QIcon(new RxStatusIcon(QColor("#3fb950"))),
            QIcon(new RxStatusIcon(QColor("#d29922"))),
            QIcon(new RxStatusIcon(QColor("#8b949e")))
        };
        const double age = index.data(Qt::UserRole + 2).toDouble();
        const int status = age <= 1.2 ? 0 : age <= 3.0 ? 1 : 2;
        const bool dark = option->palette.color(QPalette::Window).lightness() < 128;
        option->icon = icons[(dark ? 3 : 0) + status];
        option->decorationSize = QSize(12, 12);
        option->features |= QStyleOptionViewItem::HasDecoration;
    }
};

/** @brief Keep a locked RX column layout fitted to the viewport. */
class RxHeader : public QHeaderView
{
public:
    /** @brief Disable user resizing and keep all width changes under layout control. */
    explicit RxHeader(QWidget *parent) : QHeaderView(Qt::Horizontal, parent)
    {
        setSectionsMovable(false);
        setSectionsClickable(false);
        setHighlightSections(false);
        setSectionResizeMode(QHeaderView::Fixed);
        setStretchLastSection(false);
        setMinimumSectionSize(1);
        setFixedHeight(36);
        connect(this, &QHeaderView::sectionResized, this, [this]()
        {
            resetColumns();
        });
    }

    /** @brief Apply the fixed column distribution at the current viewport width. */
    void resetColumns()
    {
        if (adjusting || count() != 3 || !isVisible())
        {
            return;
        }
        adjusting = true;
        const int available = qMax(viewport()->width(), 3);
        const std::array<int, 3> minima{180, 90, 80};
        std::array<int, 3> widths{};
        if (available >= 350)
        {
            widths[0] = qBound(180, available / 2, available - 170);
            const int surplus = available - widths[0] - 170;
            for (int column = 1; column < 3; ++column)
            {
                widths[column] = minima[column] + surplus / 2 + (column <= surplus % 2 ? 1 : 0);
            }
        }
        else
        {
            int remaining = available;
            for (int column = 0; column < 2; ++column)
            {
                widths[column] = qMax(1, minima[column] * available / 350);
                remaining -= widths[column];
            }
            widths[2] = remaining;
        }
        for (int column = 0; column < 3; ++column)
        {
            resizeSection(column, widths[column]);
        }
        adjusting = false;
    }

protected:
    /** @brief Fit the locked layout after visible geometry is available. */
    void showEvent(QShowEvent *event) override
    {
        QHeaderView::showEvent(event);
        resetColumns();
    }

    /** @brief Keep the locked layout fitted when the viewport changes width. */
    void resizeEvent(QResizeEvent *event) override
    {
        QHeaderView::resizeEvent(event);
        resetColumns();
    }

private:
    bool adjusting = false;
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
    else if (name == "stopTx")
    {
        painter.drawRoundedRect(QRectF(4, 4, 8, 8), 1, 1);
    }
    else if (name == "runAll")
    {
        painter.setBrush(color);
        QPainterPath first;
        first.moveTo(3.0, 4.2);
        first.lineTo(7.5, 8.0);
        first.lineTo(3.0, 11.8);
        first.closeSubpath();
        painter.drawPath(first);

        QPainterPath second;
        second.moveTo(8.5, 4.2);
        second.lineTo(13.0, 8.0);
        second.lineTo(8.5, 11.8);
        second.closeSubpath();
        painter.drawPath(second);
    }
    else if (name == "stopAll")
    {
        painter.setBrush(color);
        painter.drawRoundedRect(QRectF(3.5, 3.5, 9.0, 9.0), 2.0, 2.0);
    }
    else if (name == "deleteAll")
    {
        painter.setPen(QPen(color, 1.35, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);

        // Handle on top: rounded top corners resting on lid
        QPainterPath handle;
        handle.moveTo(6.2, 3.4);
        handle.lineTo(6.2, 1.8);
        handle.quadTo(6.2, 1.2, 6.9, 1.2);
        handle.lineTo(9.1, 1.2);
        handle.quadTo(9.8, 1.2, 9.8, 1.8);
        handle.lineTo(9.8, 3.4);
        painter.drawPath(handle);

        // Lid: rounded rectangle
        painter.drawRoundedRect(QRectF(2.6, 3.4, 10.8, 2.2), 0.9, 0.9);

        // Body: connects directly to lid bottom with rounded bottom corners
        QPainterPath body;
        body.moveTo(3.8, 5.6);
        body.lineTo(3.8, 12.2);
        body.quadTo(3.8, 13.8, 5.4, 13.8);
        body.lineTo(10.6, 13.8);
        body.quadTo(12.2, 13.8, 12.2, 12.2);
        body.lineTo(12.2, 5.6);
        painter.drawPath(body);

        // Three vertical parallel ribs
        painter.drawLine(QPointF(5.8, 7.4), QPointF(5.8, 11.8));
        painter.drawLine(QPointF(8.0, 7.4), QPointF(8.0, 11.8));
        painter.drawLine(QPointF(10.2, 7.4), QPointF(10.2, 11.8));
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
        QDialog dialog(this);
        dialog.setObjectName("aboutDialog");
        dialog.setWindowTitle("About MAVLink Simulator");
        auto *layout = new QVBoxLayout(&dialog);
        layout->setContentsMargins(20, 16, 20, 16);
        layout->setSpacing(12);
        layout = addDialogTitleBar(&dialog, layout);
        dialog.setMinimumWidth(480);
        auto *version = new QLabel(QString("Version %1").arg(MAVLINK_SIMULATOR_VERSION), &dialog);
        version->setObjectName("secondaryText");
        layout->addWidget(version);

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

        auto *mavVersion = new QLabel(QString("MAVLink library: %1").arg(mavlinkLib), &dialog);
        mavVersion->setObjectName("secondaryText");
        layout->addWidget(mavVersion);

        auto *details = new QLabel(
            QString(
                "<p>A desktop workbench for sending, receiving and inspecting MAVLink messages over serial UART.</p>"
                "<p><b>Features</b><br>"
                "Configurable serial connection<br>"
                "Periodic and single-message transmission<br>"
                "Live message rates and frame counters<br>"
                "RX streams grouped by System and Component<br>"
                "Decoded payload field inspection</p>"
                "<p><b>Protocol</b><br>MAVLink 1 and 2<br>"
                "Dialect: ardupilotmega (including common)<br>"
                "Official MAVLink C library (%1)</p>"
                "<p><b>Platform</b><br>Ubuntu / Linux<br>"
                "C++17 · Qt Widgets · Qt SerialPort</p>").arg(mavlinkLib), &dialog);
        details->setObjectName("aboutDetails");
        details->setWordWrap(true);
        details->setTextInteractionFlags(Qt::TextSelectableByMouse);
        details->ensurePolished();
        details->setMinimumHeight(details->heightForWidth(440) + details->fontMetrics().lineSpacing());
        layout->addWidget(details);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        layout->addWidget(buttons);
        dialog.exec();
    });
    aboutAction->setObjectName("actionAbout");
    for (auto *menu : {fileMenu, viewMenu, toolMenu, helpMenu})
    {
        menu->setAttribute(Qt::WA_TranslucentBackground, true);
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
    txGroup->setMinimumWidth(340);
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
    auto *txHeading = new QVBoxLayout;
    txHeading->setSpacing(6);
    txHeading->addLayout(txHeader);
    txSummary = new QLabel("0 messages  ·  0 running");
    txSummary->setObjectName("secondaryText");
    auto *txOverview = new QHBoxLayout;
    txOverview->addWidget(txSummary);
    txOverview->addStretch();
    txHeading->addLayout(txOverview);
    txLayout->addLayout(txHeading);
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
    txTable->verticalHeader()->setDefaultSectionSize(74);
    txTable->setAlternatingRowColors(false);
    txTable->setMouseTracking(true);
    txTable->viewport()->setMouseTracking(true);
    txTable->viewport()->installEventFilter(this);
    txTable->setAccessibleName("Transmit message cards");
    txLayout->addWidget(txTable, 1);
    txMenu = new QMenu(txTable);
    txMenu->setObjectName("txMessageMenu");
    txMenu->setAttribute(Qt::WA_TranslucentBackground, true);
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
    removeAction = addTxAction("Remove message", "removeMessage");
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
    auto *batchDivider = new QFrame;
    batchDivider->setObjectName("txBatchDivider");
    batchDivider->setFixedHeight(1);
    txLayout->addWidget(batchDivider);
    auto *batchActions = new QHBoxLayout;
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

    txLayout->addLayout(batchActions);
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
    rxFilter = new CatalogSearch;
    rxFilter->setObjectName("rxFilter");
    rxFilter->setPlaceholderText("Search messages or sender…");
    rxFilter->setClearButtonEnabled(true);
    rxFilter->setMinimumWidth(140);
    rxFilter->setAccessibleName("Filter received MAVLink streams");
    rxActions->addWidget(rxFilter);
    auto *pause = button("Pause display", "pauseDisplay", rxActions);
    pause->setCheckable(true);
    pause->setMinimumWidth(136);
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
    auto *valueMenu = new QMenu(inspector);
    valueMenu->setObjectName("inspectorMenu");
    valueMenu->setAttribute(Qt::WA_TranslucentBackground, true);
    valueMenu->installEventFilter(this);
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
        copyValue->setData(value->text());
        const QPoint anchor = keyboard ? inspector->visualItemRect(value).center() : position;
        valueMenu->popup(inspector->viewport()->mapToGlobal(anchor));
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

    headerLayout->addStretch(1);

    pausedLabel = new QLabel("Display paused");
    pausedLabel->setObjectName("displayPaused");
    pausedLabel->hide();
    headerLayout->addWidget(pausedLabel);

    inspectorLayout->addWidget(inspectorHeader);
    inspectorLayout->addWidget(inspector, 1);
    rxSplit->addWidget(inspectorPane);
    rxSplit->setSizes({300, 300});
    rxLayout->addWidget(rxSplit, 1);
    dashboards->addWidget(rxGroup);
    dashboards->setSizes({488, 892});
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
            // Persist the serial port and baudrate of the last successful connection
            QSettings settings("MAVLinkSimulator", "Preferences");
            if (portBox)
            {
                settings.setValue("serial/port", portBox->currentText());
            }
            if (baudBox)
            {
                settings.setValue("serial/baud", baudBox->currentText());
            }
            settings.sync();

            rxTree->clear();
            inspector->setRowCount(0);
            lastError->setText("Connected");
        }
        else if (reconnecting)
        {
            lastError->setText("Link lost. Reconnecting to " + portBox->currentText() + "...");
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
    connect(editAction, &QAction::triggered, this, [this]()
    {
        editMessage(true);
    });
    connect(removeAction, &QAction::triggered, this, [this]()
    {
        QMetaObject::invokeMethod(worker, "removeEntry", Qt::QueuedConnection, Q_ARG(quint64, selectedTx()));
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
    connect(deleteAllButton, &QPushButton::clicked, worker, &SerialWorker::deleteAll);
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
        pause->setText(checked ? "Resume display" : "Pause display");
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
    connect(restoreTxBox, &QCheckBox::toggled, this, [this]()
    {
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
        dashboards->setSizes({488, 892});
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
    if (lastError)
    {
        lastError->setText("Layout reset to defaults");
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
}

/** @brief Save application preferences such as theme and MAVLink defaults to storage. */
void MainWindow::saveSettings()
{
    QSettings settings("MAVLinkSimulator", "Preferences");
    if (themeBox)
    {
        settings.setValue("theme", themeBox->currentIndex());
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
    if (deassertControlLinesBox)
    {
        settings.setValue("serial/deassertControlLines", deassertControlLinesBox->isChecked());
    }
    settings.sync();
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

/** @brief Save only geometry, splitters and column presentation on a normal close. */
void MainWindow::closeEvent(QCloseEvent *event)
{
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
    QMainWindow::closeEvent(event);
}

MainWindow::~MainWindow()
{
    refresh.stop();
    QMetaObject::invokeMethod(worker, "closePort", Qt::BlockingQueuedConnection);
    thread.quit();
    thread.wait();
}

/** @brief Refresh the port dropdown and retain its selection only if still enumerated. */
void MainWindow::refreshPorts()
{
    if (connected || connecting || reconnecting)
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
            const QString desc = info.description();
            const QString mfg = info.manufacturer();
            const QString tip = (!desc.isEmpty() || !mfg.isEmpty()) ?
                                (desc + " · " + mfg) : info.systemLocation();
            portBox->setItemData(portBox->count() - 1, tip, Qt::ToolTipRole);
        }
    }
    if (!previous.isEmpty() && portBox->findText(previous) >= 0)
    {
        portBox->setCurrentText(previous);
    }
}

void MainWindow::toggleConnection()
{
    if (connected || reconnecting)
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
    config.autoReconnect = autoReconnectBox ? autoReconnectBox->isChecked() : false;
    config.deassertControlLines = deassertControlLinesBox ? deassertControlLinesBox->isChecked() : true;
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
    editAction->setEnabled(selected);
    removeAction->setEnabled(selected);
    onceAction->setEnabled(selected && connected);
    startAction->setEnabled(selected && connected);
    stopAction->setEnabled(selected && connected);
    const bool hasEntries = txTable->rowCount() > 0;
    runAllButton->setEnabled(connected && hasEntries);
    stopAllButton->setEnabled(connected && hasEntries);
    deleteAllButton->setEnabled(hasEntries);
    const int row = txTable->currentRow();
    const bool running = selected && txTable->item(row, 6) && txTable->item(row, 6)->text() == "Running";
    startAction->setVisible(!running);
    stopAction->setVisible(running);

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

/** @brief Configure message identity, sender and payload with contextual validation feedback. */
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
        entry.sysid = static_cast<uint8_t>(defaultSysIdBox ? defaultSysIdBox->value() : 1);
        entry.compid = static_cast<uint8_t>(defaultCompIdBox ? defaultCompIdBox->value() : 1);
        entry.version1 = (defaultVersionBox && defaultVersionBox->currentIndex() == 1);
    }
    QDialog dialog(this);
    dialog.setObjectName("messageDialog");
    dialog.setWindowTitle(edit ? "Edit message" : "Add MAVLink message");
    dialog.setMinimumSize(880, 600);
    dialog.resize(1000, 680);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(12);
    layout = addDialogTitleBar(&dialog, layout);
    if (edit)
    {
        auto *description = new QLabel("Saving stops this TX entry. Review its payload before restarting.");
        description->setObjectName("secondaryText");
        layout->addWidget(description);
    }
    auto *body = new QSplitter(Qt::Horizontal);
    body->setObjectName("messageSplitter");
    body->setChildrenCollapsible(false);
    body->setHandleWidth(2);
    auto *catalogWidget = new QWidget;
    catalogWidget->setMinimumWidth(240);
    auto *catalogPane = new QVBoxLayout(catalogWidget);
    catalogPane->setContentsMargins(0, 0, 0, 0);
    catalogPane->setSpacing(10);
    auto *catalogHeading = new QHBoxLayout;
    auto *catalogTitle = new QLabel("MESSAGE CATALOG");
    catalogTitle->setObjectName("sectionTitle");
    catalogHeading->addWidget(catalogTitle);
    auto *resultCount = new QLabel;
    resultCount->setObjectName("catalogResultCount");
    resultCount->setMinimumWidth(82);
    resultCount->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    catalogHeading->addStretch();
    catalogHeading->addWidget(resultCount);
    catalogPane->addLayout(catalogHeading);
    auto *search = new CatalogSearch;
    search->setPlaceholderText("Search ID or name…");
    search->setAccessibleName("Search MAVLink catalog");
    search->setObjectName("catalogSearch");
    catalogPane->addWidget(search);
    auto *catalogList = new QListWidget;
    catalogList->setObjectName("messageCatalog");
    catalogList->setItemDelegate(new CatalogDelegate(catalogList));
    catalogList->setMouseTracking(true);
    catalogList->setTextElideMode(Qt::ElideRight);
    catalogList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    for (const auto &info : Mav::catalog())
    {
        auto *item = new CatalogItem(QString("[%1] %2").arg(info.msgid).arg(info.name), catalogList);
        item->setData(Qt::UserRole, info.msgid);
        item->setData(Qt::UserRole + 3, QString::fromLatin1(info.name));
        item->setToolTip(item->text());
    }
    catalogPane->addWidget(catalogList, 1);
    auto *noMatches = new QLabel("No matching messages");
    noMatches->setObjectName("catalogEmpty");
    noMatches->setAlignment(Qt::AlignCenter);
    noMatches->hide();
    catalogPane->addWidget(noMatches, 1);
    resultCount->setText(QString::number(catalogList->count()) + " results");
    body->addWidget(catalogWidget);
    auto *editorWidget = new QWidget;
    editorWidget->setMinimumWidth(540);
    auto *editor = new QVBoxLayout(editorWidget);
    editor->setContentsMargins(0, 0, 0, 0);
    editor->setSpacing(10);
    auto *messageTitle = new QLabel;
    messageTitle->setObjectName("selectedMessageTitle");
    messageTitle->setWordWrap(true);
    editor->addWidget(messageTitle);
    auto *form = new QHBoxLayout;
    auto *sysid = new WorkbenchSpinBox<QSpinBox>;
    sysid->setObjectName("messageSysId");
    auto *compid = new WorkbenchComboBox;
    compid->setObjectName("messageCompId");
    compid->setEditable(true);

    const auto &compEntries = Mav::enumEntries("MAV_COMPONENT");
    QStringList compSuggestions;
    compSuggestions.reserve(int(compEntries.size()));
    for (const auto &entryItem : compEntries)
    {
        const QString name = QString::fromLatin1(entryItem.name);
        compSuggestions << name;
        compid->addItem(name, QVariant::fromValue<qulonglong>(entryItem.value));
        compid->setItemData(compid->count() - 1, QString::number(entryItem.value), Qt::ToolTipRole);
    }

    if (auto *line = compid->lineEdit())
    {
        line->setObjectName("messageCompIdEditor");
        line->setTextMargins(0, 0, 18, 0);
        auto *completer = new QCompleter(compSuggestions, line);
        completer->setCaseSensitivity(Qt::CaseInsensitive);
        completer->setFilterMode(Qt::MatchContains);
        line->setCompleter(completer);
    }

    const auto updateCompIdTooltipAndNormalize = [compid, &compEntries](bool normalizeNumber)
    {
        QString text = compid->currentText().trimmed();
        if (text.isEmpty())
        {
            compid->setToolTip(QString());
            return;
        }

        const int paren = text.indexOf('(');
        if (paren > 0)
        {
            text = text.left(paren).trimmed();
        }

        const Mav::EnumEntry *matched = nullptr;
        bool ok = false;
        const uint64_t numVal = text.toULongLong(&ok, 0);

        if (ok)
        {
            for (const auto &e : compEntries)
            {
                if (e.value == numVal)
                {
                    matched = &e;
                    break;
                }
            }
            if (normalizeNumber && matched)
            {
                const QSignalBlocker blocker(compid);
                compid->setEditText(QString::fromLatin1(matched->name));
            }
        }
        else
        {
            for (const auto &e : compEntries)
            {
                if (text.compare(e.name, Qt::CaseInsensitive) == 0)
                {
                    matched = &e;
                    break;
                }
            }
        }

        if (matched)
        {
            QString tip = QString("%1 = %2").arg(matched->name).arg(matched->value);
            if (matched->description && *matched->description)
            {
                tip += QString(" (%1)").arg(matched->description);
            }
            compid->setToolTip(tip);
        }
        else if (ok && numVal <= 255)
        {
            compid->setToolTip(QString("Component ID %1").arg(numVal));
        }
        else
        {
            compid->setToolTip(QString());
        }
    };

    const std::string initialCompName = Mav::lookupEnumNameByType("MAV_COMPONENT", entry.compid);
    if (!initialCompName.empty())
    {
        const int initialIdx = compid->findText(QString::fromLatin1(initialCompName));
        if (initialIdx >= 0)
        {
            compid->setCurrentIndex(initialIdx);
        }
        else
        {
            compid->setEditText(QString::fromLatin1(initialCompName));
        }
    }
    else
    {
        const int initialIdx = compid->findData(quint64(entry.compid));
        if (initialIdx >= 0)
        {
            compid->setCurrentIndex(initialIdx);
        }
        else
        {
            compid->setEditText(QString::number(entry.compid));
        }
    }
    updateCompIdTooltipAndNormalize(false);

    QObject::connect(compid, &QComboBox::currentTextChanged, [updateCompIdTooltipAndNormalize](const QString &)
    {
        updateCompIdTooltipAndNormalize(false);
    });
    if (auto *line = compid->lineEdit())
    {
        QObject::connect(line, &QLineEdit::editingFinished, [updateCompIdTooltipAndNormalize]()
        {
            updateCompIdTooltipAndNormalize(true);
        });
    }
    QObject::connect(compid, QOverload<int>::of(&QComboBox::activated), [updateCompIdTooltipAndNormalize](int)
    {
        updateCompIdTooltipAndNormalize(true);
    });

    sysid->setRange(1, 255);
    sysid->setValue(entry.sysid);
    auto *version = new WorkbenchComboBox;
    version->setObjectName("messageVersion");
    version->addItems({"MAVLink v2", "MAVLink v1"});
    version->setCurrentIndex(entry.version1 ? 1 : 0);
    auto *rate = new WorkbenchSpinBox<QDoubleSpinBox>;
    rate->setDecimals(0);
    rate->setRange(1, 1000);
    rate->setSingleStep(1);
    rate->setValue(entry.hz);
    const auto fieldControl = [&](const QString &name, QWidget *control, int stretch = 1)
    {
        auto *field = new QVBoxLayout;
        field->setSpacing(6);
        auto *label = new QLabel(name);
        label->setObjectName("serialFieldLabel");
        label->setBuddy(control);
        field->addWidget(label);
        control->setFixedHeight(34);
        control->setMinimumWidth(0);
        control->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        field->addWidget(control);
        control->setAccessibleName(name);
        form->addLayout(field, stretch);
    };
    fieldControl("System ID", sysid, 1);
    fieldControl("Component ID", compid, 2);
    form->addSpacing(14);
    fieldControl("Rate (Hz)", rate, 1);
    editor->addLayout(form);
    auto *parameterSearch = new CatalogSearch;
    parameterSearch->setObjectName("parameterSearch");
    parameterSearch->setPlaceholderText("Filter parameters…");
    parameterSearch->setAccessibleName("Filter message parameters");
    parameterSearch->hide();
    editor->addWidget(parameterSearch);
    auto *fields = table({"Parameter", "Type", "Value"});
    fields->setObjectName("payloadFields");
    fields->setItemDelegateForColumn(0, new PayloadIdentityDelegate(fields));
    fields->setItemDelegateForColumn(1, new PayloadIdentityDelegate(fields));
    fields->setEditTriggers(QAbstractItemView::AllEditTriggers);
    fields->setSelectionBehavior(QAbstractItemView::SelectItems);
    fields->setAlternatingRowColors(false);
    fields->setMouseTracking(true);
    fields->verticalHeader()->setDefaultSectionSize(40);
    fields->horizontalHeader()->setFixedHeight(36);
    fields->horizontalHeader()->setMinimumSectionSize(90);
    fields->setColumnWidth(0, 190);
    fields->setColumnWidth(1, 105);
    editor->addWidget(fields, 1);
    auto *hint = new QLabel;
    hint->setWordWrap(true);
    hint->setObjectName("payloadHint");
    hint->hide();
    auto *hintRow = new QHBoxLayout;
    hintRow->addWidget(hint, 1);
    auto *arrayButton = new QPushButton("Edit array…");
    arrayButton->setObjectName("editPayloadArray");
    arrayButton->setAutoDefault(false);
    arrayButton->hide();
    hintRow->addWidget(arrayButton);
    editor->addLayout(hintRow);
    auto *error = new QLabel;
    error->setObjectName("validationError");
    error->setWordWrap(true);
    error->hide();
    editor->addWidget(error);
    body->addWidget(editorWidget);
    body->setSizes({270, 673});
    body->setStretchFactor(0, 0);
    body->setStretchFactor(1, 1);
    layout->addWidget(body, 1);
    auto *footerDivider = new QFrame;
    footerDivider->setObjectName("messageFooterDivider");
    footerDivider->setFixedHeight(1);
    layout->addWidget(footerDivider);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->setLayoutDirection(Qt::RightToLeft);
    QWidget::setTabOrder(buttons->button(QDialogButtonBox::Cancel), buttons->button(QDialogButtonBox::Save));
    auto *saveButton = buttons->button(QDialogButtonBox::Save);
    saveButton->setProperty("role", "primary");
    saveButton->setMinimumWidth(120);
    saveButton->setText(edit ? "Save changes" : "Add message");
    saveButton->setToolTip("Ctrl+Click to fill empty fields with 0");
    auto *footerRow = new QHBoxLayout;
    footerRow->setContentsMargins(0, 0, 0, 0);
    footerRow->setSpacing(12);

    auto *protocolLayout = new QHBoxLayout;
    protocolLayout->setSpacing(8);
    auto *protocolLabel = new QLabel("MAVLink Version:");
    protocolLabel->setObjectName("secondaryText");
    protocolLabel->setBuddy(version);
    protocolLayout->addWidget(protocolLabel, 0, Qt::AlignVCenter);
    version->setFixedWidth(150);
    version->setFixedHeight(32);
    version->setAccessibleName("MAVLink Version");
    protocolLayout->addWidget(version, 0, Qt::AlignVCenter);
    footerRow->addLayout(protocolLayout);

    footerRow->addStretch();
    buttons->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    footerRow->addWidget(buttons, 0, Qt::AlignRight | Qt::AlignVCenter);
    layout->addLayout(footerRow);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    const mavlink_message_info_t *selectedInfo = nullptr;
    std::map<uint32_t, QStringList> drafts;
    bool loadingFields = false;
    bool submissionAttempted = false;
    // Commit an active native editor before saving a draft or opening another editor.
    const auto commitEditor = [&]()
    {
        auto *active = fields->findChild<QLineEdit *>("payloadValueEditor");
        if (active && active->isVisible())
        {
            auto *delegate = fields->itemDelegateForColumn(2);
            QWidget *target = active;
            if (auto *combo = qobject_cast<QComboBox *>(active->parentWidget()))
            {
                target = combo;
            }
            emit delegate->commitData(target);
            emit delegate->closeEditor(target, QAbstractItemDelegate::NoHint);
        }
    };
    const auto validateField = [&](int row)
    {
        auto *item = fields->item(row, 2);
        if (!selectedInfo || !item || !(item->flags() & Qt::ItemIsEditable))
        {
            return true;
        }
        const auto &enumEntries = Mav::fieldEnumEntries(selectedInfo->msgid, selectedInfo->fields[row].name);
        const QString currentText = item->text().trimmed();
        const auto &field = selectedInfo->fields[row];
        const bool isCharArray = (field.type == MAVLINK_TYPE_CHAR && field.array_length);

        if (currentText.isEmpty() && !isCharArray)
        {
            const QSignalBlocker blocker(fields);
            const bool showError = submissionAttempted;
            item->setData(Qt::UserRole + 1, showError);
            const QString emptyReason = !enumEntries.empty() ?
                "Value required: please select or enter an enum name" :
                "Value required: please enter a value";
            item->setData(Qt::UserRole + 4, showError ? emptyReason : QString());
            item->setData(Qt::AccessibleDescriptionRole, showError ? emptyReason : QString());
            item->setToolTip(emptyReason);
            fields->setRowHeight(row, showError ? 60 : 40);
            return false;
        }

        if (!enumEntries.empty())
        {
            bool ok = false;
            const qulonglong numVal = currentText.toULongLong(&ok, 0);
            if (ok)
            {
                const std::string enumSymbol = Mav::lookupEnumName(selectedInfo->msgid, selectedInfo->fields[row].name, numVal);
                if (!enumSymbol.empty() && currentText != QString::fromLatin1(enumSymbol))
                {
                    const QSignalBlocker blocker(fields);
                    item->setText(QString::fromLatin1(enumSymbol));
                }
            }
        }
        const auto *metadata = mavlink_get_msg_entry(selectedInfo->msgid);
        Mav::Bytes probe(metadata->max_msg_len, 0);
        std::string reason;
        const bool valid = Mav::setField(probe, selectedInfo->fields[row], item->text().toUtf8().toStdString(), reason);
        const QSignalBlocker blocker(fields);
        item->setData(Qt::UserRole + 1, !valid);
        item->setData(Qt::UserRole + 4, QString::fromStdString(reason));
        item->setData(Qt::AccessibleDescriptionRole, QString::fromStdString(reason));
        if (valid)
        {
            if (!enumEntries.empty())
            {
                const QString currentText = item->text().trimmed();
                const Mav::EnumEntry *matched = nullptr;
                for (const auto &e : enumEntries)
                {
                    if (currentText.compare(e.name, Qt::CaseInsensitive) == 0)
                    {
                        matched = &e;
                        break;
                    }
                }
                if (!matched)
                {
                    bool ok = false;
                    const uint64_t numVal = currentText.toULongLong(&ok);
                    if (ok)
                    {
                        for (const auto &e : enumEntries)
                        {
                            if (e.value == numVal)
                            {
                                matched = &e;
                                break;
                            }
                        }
                    }
                    else
                    {
                        const auto valOpt = Mav::lookupEnumValue(currentText.toStdString());
                        if (valOpt.has_value())
                        {
                            for (const auto &e : enumEntries)
                            {
                                if (e.value == *valOpt)
                                {
                                    matched = &e;
                                    break;
                                }
                            }
                        }
                    }
                }
                if (matched)
                {
                    QString tip = QString("%1 = %2").arg(matched->name).arg(matched->value);
                    if (matched->description && *matched->description)
                    {
                        tip += QString(" (%1)").arg(matched->description);
                    }
                    item->setToolTip(tip);
                }
                else
                {
                    item->setToolTip(QString());
                }
            }
            else
            {
                item->setToolTip(QString());
            }
        }
        else
        {
            item->setToolTip(QString::fromStdString(reason));
        }
        fields->setRowHeight(row, valid ? 40 : 60);
        return valid;
    };
    const auto filterParameters = [&]()
    {
        const QString query = parameterSearch->text().trimmed();
        for (int row = 0; row < fields->rowCount(); ++row)
        {
            fields->setRowHidden(row, !fields->item(row, 0)->text().contains(query, Qt::CaseInsensitive));
        }
    };
    connect(parameterSearch, &QLineEdit::textChanged, &dialog, filterParameters);
    // Field-specific guidance replaces the permanent multi-line footer instructions.
    connect(fields, &QTableWidget::currentCellChanged, &dialog, [&](int row, int, int, int)
    {
        hint->clear();
        arrayButton->hide();
        if (selectedInfo && row >= 0 && row < int(selectedInfo->num_fields) && fields->item(row, 2))
        {
            const auto &field = selectedInfo->fields[row];
            if (!(fields->item(row, 2)->flags() & Qt::ItemIsEditable))
            {
                hint->setText("Read-only: protocol constant supplied by the official HEARTBEAT packer.");
            }
            else if (field.array_length)
            {
                arrayButton->show();
                hint->setText(field.type == MAVLINK_TYPE_CHAR ?
                    QString("UTF-8 text · maximum %1 bytes.").arg(field.array_length) :
                    QString("Enter exactly %1 comma-separated numeric values.").arg(field.array_length));
            }
            else
            {
                const auto enumType = Mav::fieldEnumType(selectedInfo->msgid, field.name);
                if (!enumType.empty())
                {
                    hint->setText(QString("Enum %1: accept symbolic name or numeric value (e.g. MAV_PARAM_TYPE_REAL32 or 9).")
                                  .arg(QString::fromStdString(enumType)));
                }
            }
        }
        hint->setVisible(!hint->text().isEmpty());
    });
    connect(fields, &QTableWidget::itemChanged, &dialog, [&](QTableWidgetItem *item)
    {
        if (!loadingFields && item->column() == 2)
        {
            validateField(item->row());
            error->clear();
            error->hide();
        }
    });
    connect(arrayButton, &QPushButton::clicked, &dialog, [&]()
    {
        commitEditor();
        const int row = fields->currentRow();
        if (!selectedInfo || row < 0 || row >= int(selectedInfo->num_fields))
        {
            return;
        }
        const auto &field = selectedInfo->fields[row];
        if (!field.array_length)
        {
            return;
        }
        QDialog arrayDialog(&dialog);
        arrayDialog.setObjectName("arrayDialog");
        arrayDialog.setWindowTitle(QString("Edit %1").arg(field.name));
        arrayDialog.resize(620, 380);
        arrayDialog.setMinimumSize(460, 300);
        auto *arrayLayout = new QVBoxLayout(&arrayDialog);
        arrayLayout->setContentsMargins(20, 12, 20, 16);
        arrayLayout = addDialogTitleBar(&arrayDialog, arrayLayout);
        auto *guidance = new QLabel(hint->text());
        guidance->setObjectName("payloadHint");
        guidance->setWordWrap(true);
        arrayLayout->addWidget(guidance);
        auto *input = new QPlainTextEdit;
        input->setObjectName("arrayValueEditor");
        input->setAccessibleName(QString::fromLatin1(field.name));
        input->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        input->setPlainText(fields->item(row, 2)->text());
        arrayLayout->addWidget(input, 1);
        auto *arrayError = new QLabel;
        arrayError->setObjectName("validationError");
        arrayError->setWordWrap(true);
        arrayError->hide();
        arrayLayout->addWidget(arrayError);
        auto *actions = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Save);
        actions->setLayoutDirection(Qt::RightToLeft);
        QWidget::setTabOrder(actions->button(QDialogButtonBox::Cancel), actions->button(QDialogButtonBox::Save));
        actions->button(QDialogButtonBox::Save)->setText("Apply");
        actions->button(QDialogButtonBox::Save)->setProperty("role", "primary");
        arrayLayout->addWidget(actions, 0, Qt::AlignRight | Qt::AlignAbsolute);
        connect(actions, &QDialogButtonBox::rejected, &arrayDialog, &QDialog::reject);
        connect(actions, &QDialogButtonBox::accepted, &arrayDialog, [&]()
        {
            const QString value = input->toPlainText();
            const auto *metadata = mavlink_get_msg_entry(selectedInfo->msgid);
            Mav::Bytes probe(metadata->max_msg_len, 0);
            std::string reason;
            if (!Mav::setField(probe, field, value.toUtf8().toStdString(), reason))
            {
                arrayError->setText(QString::fromStdString(reason));
                arrayError->show();
                input->setFocus();
                return;
            }
            QString compact = value;
            if (field.type != MAVLINK_TYPE_CHAR)
            {
                QStringList parts = value.split(',');
                for (auto &part : parts)
                {
                    part = part.trimmed();
                }
                compact = parts.join(", ");
            }
            fields->item(row, 2)->setText(compact);
            arrayDialog.accept();
        });
        arrayDialog.exec();
    });
    connect(catalogList, &QListWidget::currentItemChanged, &dialog, [&](QListWidgetItem *item)
    {
        commitEditor();
        submissionAttempted = false;
        if (selectedInfo)
        {
            QStringList values;
            for (int row = 0; row < fields->rowCount(); ++row)
            {
                values.append(fields->item(row, 2)->text());
            }
            drafts[selectedInfo->msgid] = values;
        }
        loadingFields = true;
        selectedInfo = item ? mavlink_get_message_info_by_id(item->data(Qt::UserRole).toUInt()) : nullptr;
        fields->setRowCount(0);
        error->clear();
        error->hide();
        hint->hide();
        arrayButton->hide();
        parameterSearch->clear();
        parameterSearch->setVisible(selectedInfo && selectedInfo->num_fields > 12);
        messageTitle->setText(selectedInfo ? QString("[%1] %2").arg(selectedInfo->msgid).arg(selectedInfo->name) : QString());
        if (!selectedInfo)
        {
            loadingFields = false;
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
            QFont nameFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
            nameFont.setPixelSize(13);
            fields->item(int(i), 0)->setFont(nameFont);
            nameFont.setPixelSize(12);
            fields->item(int(i), 1)->setFont(nameFont);
            const auto draft = drafts.find(selectedInfo->msgid);
            const auto &enumEntries = Mav::fieldEnumEntries(selectedInfo->msgid, field.name);
            QString initialValue;
            if (draft != drafts.end())
            {
                initialValue = draft->second.value(int(i));
            }
            else if (edit && selectedInfo->msgid == entry.id)
            {
                initialValue = QString::fromStdString(Mav::readField(source, field));
                if (!enumEntries.empty())
                {
                    bool ok = false;
                    const qulonglong num = initialValue.toULongLong(&ok, 0);
                    if (ok)
                    {
                        const std::string name = Mav::lookupEnumName(selectedInfo->msgid, field.name, num);
                        if (!name.empty())
                        {
                            initialValue = QString::fromLatin1(name);
                        }
                    }
                }
            }
            else
            {
                initialValue = QString();
            }
            cell(fields, int(i), 2, initialValue);
            if (!enumEntries.empty())
            {
                QStringList suggestions;
                QVariantList tooltips;
                suggestions.reserve(int(enumEntries.size()));
                tooltips.reserve(int(enumEntries.size()));
                for (const auto &e : enumEntries)
                {
                    suggestions << QString::fromLatin1(e.name);
                    tooltips << QString::number(e.value);
                }
                fields->item(int(i), 2)->setData(Qt::UserRole + 7, suggestions);
                fields->item(int(i), 2)->setData(Qt::UserRole + 8, tooltips);
            }
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
        loadingFields = false;
        for (int row = 0; row < fields->rowCount(); ++row)
        {
            validateField(row);
        }
        filterParameters();
    });
    connect(search, &QLineEdit::textChanged, &dialog, [=](const QString &query)
    {
        const QSignalBlocker blocker(catalogList);
        auto *current = catalogList->currentItem();
        const QString needle = query.trimmed();
        int count = 0;
        for (int i = 0; i < catalogList->count(); ++i)
        {
            auto *item = catalogList->item(i);
            const QString name = item->data(Qt::UserRole + 3).toString();
            const QString id = QString::number(item->data(Qt::UserRole).toUInt());
            const bool visible = item->text().contains(needle, Qt::CaseInsensitive);
            const bool exact = id == needle || name.compare(needle, Qt::CaseInsensitive) == 0;
            const bool prefix = id.startsWith(needle) || name.startsWith(needle, Qt::CaseInsensitive);
            item->setData(Qt::UserRole + 4, needle.isEmpty() ? 0 : exact ? 0 : prefix ? 1 : 2);
            item->setHidden(!visible);
            count += visible;
        }
        catalogList->sortItems();
        catalogList->setCurrentItem(current);
        catalogList->scrollToTop();
        resultCount->setText(QString::number(count) + " results");
        noMatches->setVisible(count == 0);
        catalogList->setVisible(count != 0);
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]()
    {
        // Commit an active table editor before reading its value.
        commitEditor();

        // Hold Ctrl while clicking to automatically populate remaining empty fields with 0
        if (QApplication::keyboardModifiers() & Qt::ControlModifier)
        {
            for (int row = 0; row < fields->rowCount(); ++row)
            {
                auto *item = fields->item(row, 2);
                if (item && (item->flags() & Qt::ItemIsEditable) && item->text().trimmed().isEmpty())
                {
                    QString zeroValue = "0";
                    if (selectedInfo && row < int(selectedInfo->num_fields))
                    {
                        const auto &field = selectedInfo->fields[row];
                        if (field.array_length > 1 && field.type != MAVLINK_TYPE_CHAR)
                        {
                            QStringList zeros;
                            for (unsigned n = 0; n < field.array_length; ++n)
                            {
                                zeros.append("0");
                            }
                            zeroValue = zeros.join(", ");
                        }
                    }
                    item->setText(zeroValue);
                }
            }
        }

        submissionAttempted = true;
        buttons->setFocus();
        error->clear();
        error->hide();
        for (int row = 0; row < fields->rowCount(); ++row)
        {
            validateField(row);
        }
        if (!selectedInfo)
        {
            error->setText("Select a message");
            error->show();
            return;
        }
        QString compText = compid->currentText().trimmed();
        const int compParen = compText.indexOf('(');
        if (compParen > 0)
        {
            compText = compText.left(compParen).trimmed();
        }
        uint64_t resolvedCompId = 0;
        bool compValid = false;
        const auto symVal = Mav::lookupEnumValue(compText.toStdString());
        if (symVal.has_value() && *symVal <= 255)
        {
            resolvedCompId = *symVal;
            compValid = true;
        }
        else
        {
            bool ok = false;
            const uint64_t num = compText.toULongLong(&ok, 0);
            if (ok && num <= 255)
            {
                resolvedCompId = num;
                compValid = true;
            }
        }
        if (!compValid)
        {
            error->setText("Component ID must be between 0 and 255 or a valid MAV_COMPONENT enum");
            error->show();
            compid->setFocus();
            return;
        }

        const auto *metadata = mavlink_get_msg_entry(selectedInfo->msgid);
        entry.id = selectedInfo->msgid;
        entry.payload.assign(metadata->max_msg_len, 0);
        entry.sysid = uint8_t(sysid->value());
        entry.compid = uint8_t(resolvedCompId);
        entry.version1 = version->currentIndex() == 1;
        entry.hz = rate->value();
        for (unsigned i = 0; i < selectedInfo->num_fields; ++i)
        {
            std::string reason;
            if (!Mav::setField(entry.payload, selectedInfo->fields[i],
                              fields->item(int(i), 2)->text().toUtf8().toStdString(), reason))
            {
                const QString val = fields->item(int(i), 2)->text().trimmed();
                if (val.isEmpty())
                {
                    const auto &enumEntries = Mav::fieldEnumEntries(selectedInfo->msgid, selectedInfo->fields[i].name);
                    reason = !enumEntries.empty() ?
                        "Value required: please select or enter an enum name" :
                        "Value required: please enter a value";
                }
                error->setText(QString("%1: %2").arg(selectedInfo->fields[i].name, QString::fromStdString(reason)));
                error->show();
                parameterSearch->clear();
                filterParameters();
                validateField(int(i));
                fields->setCurrentCell(int(i), 2);
                fields->scrollToItem(fields->item(int(i), 2));
                fields->setFocus();
                return;
            }
        }
        if (!Mav::validTx(entry))
        {
            error->setText("MAVLink 1 supports message IDs 0–255 only");
            error->show();
            version->setFocus();
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
        if (!edit && connected)
        {
            QMetaObject::invokeMethod(worker, "setActive", Qt::QueuedConnection,
                                      Q_ARG(quint64, entry.key), Q_ARG(bool, true));
        }
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
        // Cards use the interface font rather than the numeric cell font.
        txTable->item(row, 1)->setFont(txTable->font());
        cell(txTable, row, 2, QString("%1 / %2").arg(entry.sysid).arg(entry.compid));
        cell(txTable, row, 3, QString::number(entry.hz, 'f', 0));
        cell(txTable, row, 4, QString::number(view.measuredHz, 'f', 0));
        cell(txTable, row, 5, QString::number(qulonglong(entry.count)));
        cell(txTable, row, 6, entry.error.empty() ? (entry.active ? "Running" : "Stopped") : QString::fromStdString(entry.error));
        const QString description = QString("%1 · ID %2\nSystem / Component %3\nTarget %4 Hz · Actual %5 Hz\n%6 frames handed to Qt output\n%7")
                                    .arg(messageName(entry.id), QString::number(entry.id), txTable->item(row, 2)->text(),
                                         txTable->item(row, 3)->text(), txTable->item(row, 4)->text(),
                                         txTable->item(row, 5)->text(), txTable->item(row, 6)->text());
        txTable->item(row, 1)->setData(Qt::AccessibleTextRole, description);
        if (entry.key == selected && txTable->currentRow() != row)
        {
            const int scroll = txTable->verticalScrollBar()->value();
            txTable->setCurrentCell(row, 1);
            txTable->verticalScrollBar()->setValue(scroll);
        }
    }
    updateTxHeights();
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
        const auto fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);

        for (const auto &entry : latest->rx)
        {
            const auto &message = entry.message;
            const uint32_t sysid = message.sysid;
            const uint8_t compid = message.compid;

            QTreeWidgetItem *device = nullptr;
            for (int i = 0; i < rxTree->topLevelItemCount(); ++i)
            {
                auto *candidate = rxTree->topLevelItem(i);
                if (candidate->data(0, Qt::UserRole).toUInt() == sysid &&
                    candidate->data(0, Qt::UserRole + 1).toUInt() == compid)
                {
                    device = candidate;
                    break;
                }
            }
            if (!device)
            {
                device = new QTreeWidgetItem(rxTree);
                device->setData(0, Qt::UserRole, sysid);
                device->setData(0, Qt::UserRole + 1, compid);
                device->setExpanded(true);
                device->setFirstColumnSpanned(true);

                QFont groupFont = rxTree->font();
                groupFont.setPixelSize(13);
                groupFont.setWeight(QFont::Bold);
                device->setFont(0, groupFont);

                device->setTextAlignment(0, Qt::AlignLeft | Qt::AlignVCenter);
            }

            device->setText(0, QString("System %1 · Component %2").arg(sysid).arg(compid));

            QTreeWidgetItem *item = nullptr;
            for (int i = 0; i < device->childCount(); ++i)
            {
                if (device->child(i)->data(0, Qt::UserRole).toUInt() == message.msgid)
                {
                    item = device->child(i);
                    break;
                }
            }
            if (!item)
            {
                item = new QTreeWidgetItem(device);
                item->setData(0, Qt::UserRole, message.msgid);

                QFont messageFont = rxTree->font();
                messageFont.setPixelSize(13);
                item->setFont(0, messageFont);
                item->setFont(1, fixedFont);
                item->setFont(2, fixedFont);

                item->setTextAlignment(0, Qt::AlignLeft | Qt::AlignVCenter);
                item->setTextAlignment(1, Qt::AlignCenter);
                item->setTextAlignment(2, Qt::AlignCenter);
            }

            item->setText(0, QString("[%1] %2").arg(message.msgid).arg(messageName(message.msgid)));
            item->setText(1, QString::number(entry.hz, 'f', (entry.hz > 0.0 && entry.hz < 10.0) ? 1 : 0));
            item->setData(1, Qt::UserRole, entry.hz);
            item->setText(2, QString::number(qulonglong(entry.count)));
            item->setData(2, Qt::UserRole, qulonglong(entry.count));
            item->setData(0, Qt::UserRole + 2, entry.age);
            item->setToolTip(0, rxStatus(entry.age));
            item->setData(0, Qt::AccessibleTextRole, item->text(0) + ", " + rxStatus(entry.age));
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
    inspectorSource->clear();
    inspectorSource->hide();
    auto *item = rxTree->currentItem();
    if (!item || !item->parent())
    {
        inspector->setRowCount(0);
        return;
    }
    const uint32_t id = item->data(0, Qt::UserRole).toUInt();
    const uint32_t system = item->parent()->data(0, Qt::UserRole).toUInt();
    const uint8_t component = uint8_t(item->parent()->data(0, Qt::UserRole + 1).toUInt());
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
        inspectorTitle->setText(QString("[%1] %2").arg(id).arg(messageName(id)));
        inspectorSource->setText(QString("System %1 · Component %2").arg(system).arg(component));
        inspectorSource->show();
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
            QFont identityFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
            identityFont.setPixelSize(13);
            inspector->item(int(i), 0)->setFont(identityFont);
            identityFont.setPixelSize(12);
            inspector->item(int(i), 1)->setFont(identityFont);
            const QString valStr = QString::fromStdString(Mav::readField(message, field));
            const QString fieldKey = QString("%1.%2.%3.%4").arg(system).arg(component).arg(id).arg(field.name);
            const auto prevIt = lastFieldValues.find(fieldKey);
            const bool changed = (prevIt != lastFieldValues.end()) && (prevIt->second != valStr);
            lastFieldValues[fieldKey] = valStr;
            cell(inspector, int(i), 2, valStr);
            auto *valItem = inspector->item(int(i), 2);
            valItem->setData(Qt::UserRole + 5, changed);
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
    int total = 0;
    int matched = 0;
    for (int i = 0; i < rxTree->topLevelItemCount(); ++i)
    {
        auto *device = rxTree->topLevelItem(i);
        bool deviceVisible = false;
        for (int j = 0; j < device->childCount(); ++j)
        {
            auto *item = device->child(j);
            const QString identity = device->text(0) + " " + item->text(0);
            const bool visible = identity.contains(query, Qt::CaseInsensitive);
            item->setHidden(!visible);
            ++total;
            matched += visible;
            deviceVisible |= visible;
        }
        device->setHidden(!deviceVisible);
    }
    rxSummary->setText(query.isEmpty() ? QString("%1 %2").arg(total).arg(total == 1 ? "stream" : "streams") :
                       QString("%1 / %2 streams").arg(matched).arg(total));
}

void MainWindow::showError(const QString &message)
{
    lastError->setText("Error: " + message);
    lastError->setToolTip(message);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (auto *popup = qobject_cast<QMenu *>(watched))
    {
        if (event->type() == QEvent::Resize || event->type() == QEvent::Show)
        {
            popup->setAttribute(Qt::WA_TranslucentBackground, true);
            QPainterPath outline;
            outline.addRoundedRect(QRectF(popup->rect()), 6.0, 6.0);
            popup->setMask(QRegion(outline.toFillPolygon().toPolygon()));
        }
    }
    if (watched == qApp && event->type() == QEvent::ApplicationPaletteChange &&
        themeBox && themeBox->currentIndex() == 0)
    {
        applyTheme();
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
                    const QRect card = rect.adjusted(1, 5, -2, -5);
                    const QRect badge(card.right() - 88, card.top() + 8, 80, 24);
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
                const QRect card = rect.adjusted(1, 5, -2, -5);
                const QRect badge(card.right() - 88, card.top() + 8, 80, 24);
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
    qApp->setProperty("darkTheme", dark);
    setProperty("darkTheme", dark);
    if (settingsDialog)
    {
        settingsDialog->setProperty("darkTheme", dark);
    }
    if (txTable)
    {
        txTable->setProperty("darkTheme", dark);
    }
    if (inspector)
    {
        inspector->setProperty("darkTheme", dark);
    }
    for (auto *box : {restoreTxBox, autoReconnectBox, deassertControlLinesBox})
    {
        if (box)
        {
            box->setProperty("darkTheme", dark);
            box->update();
        }
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
        "QDialog { border:1px solid %4; }"
        "QFrame#dialogTitleBar { background:%1; border:0; }"
        "QFrame#dialogTitleDivider { background:%4; border:0; }"
        "QLabel#dialogWindowTitle { background:transparent; color:%3; font-size:18px; font-weight:600; }"
        "QPushButton#closeDialog { background:transparent; border:1px solid transparent; padding:0; font-size:20px; font-weight:400; }"
        "QPushButton#closeDialog:hover { background:%6; }"
        "QPushButton#closeDialog:pressed { background:%4; }"
        "QPushButton#closeDialog[keyboardFocus=true] { border-color:#818cf8; }"
        "QMenuBar { background:%1; color:%3; border-bottom:1px solid %4; padding:2px 8px; font-size:13px; outline:none; }"
        "QMenuBar::item { background:transparent; padding:4px 8px; border-radius:4px; outline:none; }"
        "QMenuBar::item:selected { background:%6; color:%3; }"
        "QMenuBar::item:pressed { background:%6; }"
        "QMenu { background:%2; color:%3; border:1px solid %4; border-radius:6px; padding:4px; font-size:13px; outline:none; }"
        "QMenu::item { padding:6px 24px 6px 12px; border-radius:4px; }"
        "QMenu::item:selected { background:#4f46e5; color:white; }"
        "QMenu::separator { height:1px; background:%4; margin:4px 6px; }"
        "QWidget { font-size:14px; } QLabel { color:%3; }"
        "QLabel#appMark { font-size:24px; font-weight:700; color:white; background:#4f46e5; border-radius:12px; }"
        "QLabel#heading { font-size:22px; font-weight:700; }"
        "QLabel#panelTitle { font-size:18px; font-weight:700; }"
        "QLabel#selectedMessageTitle { font-size:16px; font-weight:600; color:%3; }"
        "QLabel#catalogResultCount,QLabel#payloadHint,QLabel#catalogEmpty { color:%5; font-size:12px; }"
        "QFrame#messageFooterDivider { background:%4; border:0; }"
        "QSplitter#messageSplitter::handle { background:transparent; border-left:1px solid %4; margin:0 7px; }"
        "QLabel#sectionTitle { font-size:11px; font-weight:700; color:%5; }"
        "QLabel#secondaryText { color:%5; font-size:12px; }"
        "QLabel#summaryBadge { color:%5; background:%6; border-radius:8px; padding:5px 10px; font-size:12px; }"
        "QFrame#connectionBar { background:transparent; border:0; }"
        "QFrame#connectionCard { background:transparent; border:0; }"
        "QFrame#connectionCardDivider { background:%4; border:0; }"
        "QLabel#serialSummary { color:%5; background:%6; border:1px solid %4; border-radius:7px; padding:0 10px; font-size:12px; font-weight:500; }"
        "QLabel#serialFieldLabel { color:%5; font-size:12px; }"
        "QFrame#serialDivider,QFrame#txDivider,QFrame#txBatchDivider { background:%4; border:0; }"
        "QMenu#serialMenu { background:%6; border:1px solid %4; border-radius:7px; padding:0; }"
        "QFrame#serialAdvanced { background:transparent; border:0; }"
        "QFrame#dashboardPanel { background:%2; border:1px solid %4; border-radius:14px; }"
        "QFrame#settingsCard { background:%2; border:1px solid %4; border-radius:8px; }"
        "QLabel#settingsGroupTitle { font-size:13px; font-weight:700; color:%3; }"
        "QCheckBox { background:transparent; color:%3; font-size:13px; outline:none; }"
        "QDialogButtonBox QPushButton { qproperty-icon: none; }"
        "QPushButton { background:%2; color:%3; border:1px solid %4; border-radius:7px; padding:7px 10px; font-weight:500; outline:none; }"
        "QPushButton#runAll,QPushButton#stopAll,QPushButton#deleteAll { padding:0; border-radius:5px; }"
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
        "QLineEdit:focus,QSpinBox:focus,QDoubleSpinBox:focus { border:1px solid #4f46e5; outline:none; }"
        "QPushButton[keyboardFocus=true],QPushButton[role=primary][keyboardFocus=true],QPushButton[role=success][keyboardFocus=true] { border:2px solid #818cf8; padding:6px 9px; outline:none; }"
        "QLineEdit,QSpinBox,QDoubleSpinBox { background:%2; color:%3; border:1px solid %8; border-radius:7px; padding:7px; }"
        "QComboBox { background:%2; color:%3; border:1px solid %8; border-radius:7px; padding:4px 28px 4px 10px; min-height:22px; outline:none; }"
        "QComboBox:editable { padding:4px 28px 4px 7px; }"
        "QComboBox:hover { background:%6; border-color:#818cf8; }"
        "QComboBox:focus,QComboBox:on { border:1px solid #4f46e5; outline:none; }"
        "QComboBox:disabled { color:%5; background:%6; border-color:%4; }"
        "QComboBox::drop-down { subcontrol-origin:padding; subcontrol-position:top right; width:26px; border:0; }"
        "QComboBox::down-arrow { image:none; }"
        "QComboBox QLineEdit,QComboBox QLineEdit:focus,QComboBox QLineEdit:hover { padding:0; margin:0; border:0; outline:none; background:transparent; }"
        "QComboBox QFrame { background:transparent; border:0; }"
        "QPushButton#refreshPorts { padding:0; }"
        "QPushButton#refreshPorts[keyboardFocus=true] { padding:0; }"
        "QSpinBox::up-button,QDoubleSpinBox::up-button { subcontrol-origin:border; subcontrol-position:top right; width:20px; border-left:1px solid %4; border-bottom:1px solid %4; }"
        "QSpinBox::down-button,QDoubleSpinBox::down-button { subcontrol-origin:border; subcontrol-position:bottom right; width:20px; border-left:1px solid %4; }"
        "QSpinBox::up-arrow,QSpinBox::down-arrow,QDoubleSpinBox::up-arrow,QDoubleSpinBox::down-arrow { image:none; }"
        "QComboBox QAbstractItemView { background:%2; color:%3; border:1px solid %4; border-radius:8px; padding:4px; outline:none; selection-background-color:transparent; }"
        "QComboBox QAbstractItemView::item { min-height:28px; border-radius:5px; margin:1px 2px; }"
        "QComboBox QAbstractItemView::item:hover,QComboBox QAbstractItemView::item:selected { background:transparent; }"
        "QListWidget { background:%2; color:%3; selection-background-color:#4f46e5; selection-color:white; padding:3px; }"
        "QListWidget::item { padding:5px 6px; border-radius:3px; }"
        "QListWidget::item:selected { background:#4f46e5; color:white; }"
        "QListWidget::item:hover:!selected { background:%6; }"
        "QAbstractItemView { background:%2; color:%3; alternate-background-color:%6; border:0; border-radius:0; gridline-color:%4; }"
        "QTableView::item { padding:0 6px; border:0; }"
        "QTableWidget#txTable::item,QTableWidget#txTable::item:selected,QTableWidget#txTable::item:hover { padding:0; background:transparent; border:0; }"
        "QTreeView::item { padding:5px 4px; border:0; }"
        "QHeaderView::section { background:%6; color:%5; padding:9px 8px; border:0; border-bottom:1px solid %4; font-weight:600; font-size:12px; }"
        "QTableWidget#payloadFields,QTableWidget#inspector { border:0; border-radius:0; }"
        "QTableWidget#payloadFields::item,QTableWidget#inspector::item { padding:0 10px; border-bottom:1px solid %4; }"
        "QTableWidget#payloadFields QHeaderView::section,QTableWidget#inspector QHeaderView::section { background:%6; color:%3; padding:8px 10px; border-bottom:1px solid %4; font-weight:600; font-size:12px; }"
        "QLineEdit#payloadValueEditor { padding:0 8px; border-radius:5px; font-size:13px; selection-background-color:#4f46e5; selection-color:white; }"
        "QComboBox QLineEdit#payloadValueEditor { padding:0; margin:0; border:0; outline:none; background:transparent; }"
        "QSplitter::handle { background:transparent; }"
        "QStatusBar { color:%5; background:transparent; border:0; }"
        "QStatusBar QLabel { color:%5; font-size:12px; } QStatusBar::item { border:0; }"
        "QScrollBar:vertical { background:%6; width:6px; } QScrollBar:horizontal { background:%6; height:6px; }"
        "QScrollBar::handle { background:%8; border-radius:3px; }"
        "QScrollBar::handle:vertical { min-height:24px; } QScrollBar::handle:horizontal { min-width:24px; }"
        "QScrollBar::handle:hover { background:#818cf8; } QScrollBar::add-line,QScrollBar::sub-line { width:0px; height:0px; }"
        "QLabel#validationError { color:%7; }"
    ).arg(background, surface, foreground, border, muted, alternate,
          dark ? "#fca5a5" : "#b91c1c", controlBorder);
    const QString connectionStyle = QString(
        "QPushButton#connect[connected=true] { background:%1; color:%2; border:1px solid %3; font-weight:400; }"
        "QPushButton#connect[connected=true]:hover { background:%4; border-color:#ef4444; color:%5; }"
        "QPushButton#connect[connected=true]:pressed { background:%6; }"
        "QPushButton#connect[reconnecting=true] { background:%7; color:%8; border:1px solid %9; font-weight:400; }"
        "QPushButton#connect[reconnecting=true]:hover { background:%10; border-color:#f59e0b; color:%11; }"
        "QPushButton#connect[reconnecting=true]:pressed { background:%12; }"
    ).arg(dark ? "#3b1e26" : "#fef2f2",
          dark ? "#fca5a5" : "#b91c1c",
          dark ? "#7f2d3f" : "#fca5a5",
          dark ? "#4e2531" : "#fee2e2",
          dark ? "#ffffff" : "#991b1b",
          dark ? "#2d151c" : "#fecaca")
     .arg(dark ? "#3d2a10" : "#fffbeb",
          dark ? "#fcd34d" : "#b45309",
          dark ? "#78350f" : "#fde68a",
          dark ? "#4e3614" : "#fef3c7",
          dark ? "#ffffff" : "#92400e",
          dark ? "#291b08" : "#fde68a");
    const QString selectionStyle = QString(
        "QTableView::item:selected,QTreeView::item:selected { background:%1; color:%2; }"
        "QTreeView::branch:selected { background:%1; }"
        "QTableView::item:hover:!selected,QTreeView::item:hover:!selected { background:%3; }"
    ).arg(dark ? "#2c3550" : "#eeedfa", foreground, alternate);
    const QString dataStyle = QString(
        "QFrame#dataPane { background:%1; border:1px solid %2; border-radius:5px; }"
        "QLabel#dataPaneTitle { background:%3; color:%4; border-bottom:1px solid %2; padding:8px 10px; font-size:12px; font-weight:600; }"
        "QFrame#inspectorHeader { background:%3; border-bottom:1px solid %2; }"
        "QLabel#inspectorMessageTitle { background:transparent; color:%4; font-size:12px; font-weight:600; }"
        "QLabel#inspectorSource { color:%4; font-size:12px; }"
        "QLabel#displayPaused { color:%7; background:%1; padding:2px 6px; border-radius:4px; font-size:11px; }"
        "QTreeWidget#rxTree { background:%1; alternate-background-color:%5; outline:none; }"
        "QTableWidget#inspector { background:%1; border:0; }"
        "QTableWidget#txTable { background:transparent; }"
        "QTableView#txTable::item:selected,QTableView#txTable::item:hover { background:transparent; }"
        "QTreeWidget#rxTree::item { border-bottom:1px solid %5; height:34px; padding:0 8px; outline:none; }"
        "QTreeWidget#rxTree::item:focus { outline:none; border:none; }"
        "QTreeWidget#rxTree::item:selected:focus { outline:none; border:none; }"
        "QHeaderView::section { background:%6; color:%4; border-bottom:1px solid %2; border-right:1px solid %5; padding:4px 10px; }"
        "QSplitter#rxSplit::handle { background:transparent; border:0; }"
    ).arg(dark ? "#162134" : "#ffffff", dark ? "#35445d" : "#d1dbea",
          dark ? "#202d42" : "#eef2f9", dark ? "#d6dfee" : "#40536e",
          dark ? "#1d2b41" : "#f4f7fb", dark ? "#1d293c" : "#f7f9fc", dark ? "#f3d698" : "#805c17");
    setStyleSheet(baseStyle + connectionStyle + selectionStyle + dataStyle);
    for (const auto &name : {"addMessage", "startTx", "sendOnce", "stopTx", "refreshPorts", "serialSettings", "editMessage", "removeMessage", "runAll", "stopAll", "deleteAll"})
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
