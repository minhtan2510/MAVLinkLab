/**
 * @file window_private.h
 * @brief Internal Qt widget declarations, delegates, custom controls, and helper utilities.
 */

#pragma once

#include "window.h"
#include "mavlink_enums.h"
#include <QApplication>
#include <QDialog>
#include <QFrame>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QMenu>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QTableWidget>
#include <QTreeWidget>
#include <QHeaderView>
#include <QElapsedTimer>
#include <QTimer>
#include <QIconEngine>
#include <QSerialPortInfo>
#include <QPainter>
#include <QPainterPath>
#include <QFontDatabase>
#include <QToolTip>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QStyleOptionSpinBox>
#include <QStyleOptionViewItem>
#include <QTextLayout>
#include <QCompleter>
#include <QListWidget>
#include <QFileDialog>
#include <QSaveFile>
#include <QCloseEvent>
#include <QSettings>
#include <QClipboard>
#include <QWidgetAction>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QBoxLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QToolButton>
#include <QRegion>
#include <QStyledItemDelegate>
#include <QScrollBar>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFocusEvent>
#include <QWindow>
#include <algorithm>
#include <array>
#include <limits>
#include <map>
/** @brief Keep an accepted import visible until the worker reports its outcome. */
class ProfileImportDialog : public QDialog
{
public:
    using QDialog::QDialog;
    bool applying = false;

    /** @brief Allow cancellation only before an import has been submitted. */
    void reject() override
    {
        if (!applying)
        {
            QDialog::reject();
        }
    }

protected:
    /** @brief Prevent window-manager close while a submitted import is pending. */
    void closeEvent(QCloseEvent *event) override
    {
        if (applying)
        {
            event->ignore();
            return;
        }
        QDialog::closeEvent(event);
    }
};

/** @brief Paint a small theme-aware document mark for the profile picker. */
class ProfileDocumentIcon : public QWidget
{
public:
    /** @brief Reserve a compact decorative area without adding a keyboard stop. */
    explicit ProfileDocumentIcon(QWidget *parent = nullptr) : QWidget(parent)
    {
        setFixedSize(40, 40);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFocusPolicy(Qt::NoFocus);
    }

protected:
    /** @brief Draw the document with antialiased vector paths in the current theme. */
    void paintEvent(QPaintEvent *) override
    {
        const bool dark = qApp->property("darkTheme").toBool();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(dark ? "#30355d" : "#eeedff"));
        painter.drawRoundedRect(QRectF(0, 0, 40, 40), 10, 10);
        painter.setPen(QPen(QColor(dark ? "#b4adff" : "#6254d8"), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        QPainterPath page;
        page.moveTo(13, 9);
        page.lineTo(22, 9);
        page.lineTo(28, 15);
        page.lineTo(28, 30);
        page.lineTo(13, 30);
        page.closeSubpath();
        painter.drawPath(page);
        painter.drawLine(QPointF(22, 9), QPointF(22, 15));
        painter.drawLine(QPointF(22, 15), QPointF(28, 15));
        painter.drawLine(QPointF(17, 21), QPointF(24, 21));
        painter.drawLine(QPointF(17, 25), QPointF(22, 25));
    }
};

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
inline QVBoxLayout *addDialogTitleBar(QDialog *dialog, QVBoxLayout *layout)
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

/** @brief Paint a compact theme-aware alert icon. */
class ConnectionAlertIcon : public QWidget
{
public:
    explicit ConnectionAlertIcon(QWidget *parent = nullptr) : QWidget(parent)
    {
        setFixedSize(32, 32);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFocusPolicy(Qt::NoFocus);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const bool dark = qApp->property("darkTheme").toBool();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(dark ? "#3b1e26" : "#fef2f2"));
        painter.drawRoundedRect(QRectF(0, 0, 32, 32), 8, 8);

        // Warning triangle
        painter.setPen(QPen(QColor(dark ? "#f87171" : "#dc2626"), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        QPainterPath triangle;
        triangle.moveTo(16, 7);
        triangle.lineTo(25, 24);
        triangle.lineTo(7, 24);
        triangle.closeSubpath();
        painter.drawPath(triangle);

        // Exclamation mark
        painter.drawLine(QPointF(16, 13), QPointF(16, 18));
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(dark ? "#f87171" : "#dc2626"));
        painter.drawEllipse(QRectF(15, 20.5, 2, 2));
    }
};

/** @brief Compact modal dialog presenting connection failure directly and cleanly. */
class ConnectionErrorDialog : public QDialog
{
public:
    explicit ConnectionErrorDialog(const QString &titleText, const QString &messageText, QWidget *parent = nullptr)
        : QDialog(parent)
    {
        setObjectName("connectionErrorDialog");
        setWindowTitle(titleText);
        setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
        setAttribute(Qt::WA_TranslucentBackground);
        setFixedWidth(380);

        auto *rootLayout = new QVBoxLayout(this);
        rootLayout->setContentsMargins(4, 4, 4, 4);

        auto *card = new QFrame(this);
        card->setObjectName("connectionErrorCard");
        auto *layout = new QVBoxLayout(card);
        layout->setContentsMargins(18, 18, 18, 16);
        layout->setSpacing(14);

        // Content row: Icon + Text
        auto *contentRow = new QHBoxLayout;
        contentRow->setSpacing(12);
        contentRow->setAlignment(Qt::AlignTop);

        contentRow->addWidget(new ConnectionAlertIcon(card), 0, Qt::AlignTop);

        auto *textLayout = new QVBoxLayout;
        textLayout->setSpacing(4);

        auto *headline = new QLabel(titleText, card);
        headline->setObjectName("dialogHeadline");
        QFont f = font();
        f.setPixelSize(14);
        f.setWeight(QFont::DemiBold);
        headline->setFont(f);
        textLayout->addWidget(headline);

        auto *message = new QLabel(messageText, card);
        message->setObjectName("connectionErrorMessage");
        message->setWordWrap(true);
        QFont mf = font();
        mf.setPixelSize(12);
        message->setFont(mf);
        textLayout->addWidget(message);

        contentRow->addLayout(textLayout, 1);
        layout->addLayout(contentRow);

        // Button row: right-aligned compact OK button
        auto *buttonRow = new QHBoxLayout;
        buttonRow->addStretch(1);
        auto *okButton = new QPushButton("OK", card);
        okButton->setObjectName("dismissConnectionError");
        okButton->setFixedSize(72, 28);
        okButton->setDefault(true);
        connect(okButton, &QPushButton::clicked, this, &QDialog::accept);
        buttonRow->addWidget(okButton);
        layout->addLayout(buttonRow);

        rootLayout->addWidget(card);
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
        {
            offset = event->globalPosition().toPoint() - frameGeometry().topLeft();
            dragging = !windowHandle() || !windowHandle()->startSystemMove();
            event->accept();
            return;
        }
        QDialog::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (dragging && (event->buttons() & Qt::LeftButton))
        {
            move(event->globalPosition().toPoint() - offset);
            event->accept();
            return;
        }
        QDialog::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        dragging = false;
        QDialog::mouseReleaseEvent(event);
    }

private:
    QPoint offset;
    bool dragging = false;
};

/**
 * @brief Check whether a serial port represents an actively connected hardware device.
 * @param info Serial port metadata.
 * @return True if the device is a plugged USB converter, active onboard UART, or genuine hardware port.
 */
inline bool isPluggedPort(const QSerialPortInfo &info)
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
 * @brief Return a cached fixed-pitch system font to avoid repetitive font database queries.
 * @return Cached QFont configured for fixed-width numeric and telemetry display.
 */
inline const QFont &cachedFixedFont()
{
    static const QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    return font;
}

/**
 * @brief Provide human-readable enum descriptions for common MAVLink fields.
 * @param msgid MAVLink message identity.
 * @param field Field name string.
 * @param val Decimal string value.
 * @return Concise description or empty string.
 */
inline QString mavlinkEnumHint(uint32_t msgid, const QString &field, const QString &val)
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

/**
 * @brief Configure a popup menu with frameless window flags and translucent background for clean rounded corners.
 * @param menu The popup menu widget to configure.
 */
inline void configureTranslucentMenu(QMenu *menu)
{
    if (!menu)
    {
        return;
    }
    menu->setWindowFlags(menu->windowFlags() | Qt::FramelessWindowHint);
    menu->setAttribute(Qt::WA_TranslucentBackground, true);
}

/**
 * @brief Custom menu supporting decoupled, right-aligned keycap pill badges for keyboard shortcuts.
 */
class WorkbenchMenu : public QMenu
{
public:
    using QMenu::QMenu;

protected:
    /**
     * @brief Intercept style option to decouple accelerator text from menu item labels.
     * @param option The style option being populated.
     * @param action The corresponding action being queried.
     */
    void initStyleOption(QStyleOptionMenuItem *option, const QAction *action) const override
    {
        QMenu::initStyleOption(option, action);
        if (option)
        {
            const int tabIdx = option->text.indexOf(QLatin1Char('\t'));
            if (tabIdx >= 0)
            {
                option->text = option->text.left(tabIdx);
            }
        }
    }

    /**
     * @brief Render right-aligned keycap pill badges for actions with assigned keyboard shortcuts.
     * @param event The paint event parameters.
     */
    void paintEvent(QPaintEvent *event) override
    {
        QMenu::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const QAction *active = activeAction();
        const bool isDark = palette().color(QPalette::Window).lightness() < 128;

        for (auto *action : actions())
        {
            if (!action || action->isSeparator() || action->shortcut().isEmpty())
            {
                continue;
            }

            const QRect rect = actionGeometry(action);
            if (rect.isEmpty())
            {
                continue;
            }

            const QString shortcutText = action->shortcut().toString(QKeySequence::NativeText);
            if (shortcutText.isEmpty())
            {
                continue;
            }

            const bool isHovered = (action == active);

            QFont font = painter.font();
            font.setPointSize(9);
            font.setWeight(QFont::Normal);
            painter.setFont(font);

            const QFontMetrics fm(font);
            const int textW = fm.horizontalAdvance(shortcutText);
            const int textH = fm.height();

            const int badgeW = textW + 10;
            const int badgeH = textH + 2;
            const int rightMargin = 12;

            const QRect badgeRect(rect.right() - badgeW - rightMargin + 1,
                                  rect.top() + (rect.height() - badgeH) / 2,
                                  badgeW, badgeH);

            QPainterPath path;
            path.addRoundedRect(badgeRect, 3.5, 3.5);

            if (isHovered)
            {
                painter.fillPath(path, QColor(255, 255, 255, 28));
                painter.setPen(QPen(QColor(255, 255, 255, 45), 1.0));
                painter.drawPath(path);

                painter.setPen(QColor(255, 255, 255, 190));
                painter.drawText(badgeRect, Qt::AlignCenter, shortcutText);
            }
            else
            {
                if (isDark)
                {
                    painter.fillPath(path, QColor(255, 255, 255, 10));
                    painter.setPen(QPen(QColor(255, 255, 255, 22), 1.0));
                    painter.drawPath(path);

                    painter.setPen(QColor(148, 163, 184, 165));
                    painter.drawText(badgeRect, Qt::AlignCenter, shortcutText);
                }
                else
                {
                    painter.fillPath(path, QColor(0, 0, 0, 8));
                    painter.setPen(QPen(QColor(0, 0, 0, 18), 1.0));
                    painter.drawPath(path);

                    painter.setPen(QColor(120, 135, 155));
                    painter.drawText(badgeRect, Qt::AlignCenter, shortcutText);
                }
            }
        }
    }
};

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
                container->setWindowFlags(container->windowFlags() | Qt::FramelessWindowHint);
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
            container->setWindowFlags(container->windowFlags() | Qt::FramelessWindowHint);
            container->setAttribute(Qt::WA_TranslucentBackground, true);
            container->clearMask();
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

inline WorkbenchComboDelegate::WorkbenchComboDelegate(WorkbenchComboBox *combo)
    : QStyledItemDelegate(reinterpret_cast<QObject *>(combo)), m_combo(combo)
{
}

inline void WorkbenchComboDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
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

using WorkbenchDoubleSpinBox = WorkbenchSpinBox<QDoubleSpinBox>;

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

/** @brief Line edit that maintains a theme-aware dimmed placeholder text color. */
class WorkbenchLineEdit : public QLineEdit
{
public:
    /**
     * @brief Construct an input line edit with subdued placeholder support.
     * @param parent Optional parent widget.
     */
    explicit WorkbenchLineEdit(QWidget *parent = nullptr) : QLineEdit(parent)
    {
        updatePlaceholderColor();
    }

    /**
     * @brief Construct an input line edit with initial text.
     * @param contents Initial text string.
     * @param parent Optional parent widget.
     */
    explicit WorkbenchLineEdit(const QString &contents, QWidget *parent = nullptr) : QLineEdit(contents, parent)
    {
        updatePlaceholderColor();
    }

protected:
    /**
     * @brief Ensure placeholder color persists when the widget is shown.
     * @param event Show event from Qt.
     */
    void showEvent(QShowEvent *event) override
    {
        QLineEdit::showEvent(event);
        updatePlaceholderColor();
    }

    /**
     * @brief Keep placeholder hints subdued when the palette or style changes.
     * @param event Change event from Qt.
     */
    void changeEvent(QEvent *event) override
    {
        QLineEdit::changeEvent(event);
        if (event && (event->type() == QEvent::PaletteChange ||
                      event->type() == QEvent::ApplicationPaletteChange ||
                      event->type() == QEvent::StyleChange))
        {
            updatePlaceholderColor();
        }
    }

    /** @brief Apply the dimmed hint color to the placeholder text role. */
    void updatePlaceholderColor()
    {
        const bool dark = qApp->property("darkTheme").toBool();
        const QColor hint(dark ? "#606d88" : "#8893a7");
        QPalette colors = palette();
        if (colors.color(QPalette::PlaceholderText) != hint)
        {
            colors.setResolveMask(0);
            colors.setColor(QPalette::PlaceholderText, hint);
            setPalette(colors);
        }
    }
};

/**
 * @brief Dynamic visual status indicator for configured and active TX messages.
 * Renders dual rounded pill badges: total messages count and live running state.
 */
class TxSummaryWidget : public QWidget
{
public:
    /**
     * @brief Construct a summary widget with dual-pills appearance.
     * @param parent Optional parent widget.
     */
    explicit TxSummaryWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        setFixedHeight(26);
    }

    /**
     * @brief Update message counts and estimated bandwidth, requesting visual repaint.
     * @param total Total number of configured TX messages.
     * @param running Number of currently transmitting messages.
     * @param estimatedBps Estimated total transmission wire load in bytes/second.
     * @param utilization Estimated channel utilization percentage.
     */
    void setStats(int total, int running, double estimatedBps = 0.0, double utilization = 0.0)
    {
        if (totalCount != total || runningCount != running ||
            std::abs(estBps - estimatedBps) > 0.1 || std::abs(estUtil - utilization) > 0.1)
        {
            totalCount = total;
            runningCount = running;
            estBps = estimatedBps;
            estUtil = utilization;
            updateGeometry();
            if (parentWidget() && parentWidget()->layout())
            {
                parentWidget()->layout()->activate();
            }
            update();
        }
    }

    /**
     * @brief Get total number of configured messages.
     * @return Message count.
     */
    int total() const
    {
        return totalCount;
    }

    /**
     * @brief Get number of running messages.
     * @return Running message count.
     */
    int running() const
    {
        return runningCount;
    }

    /**
     * @brief Formatted accessible summary text.
     * @return Accessible status description string.
     */
    QString text() const
    {
        if (runningCount > 0 && estBps > 0.0)
        {
            return QString("%1 %2  ·  %3 running  ·  Est. %4 (%5%)")
                .arg(totalCount)
                .arg(totalCount == 1 ? "message" : "messages")
                .arg(runningCount)
                .arg(QString::fromStdString(Mav::formatByteRate(estBps)))
                .arg(estUtil, 0, 'f', 1);
        }
        return QString("%1 %2  ·  %3 running")
            .arg(totalCount)
            .arg(totalCount == 1 ? "message" : "messages")
            .arg(runningCount);
    }

    /**
     * @brief Calculate recommended dimensions matching badge text and geometry.
     * @return Layout size hint.
     */
    QSize sizeHint() const override
    {
        QFont baseFont = font();
        baseFont.setPixelSize(11);
        baseFont.setWeight(QFont::Medium);
        QFont boldFont = baseFont;
        boldFont.setWeight(QFont::DemiBold);

        const QString totalText = QString("%1 %2")
            .arg(totalCount)
            .arg(totalCount == 1 ? "message" : "messages");
        const QString runningText = QString("%1 running").arg(runningCount);

        const int totalTextWidth = QFontMetrics(baseFont).horizontalAdvance(totalText);
        const int runningTextWidth = QFontMetrics(runningCount > 0 ? boldFont : baseFont).horizontalAdvance(runningText);

        const int pill1Width = totalTextWidth + 20;
        const int pill2Width = runningTextWidth + 20;

        const bool showLoad = (runningCount > 0 && estBps > 0.0);
        int pill3Width = 0;
        if (showLoad)
        {
            const QString loadText = (estUtil > 0.0) ?
                QString("~%1 (%2%)").arg(QString::fromStdString(Mav::formatByteRate(estBps))).arg(estUtil, 0, 'f', 1) :
                QString("~%1").arg(QString::fromStdString(Mav::formatByteRate(estBps)));
            pill3Width = QFontMetrics(baseFont).horizontalAdvance(loadText) + 20;
        }

        const int totalWidth = pill1Width + 6 + pill2Width + (showLoad ? (6 + pill3Width) : 0);
        return QSize(totalWidth, 26);
    }

protected:
    /**
     * @brief Paint dual or triple status pills using crisp vector antialiasing.
     * @param event Paint event from Qt.
     */
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const bool dark = property("darkTheme").isValid() ?
                          property("darkTheme").toBool() :
                          (window() && window()->property("darkTheme").isValid() ?
                           window()->property("darkTheme").toBool() :
                           (palette().color(QPalette::Window).lightness() < 128));

        QFont baseFont = font();
        baseFont.setPixelSize(11);
        baseFont.setWeight(QFont::Medium);

        QFont boldFont = baseFont;
        boldFont.setWeight(QFont::DemiBold);

        const QString totalText = QString("%1 %2")
            .arg(totalCount)
            .arg(totalCount == 1 ? "message" : "messages");
        const QString runningText = QString("%1 running").arg(runningCount);

        const int totalTextWidth = QFontMetrics(baseFont).horizontalAdvance(totalText);
        const int runningTextWidth = QFontMetrics(runningCount > 0 ? boldFont : baseFont).horizontalAdvance(runningText);

        const int pillHeight = 24;
        const int y = (height() - pillHeight) / 2;

        // --- Pill 1: Total Configured Messages ---
        const int pill1Width = totalTextWidth + 20;
        const QRect pill1Rect(0, y, pill1Width, pillHeight);

        painter.setPen(QPen(QColor(dark ? "#334155" : "#cbd5e1"), 1.0));
        painter.setBrush(QColor(dark ? "#1e293b" : "#f1f5f9"));
        painter.drawRoundedRect(pill1Rect, 6.0, 6.0);

        painter.setFont(baseFont);
        painter.setPen(QColor(dark ? "#94a3b8" : "#475569"));
        painter.drawText(pill1Rect, Qt::AlignCenter, totalText);

        // --- Pill 2: Running State Indicator ---
        const int pill2Left = pill1Width + 6;
        const int pill2Width = runningTextWidth + 20;
        const QRect pill2Rect(pill2Left, y, pill2Width, pillHeight);

        if (runningCount > 0)
        {
            painter.setPen(QPen(QColor(dark ? "#0d7a5f" : "#86efac"), 1.0));
            painter.setBrush(QColor(dark ? "#0a2e23" : "#dcfce7"));
            painter.drawRoundedRect(pill2Rect, 6.0, 6.0);

            painter.setFont(boldFont);
            painter.setPen(QColor(dark ? "#34d399" : "#15803d"));
            painter.drawText(pill2Rect, Qt::AlignCenter, runningText);
        }
        else
        {
            painter.setPen(QPen(QColor(dark ? "#334155" : "#e2e8f0"), 1.0));
            painter.setBrush(QColor(dark ? "#1e293b" : "#f8fafc"));
            painter.drawRoundedRect(pill2Rect, 6.0, 6.0);

            painter.setFont(baseFont);
            painter.setPen(QColor(dark ? "#64748b" : "#94a3b8"));
            painter.drawText(pill2Rect, Qt::AlignCenter, runningText);
        }

        // --- Pill 3: Throughput Estimate (When transmitting) ---
        if (runningCount > 0 && estBps > 0.0)
        {
            const QString loadText = (estUtil > 0.0) ?
                QString("~%1 (%2%)").arg(QString::fromStdString(Mav::formatByteRate(estBps))).arg(estUtil, 0, 'f', 1) :
                QString("~%1").arg(QString::fromStdString(Mav::formatByteRate(estBps)));
            const int pill3Left = pill2Left + pill2Width + 6;
            const int pill3Width = QFontMetrics(baseFont).horizontalAdvance(loadText) + 20;
            const QRect pill3Rect(pill3Left, y, pill3Width, pillHeight);

            QColor p3Border, p3Bg, p3Text;
            if (estUtil > 90.0)
            {
                p3Border = QColor(dark ? "#7f1d1d" : "#fecaca");
                p3Bg = QColor(dark ? "#2e1215" : "#fef2f2");
                p3Text = QColor(dark ? "#f87171" : "#dc2626");
            }
            else if (estUtil > 70.0)
            {
                p3Border = QColor(dark ? "#78350f" : "#fde68a");
                p3Bg = QColor(dark ? "#2b1a07" : "#fffbeb");
                p3Text = QColor(dark ? "#fbbf24" : "#d97706");
            }
            else
            {
                p3Border = QColor(dark ? "#1e3a5f" : "#bfdbfe");
                p3Bg = QColor(dark ? "#0f233a" : "#eff6ff");
                p3Text = QColor(dark ? "#60a5fa" : "#2563eb");
            }

            painter.setPen(QPen(p3Border, 1.0));
            painter.setBrush(p3Bg);
            painter.drawRoundedRect(pill3Rect, 6.0, 6.0);

            painter.setFont(boldFont);
            painter.setPen(p3Text);
            painter.drawText(pill3Rect, Qt::AlignCenter, loadText);
        }
    }

private:
    int totalCount = 0;
    int runningCount = 0;
    double estBps = 0.0;
    double estUtil = 0.0;
};

/**
 * @brief Visual status badge next to the serial connection controls.
 * Renders an antialiased LED indicator and status text for link states.
 */
class ConnectionStatusWidget : public QWidget
{
public:
    enum class State
    {
        Disconnected,
        Connecting,
        Connected,
        Reconnecting
    };

    /**
     * @brief Construct connection status badge.
     * @param parent Optional parent widget.
     */
    explicit ConnectionStatusWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setFixedSize(116, 32);
        setAccessibleName("Connection status");
        updateTooltip();
    }

    /**
     * @brief Update status from worker state name and open flag.
     * @param state State name string from SerialWorker.
     * @param open True if serial port is open.
     */
    void setStatus(const QString &state, bool open)
    {
        State nextState = State::Disconnected;
        if (open)
        {
            nextState = State::Connected;
        }
        else if (state == "Connecting")
        {
            nextState = State::Connecting;
        }
        else if (state == "Reconnecting")
        {
            nextState = State::Reconnecting;
        }
        else
        {
            nextState = State::Disconnected;
        }

        if (m_state != nextState)
        {
            m_state = nextState;
            updateTooltip();
            update();
        }
    }

    /**
     * @brief Set direct state enum (convenient for testing).
     * @param state New connection state.
     */
    void setState(State state)
    {
        if (m_state != state)
        {
            m_state = state;
            updateTooltip();
            update();
        }
    }

    /**
     * @brief Get current connection state.
     * @return State enum value.
     */
    State state() const
    {
        return m_state;
    }

    /**
     * @brief Human-readable text for current state.
     * @return Text label.
     */
    QString text() const
    {
        switch (m_state)
        {
        case State::Connected:
            return "Connected";
        case State::Connecting:
            return "Connecting...";
        case State::Reconnecting:
            return "Reconnecting...";
        case State::Disconnected:
        default:
            return "Disconnected";
        }
    }

    /**
     * @brief Recommended dimensions for the status badge.
     * @return 116x32 size hint.
     */
    QSize sizeHint() const override
    {
        return QSize(116, 32);
    }

protected:
    /**
     * @brief Paint status badge with antialiased LED indicator.
     * @param event Paint event from Qt.
     */
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const bool dark = property("darkTheme").isValid() ?
                          property("darkTheme").toBool() :
                          (window() && window()->property("darkTheme").isValid() ?
                           window()->property("darkTheme").toBool() :
                           (palette().color(QPalette::Window).lightness() < 128));

        const QRect r = rect().adjusted(0, 0, -1, -1);
        const qreal radius = 7.0;

        QFont statusFont = font();
        statusFont.setPixelSize(11);

        QColor bgColor;
        QColor borderColor;
        QColor textColor;

        switch (m_state)
        {
        case State::Connected:
            statusFont.setWeight(QFont::DemiBold);
            bgColor = dark ? QColor("#0a2e23") : QColor("#dcfce7");
            borderColor = dark ? QColor("#0d7a5f") : QColor("#86efac");
            textColor = dark ? QColor("#34d399") : QColor("#15803d");
            break;

        case State::Connecting:
            statusFont.setWeight(QFont::DemiBold);
            bgColor = dark ? QColor("#2d2006") : QColor("#fef3c7");
            borderColor = dark ? QColor("#854d0e") : QColor("#fde68a");
            textColor = dark ? QColor("#fbbf24") : QColor("#b45309");
            break;

        case State::Reconnecting:
            statusFont.setWeight(QFont::DemiBold);
            bgColor = dark ? QColor("#341a08") : QColor("#ffedd5");
            borderColor = dark ? QColor("#9a3412") : QColor("#fed7aa");
            textColor = dark ? QColor("#fb923c") : QColor("#c2410c");
            break;

        case State::Disconnected:
        default:
            statusFont.setWeight(QFont::Medium);
            bgColor = dark ? QColor("#1e293b") : QColor("#f1f5f9");
            borderColor = dark ? QColor("#334155") : QColor("#cbd5e1");
            textColor = dark ? QColor("#94a3b8") : QColor("#64748b");
            break;
        }

        // Draw pill background and border
        painter.setPen(QPen(borderColor, 1.0));
        painter.setBrush(bgColor);
        painter.drawRoundedRect(r, radius, radius);

        // Draw centered status label
        painter.setFont(statusFont);
        painter.setPen(textColor);
        painter.drawText(r, Qt::AlignCenter, text());
    }

private:
    void updateTooltip()
    {
        switch (m_state)
        {
        case State::Connected:
            setToolTip("Serial link connected and active");
            break;
        case State::Connecting:
            setToolTip("Opening serial connection...");
            break;
        case State::Reconnecting:
            setToolTip("Serial link lost. Attempting reconnection...");
            break;
        case State::Disconnected:
        default:
            setToolTip("Serial link disconnected");
            break;
        }
    }

    State m_state = State::Disconnected;
};

/**
 * @brief Modern status bar widget displaying segmented metric chips.
 * Visualizes RX/TX throughput, CRC health, queue backpressure, and overflow.
 */
class StatusBarMetricsWidget : public QWidget
{
public:
    /**
     * @brief Construct segmented metrics bar.
     * @param parent Optional parent widget.
     */
    explicit StatusBarMetricsWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);

        rxChip = createChip(QString("RX ↓ %1").arg(formatRate(0.0, m_unit)), "Incoming serial throughput (bytes/second)");
        txChip = createChip(QString("TX ↑ %1").arg(formatRate(0.0, m_unit)), "Outgoing serial throughput (bytes/second)");
        crcChip = createChip("CRC 0", "MAVLink CRC checksum errors detected");
        pendingChip = createChip("Pending 0 B", "Outbound serial write queue backlog");
        overflowChip = createChip("Overflow 0", "Serial RX buffer hardware/kernel drops");

        layout->addWidget(rxChip);
        layout->addWidget(txChip);
        layout->addWidget(crcChip);
        layout->addWidget(pendingChip);
        layout->addWidget(overflowChip);

        setFixedHeight(26);
        applyStyles();
    }

    /**
     * @brief Update metrics and refresh chip visual states.
     * @param rxBps Incoming bytes per second.
     * @param txBps Outgoing bytes per second.
     * @param crc CRC errors count.
     * @param pending Pending write queue bytes.
     * @param overflow Buffer overflow drop count.
     * @param dark True if dark theme is active.
     * @param rxUtil Percentage of serial link capacity utilized by RX.
     * @param txUtil Percentage of serial link capacity utilized by TX.
     * @param baud Active baud rate.
     */
    void updateMetrics(double rxBps, double txBps, uint64_t crc, uint64_t pending, uint64_t overflow, bool dark,
                       double rxUtil = 0.0, double txUtil = 0.0, int baud = 0)
    {
        m_dark = dark;
        m_rxBps = rxBps;
        m_txBps = txBps;
        m_crc = crc;
        m_pending = pending;
        m_overflow = overflow;
        m_rxUtil = rxUtil;
        m_txUtil = txUtil;
        m_baud = baud;

        rxChip->setText(QString("RX ↓ %1").arg(formatRate(rxBps, m_unit)));
        txChip->setText(QString("TX ↑ %1").arg(formatRate(txBps, m_unit)));
        crcChip->setText(QString("CRC %1").arg(crc));
        pendingChip->setText(QString("Pending %1 B").arg(pending));
        overflowChip->setText(QString("Overflow %1").arg(overflow));

        if (baud > 0)
        {
            rxChip->setToolTip(QString("Incoming serial throughput: %1 (%2% of %3 baud link)")
                                   .arg(QString::fromStdString(Mav::formatByteRate(rxBps)))
                                   .arg(rxUtil, 0, 'f', 1)
                                   .arg(baud));
            txChip->setToolTip(QString("Outgoing serial throughput: %1 (%2% of %3 baud link)")
                                   .arg(QString::fromStdString(Mav::formatByteRate(txBps)))
                                   .arg(txUtil, 0, 'f', 1)
                                   .arg(baud));
        }
        else
        {
            rxChip->setToolTip(QString("Incoming serial throughput: %1").arg(QString::fromStdString(Mav::formatByteRate(rxBps))));
            txChip->setToolTip(QString("Outgoing serial throughput: %1").arg(QString::fromStdString(Mav::formatByteRate(txBps))));
        }

        applyStyles();
    }

    /**
     * @brief Set throughput rate display unit (0: B/s, 1: KB/s, 2: MB/s).
     * @param unit Unit index.
     */
    void setRateUnit(int unit)
    {
        if (m_unit != unit)
        {
            m_unit = unit;
            updateMetrics(m_rxBps, m_txBps, m_crc, m_pending, m_overflow, m_dark, m_rxUtil, m_txUtil, m_baud);
        }
    }

    /**
     * @brief Format byte rate according to the selected unit.
     * @param bps Bytes per second.
     * @param unit Unit index (0: B/s, 1: KB/s, 2: MB/s).
     * @return Formatted string with numeric value and unit suffix.
     */
    static QString formatRate(double bps, int unit)
    {
        if (unit == 0)
        {
            return QString("%1 B/s").arg(bps, 0, 'f', 0);
        }
        else if (unit == 2)
        {
            return QString("%1 MB/s").arg(bps / (1024.0 * 1024.0), 0, 'f', 2);
        }
        return QString("%1 KB/s").arg(bps / 1024.0, 0, 'f', 1);
    }

    /**
     * @brief Update theme mode and re-apply styles.
     * @param dark True for dark theme, false for light theme.
     */
    void setDarkTheme(bool dark)
    {
        if (m_dark != dark)
        {
            m_dark = dark;
            applyStyles();
        }
    }

private:
    /**
     * @brief Helper to initialize a styled metric chip.
     * @param text Initial text.
     * @param tooltip Informative tooltip.
     * @return Configured QLabel pointer.
     */
    QLabel *createChip(const QString &text, const QString &tooltip)
    {
        auto *label = new QLabel(text, this);
        label->setFixedHeight(24);
        label->setAlignment(Qt::AlignCenter);
        label->setToolTip(tooltip);
        return label;
    }

    /**
     * @brief Apply contextual stylesheets to all metric chips based on health thresholds.
     */
    void applyStyles()
    {
        const QString neutralStyle = m_dark ?
            "QLabel { background: #141c2b; color: #8896b3; border: 1px solid #233047; border-radius: 5px; padding: 2px 8px; font-size: 11px; font-weight: 500; }" :
            "QLabel { background: #e2e8f2; color: #55627a; border: 1px solid #d0d9e7; border-radius: 5px; padding: 2px 8px; font-size: 11px; font-weight: 500; }";

        const QString rxActiveStyle = m_dark ?
            "QLabel { background: #09231b; color: #34d399; border: 1px solid #0f5945; border-radius: 5px; padding: 2px 8px; font-size: 11px; font-weight: 600; }" :
            "QLabel { background: #e6f9ee; color: #16a34a; border: 1px solid #bbf7d0; border-radius: 5px; padding: 2px 8px; font-size: 11px; font-weight: 600; }";

        const QString txActiveStyle = m_dark ?
            "QLabel { background: #161636; color: #93c5fd; border: 1px solid #3730a3; border-radius: 5px; padding: 2px 8px; font-size: 11px; font-weight: 600; }" :
            "QLabel { background: #eff4ff; color: #4f46e5; border: 1px solid #c7d2fe; border-radius: 5px; padding: 2px 8px; font-size: 11px; font-weight: 600; }";

        const QString alertStyle = m_dark ?
            "QLabel { background: #2e1215; color: #f87171; border: 1px solid #7f1d1d; border-radius: 5px; padding: 2px 8px; font-size: 11px; font-weight: 600; }" :
            "QLabel { background: #fef2f2; color: #dc2626; border: 1px solid #fecaca; border-radius: 5px; padding: 2px 8px; font-size: 11px; font-weight: 600; }";

        const QString warnStyle = m_dark ?
            "QLabel { background: #2b1a07; color: #fbbf24; border: 1px solid #78350f; border-radius: 5px; padding: 2px 8px; font-size: 11px; font-weight: 600; }" :
            "QLabel { background: #fffbeb; color: #d97706; border: 1px solid #fde68a; border-radius: 5px; padding: 2px 8px; font-size: 11px; font-weight: 600; }";

        if (m_rxUtil > 90.0)
        {
            rxChip->setStyleSheet(alertStyle);
        }
        else if (m_rxUtil > 70.0)
        {
            rxChip->setStyleSheet(warnStyle);
        }
        else
        {
            rxChip->setStyleSheet(m_rxBps > 0 ? rxActiveStyle : neutralStyle);
        }

        if (m_txUtil > 90.0)
        {
            txChip->setStyleSheet(alertStyle);
        }
        else if (m_txUtil > 70.0)
        {
            txChip->setStyleSheet(warnStyle);
        }
        else
        {
            txChip->setStyleSheet(m_txBps > 0 ? txActiveStyle : neutralStyle);
        }
        crcChip->setStyleSheet(m_crc > 0 ? alertStyle : neutralStyle);
        pendingChip->setStyleSheet(m_pending > 4096 ? warnStyle : neutralStyle);
        overflowChip->setStyleSheet(m_overflow > 0 ? alertStyle : neutralStyle);
    }

    QLabel *rxChip = nullptr;
    QLabel *txChip = nullptr;
    QLabel *crcChip = nullptr;
    QLabel *pendingChip = nullptr;
    QLabel *overflowChip = nullptr;

    double m_rxBps = 0;
    double m_txBps = 0;
    uint64_t m_crc = 0;
    uint64_t m_pending = 0;
    uint64_t m_overflow = 0;
    double m_rxUtil = 0.0;
    double m_txUtil = 0.0;
    int m_baud = 0;
    int m_unit = 1;
    bool m_dark = false;
};

/**
 * @brief Modern status bar pill badge for application messages and alerts.
 * Automatically adapts visual tone (Neutral, Success, Warning, Error) based on content
 * and elides long text safely without clipping or word-wrap truncation.
 */
class StatusBarMessageWidget : public QLabel
{
public:
    /**
     * @brief Construct status message pill.
     * @param text Initial text message.
     * @param parent Optional parent widget.
     */
    explicit StatusBarMessageWidget(const QString &text = "Ready", QWidget *parent = nullptr)
        : QLabel(text, parent)
    {
        setFixedHeight(24);
        setAlignment(Qt::AlignCenter);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        setToolTip(text);
    }

    /**
     * @brief Update status text, refresh tooltip, and adapt visual badge tone.
     * @param text Message text string.
     */
    void setText(const QString &text)
    {
        QLabel::setText(text);
        setToolTip(text);
        updateGeometry();
        update();
    }

    /**
     * @brief Update theme mode and refresh styles.
     * @param dark True for dark theme, false for light theme.
     */
    void setDarkTheme(bool dark)
    {
        if (m_dark != dark)
        {
            m_dark = dark;
            update();
        }
    }

    /**
     * @brief Provide dynamic width based on message text length, bound within status bar space.
     * @return Ideal size for the pill badge.
     */
    QSize sizeHint() const override
    {
        QFont f = font();
        f.setPixelSize(11);
        const QString txt = text();
        const bool error = txt.startsWith("Error:", Qt::CaseInsensitive);
        f.setWeight(error ? QFont::DemiBold : QFont::Medium);
        const QFontMetrics fm(f);
        const int textWidth = fm.horizontalAdvance(txt);
        const int width = qBound(60, textWidth + 24, 880);
        return QSize(width, 24);
    }

    /**
     * @brief Minimum size ensuring badge presence.
     * @return Minimum dimensions.
     */
    QSize minimumSizeHint() const override
    {
        return QSize(60, 24);
    }

protected:
    /**
     * @brief Render rounded vector pill surface and elided text with antialiasing.
     * @param event Paint event parameters.
     */
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        const QString txt = text();
        if (toolTip() != txt)
        {
            const_cast<StatusBarMessageWidget *>(this)->setToolTip(txt);
        }

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        QColor bg;
        QColor border;
        QColor textCol;
        bool bold = false;

        if (txt.startsWith("Error:", Qt::CaseInsensitive))
        {
            bg = m_dark ? QColor("#2e1215") : QColor("#fef2f2");
            border = m_dark ? QColor("#7f1d1d") : QColor("#fecaca");
            textCol = m_dark ? QColor("#f87171") : QColor("#dc2626");
            bold = true;
        }
        else if (txt.startsWith("Warning:", Qt::CaseInsensitive) ||
                 txt.startsWith("Link lost", Qt::CaseInsensitive) ||
                 txt.contains("Reconnecting", Qt::CaseInsensitive))
        {
            bg = m_dark ? QColor("#2b1a07") : QColor("#fffbeb");
            border = m_dark ? QColor("#78350f") : QColor("#fde68a");
            textCol = m_dark ? QColor("#fbbf24") : QColor("#d97706");
            bold = true;
        }
        else if (txt.compare("Connected", Qt::CaseInsensitive) == 0)
        {
            bg = m_dark ? QColor("#09231b") : QColor("#e6f9ee");
            border = m_dark ? QColor("#0f5945") : QColor("#bbf7d0");
            textCol = m_dark ? QColor("#34d399") : QColor("#16a34a");
            bold = true;
        }
        else
        {
            bg = m_dark ? QColor("#141c2b") : QColor("#e2e8f2");
            border = m_dark ? QColor("#233047") : QColor("#d0d9e7");
            textCol = m_dark ? QColor("#8896b3") : QColor("#55627a");
            bold = false;
        }

        painter.setPen(QPen(border, 1));
        painter.setBrush(bg);
        painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 5, 5);

        QFont f = font();
        f.setPixelSize(11);
        f.setWeight(bold ? QFont::DemiBold : QFont::Medium);
        painter.setFont(f);
        painter.setPen(textCol);

        const QFontMetrics fm(f);
        const QRect textRect = rect().adjusted(10, 0, -10, 0);
        const QString visibleText = fm.elidedText(txt, Qt::ElideRight, textRect.width());
        painter.drawText(textRect, Qt::AlignCenter, visibleText);
    }

private:
    bool m_dark = false;
};

/** @brief Wrap a TX identity into at most two lines, eliding only the final overflow. */
inline QStringList txTitleLines(const QString &text, const QFont &font, int width)
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

/**
 * @brief Calculate the bounding rectangle of the drag handle on a TX card.
 * @param card Visual bounding rectangle of the card.
 * @return Bounding rectangle for mouse hit-testing and grip drawing.
 */
inline QRect txDragHandleRect(const QRect &card)
{
    return QRect(card.left() + 4, card.top() + 6, 14, card.height() - 12);
}

/**
 * @brief Map a visual row index to its corresponding source data row during drag-and-drop.
 * @param visualRow The visual row being rendered.
 * @param sourceRow The original row of the card currently being dragged.
 * @param targetRow The current target drop row index where placeholder should appear.
 * @return The source row index to read data from, or -1 if this visual row is the placeholder slot.
 */
inline int txVisualToSourceRow(int visualRow, int sourceRow, int targetRow)
{
    if (sourceRow < 0 || targetRow < 0)
    {
        return visualRow;
    }
    if (visualRow == targetRow)
    {
        return -1;
    }
    if (sourceRow < targetRow)
    {
        if (visualRow < sourceRow)
        {
            return visualRow;
        }
        if (visualRow < targetRow)
        {
            return visualRow + 1;
        }
        return visualRow;
    }
    else if (sourceRow > targetRow)
    {
        if (visualRow < targetRow)
        {
            return visualRow;
        }
        if (visualRow <= sourceRow)
        {
            return visualRow - 1;
        }
        return visualRow;
    }
    return visualRow;
}

/** @brief Calculate the clickable target-rate pill rectangle on a TX card. */
inline QRect txRatePillRect(const QRect &card, const QString &idStr, const QString &nameStr, const QString &hzStr, const QFont &baseFont)
{
    const QRect badge(card.right() - 88, card.top() + 8, 80, 24);
    const int textLeft = card.left() + 24;
    QFont titleFont = baseFont;
    titleFont.setPixelSize(13);
    titleFont.setWeight(QFont::Normal);
    const int maxTitleWidth = badge.left() - textLeft - 8;
    const QString title = QString("[%1] %2").arg(idStr, nameStr);
    const QStringList lines = txTitleLines(title, titleFont, maxTitleWidth);

    QFont pillFont = baseFont;
    pillFont.setPixelSize(11);
    pillFont.setWeight(QFont::Medium);
    const QString rateText = hzStr + " Hz";
    const int rateAdvance = QFontMetrics(pillFont).horizontalAdvance(rateText);
    const int pillWidth = rateAdvance + 22;

    if (lines.size() > 1)
    {
        int maxLineAdvance = 0;
        for (const QString &line : lines)
        {
            maxLineAdvance = std::max(maxLineAdvance, QFontMetrics(titleFont).horizontalAdvance(line));
        }
        const int pillLeft = textLeft + maxLineAdvance + 8;
        if (pillLeft + pillWidth <= badge.left() - 8)
        {
            return QRect(pillLeft, badge.top() + 11, pillWidth, 18);
        }
        return {};
    }
    const int titleAdvance = QFontMetrics(titleFont).horizontalAdvance(title);
    const int pillLeft = textLeft + titleAdvance + 8;
    if (pillLeft + pillWidth <= badge.left() - 8)
    {
        return QRect(pillLeft, badge.top() + 3, pillWidth, 18);
    }
    return {};
}

/** @brief Present each TX entry as a readable card backed by the existing row model. */
class TxCardDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    /** @brief Paint state-specific cards with a compact target-rate badge and distinct actions. */
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const bool dark = option.widget ? option.widget->property("darkTheme").toBool() : false;
        const QColor panelBg(dark ? "#1b2333" : "#ffffff");
        const bool isDragging = option.widget ? option.widget->property("isDragging").toBool() : false;
        const int dragSource = option.widget ? option.widget->property("dragSourceRow").toInt() : -1;
        const int dragTarget = option.widget ? option.widget->property("dragTargetRow").toInt() : -1;
        const int sourceRow = isDragging ? txVisualToSourceRow(index.row(), dragSource, dragTarget) : index.row();

        if (sourceRow == -1)
        {
            painter->save();
            painter->fillRect(option.rect, panelBg);
            painter->setRenderHint(QPainter::Antialiasing);
            const QRect card = MainWindow::txCardRect(option.rect);
            const QColor placeholderBorder(dark ? "#60a5fa" : "#3b82f6");
            const QColor placeholderBg(dark ? "#172338" : "#f0f6ff");
            painter->setPen(QPen(placeholderBorder, 1.0, Qt::SolidLine));
            painter->setBrush(placeholderBg);
            painter->drawRoundedRect(card, 8, 8);
            painter->restore();
            return;
        }

        const auto value = [&index, sourceRow](int column)
        {
            return index.sibling(sourceRow, column).data().toString();
        };
        const bool selected = (option.state & QStyle::State_Selected) && !isDragging;
        const bool hovered = (option.state & QStyle::State_MouseOver) && !isDragging;
        const QString text = value(6);
        const bool running = text == "Running";
        const bool error = text != "Stopped" && !running;
        const QColor foreground(dark ? "#edf0fa" : "#20263b");
        const QColor muted(dark ? "#aab4cd" : "#626c84");
        painter->save();
        painter->fillRect(option.rect, panelBg);
        painter->setRenderHint(QPainter::Antialiasing);

        const double animOffset = (isDragging && option.widget) ? option.widget->property("dragAnimOffset").toDouble() : 0.0;
        const int animFrom = (isDragging && option.widget) ? option.widget->property("dragAnimFrom").toInt() : -1;
        const int animTo = (isDragging && option.widget) ? option.widget->property("dragAnimTo").toInt() : -1;
        int animDy = 0;
        if (isDragging && std::abs(animOffset) > 0.01 && animFrom >= 0 && animTo >= 0)
        {
            const int minRow = std::min(animFrom, animTo);
            const int maxRow = std::max(animFrom, animTo);
            if (index.row() >= minRow && index.row() <= maxRow)
            {
                animDy = int(std::round(animOffset));
            }
        }
        const int swapRowA = option.widget ? option.widget->property("swapRowA").toInt() : -1;
        const int swapRowB = option.widget ? option.widget->property("swapRowB").toInt() : -1;
        const double swapOffset = option.widget ? option.widget->property("swapOffset").toDouble() : 0.0;
        if (swapRowA >= 0 && swapRowB >= 0 && std::abs(swapOffset) > 0.1)
        {
            if (index.row() == swapRowA)
            {
                animDy += int(std::round(swapOffset));
            }
            else if (index.row() == swapRowB)
            {
                animDy -= int(std::round(swapOffset));
            }
        }
        const int dupRow = option.widget ? option.widget->property("duplicateAnimRow").toInt() : -1;
        const double dupOffset = option.widget ? option.widget->property("duplicateAnimOffset").toDouble() : 0.0;
        const double dupProgress = option.widget ? option.widget->property("duplicateAnimProgress").toDouble() : 1.0;
        if (dupRow >= 0 && std::abs(dupOffset) > 0.1)
        {
            if (index.row() == dupRow)
            {
                animDy += int(std::round(dupOffset));
                painter->setClipRect(option.rect.adjusted(0, 0, 0, 10000), Qt::IntersectClip);
                painter->setOpacity(std::clamp(dupProgress, 0.0, 1.0));
            }
            else if (index.row() > dupRow)
            {
                animDy += int(std::round(dupOffset));
            }
        }
        const int delRow = option.widget ? option.widget->property("deleteAnimRow").toInt() : -1;
        const double delOffset = option.widget ? option.widget->property("deleteAnimOffset").toDouble() : 0.0;
        const double delProgress = option.widget ? option.widget->property("deleteAnimProgress").toDouble() : 0.0;
        if (delRow >= 0)
        {
            if (index.row() == delRow)
            {
                animDy += int(std::round(delOffset));
                painter->setClipRect(option.rect.adjusted(0, 0, 0, 10000), Qt::IntersectClip);
                painter->setOpacity(std::clamp(1.0 - delProgress, 0.0, 1.0));
            }
            else if (index.row() > delRow && std::abs(delOffset) > 0.1)
            {
                animDy += int(std::round(delOffset));
            }
        }
        const QRect card = MainWindow::txCardRect(option.rect, animDy);
        const QColor stateAccent = error ? QColor(dark ? "#ef8799" : "#bd405b") :
                                   running ? QColor(dark ? "#57c99a" : "#249569") : QColor(dark ? "#3d506e" : "#b8c8dc");
        QColor surface = panelBg;
        if (hovered)
        {
            surface = dark ? surface.lighter(112) : QColor("#f8fafc");
        }
        const bool isDeletingThis = (delRow >= 0 && index.row() == delRow);
        const QColor cardBorder = (selected && !isDeletingThis) ? QColor("#818cf8") : QColor(dark ? "#3d506e" : "#c2d0e2");
        painter->setPen(QPen(cardBorder, (selected && !isDeletingThis) ? 1.5 : 1.2));
        painter->setBrush(surface);
        painter->drawRoundedRect(card, 8, 8);

        // Refined slim 3px state capsule pill inset along left edge
        const QRectF accentPill(card.left() + 8, card.top() + 10, 3, card.height() - 20);
        painter->setPen(Qt::NoPen);
        painter->setBrush(stateAccent);
        painter->drawRoundedRect(accentPill, 1.5, 1.5);

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
        }

        // Draw compact interactive Target Hz pill with dropdown chevron
        const QRect pillRect = txRatePillRect(card, value(0), value(1), value(3), option.font);
        if (pillRect.isValid())
        {
            const QString rateText = value(3) + " Hz";
            QFont pillFont = option.font;
            pillFont.setPixelSize(11);
            pillFont.setWeight(QFont::Medium);
            painter->setPen(QPen(QColor(dark ? "#384a65" : "#cbd7e8"), 1));
            painter->setBrush(QColor(dark ? "#233144" : "#e6effa"));
            painter->drawRoundedRect(pillRect, 4, 4);
            painter->setFont(pillFont);
            painter->setPen(QColor(dark ? "#b0cbef" : "#2b5687"));
            painter->drawText(pillRect.adjusted(6, 0, -12, 0), Qt::AlignVCenter | Qt::AlignLeft, rateText);

            // Draw crisp vector down-chevron indicating dropdown switcher
            painter->setPen(QPen(QColor(dark ? "#7a90b5" : "#5b7da8"), 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            const int cx = pillRect.right() - 8;
            const int cy = pillRect.center().y();
            painter->drawLine(QPoint(cx - 3, cy - 1), QPoint(cx, cy + 2));
            painter->drawLine(QPoint(cx, cy + 2), QPoint(cx + 3, cy - 1));
        }

        // Draw schema payload wire length badge if space permits
        const uint32_t msgId = value(0).toUInt();
        const auto *metadata = mavlink_get_msg_entry(msgId);
        const uint8_t payloadLen = metadata ? metadata->max_msg_len : 0;
        const QString payloadText = QString("%1 B").arg(payloadLen);
        QFont payloadFont = option.font;
        payloadFont.setPixelSize(10);
        payloadFont.setWeight(QFont::Medium);
        const int payloadAdvance = QFontMetrics(payloadFont).horizontalAdvance(payloadText);
        const int payloadWidth = payloadAdvance + 10;
        const int payloadX = pillRect.isValid() ? (pillRect.right() + 6) : (textLeft + (lines.size() > 1 ? maxTitleWidth : QFontMetrics(titleFont).horizontalAdvance(title)) + 8);
        if (payloadX + payloadWidth <= badge.left() - 6)
        {
            const QRect payloadRect(payloadX, badge.top() + (badge.height() - 18) / 2, payloadWidth, 18);
            painter->setPen(QPen(QColor(dark ? "#384a65" : "#cbd7e8"), 1));
            painter->setBrush(QColor(dark ? "#1a2536" : "#f1f5f9"));
            painter->drawRoundedRect(payloadRect, 4, 4);
            painter->setFont(payloadFont);
            painter->setPen(QColor(dark ? "#8ca0be" : "#475569"));
            painter->drawText(payloadRect, Qt::AlignCenter, payloadText);
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

/**
 * @brief Render a crisp vector 'x' icon for search field clear buttons.
 * @param dark Whether dark theme palette is active.
 * @return Theme-aware QIcon with normal and active hover states.
 */
inline QIcon clearButtonIcon(bool dark)
{
    const QColor normalColor = dark ? QColor("#8992a6") : QColor("#7e899f");
    const QColor activeColor = dark ? QColor("#f1f5f9") : QColor("#0f172a");

    auto makePixmap = [](const QColor &color) -> QPixmap
    {
        QPixmap pixmap(32, 32);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.scale(2, 2);
        painter.setPen(QPen(color, 1.45, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(QPointF(4.5, 4.5), QPointF(11.5, 11.5));
        painter.drawLine(QPointF(11.5, 4.5), QPointF(4.5, 11.5));
        return pixmap;
    };

    QIcon icon;
    icon.addPixmap(makePixmap(normalColor), QIcon::Normal);
    icon.addPixmap(makePixmap(activeColor), QIcon::Active);
    return icon;
}

/** @brief Draw a theme-aware search glyph while retaining native clear and keyboard actions. */
class CatalogSearch : public QLineEdit
{
public:
    /** @brief Reserve space for the search glyph and enable the standard clear action with vector 'x' icon. */
    explicit CatalogSearch(QWidget *parent = nullptr) : QLineEdit(parent)
    {
        setTextMargins(22, 0, 0, 0);
        setClearButtonEnabled(true);
        setAttribute(Qt::WA_InputMethodEnabled, false);
        setInputMethodHints(Qt::ImhLatinOnly | Qt::ImhNoPredictiveText);
        updatePlaceholderColor();
        updateClearButtonIcon();
    }

    /** @brief Apply custom vector 'x' icon to the embedded clear button. */
    void updateClearButtonIcon()
    {
        auto *clearBtn = findChild<QToolButton *>();
        if (clearBtn)
        {
            const bool dark = palette().color(QPalette::Window).lightness() < 128;
            clearBtn->setIcon(clearButtonIcon(dark));
        }
    }

protected:
    /** @brief Keep search hints and clear button subdued when the application theme changes. */
    void changeEvent(QEvent *event) override
    {
        QLineEdit::changeEvent(event);
        if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange ||
            event->type() == QEvent::StyleChange)
        {
            updatePlaceholderColor();
            updateClearButtonIcon();
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

/** @brief Floating badge providing transient visual feedback when copying decoded MAVLink values. */
class CopyFeedbackBadge : public QLabel
{
public:
    explicit CopyFeedbackBadge(QWidget *parent = nullptr) : QLabel(parent)
    {
        setObjectName("copyFeedbackBadge");
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAlignment(Qt::AlignCenter);
        hide();
    }

    /** @brief Refresh badge styling according to the active theme palette. */
    void refreshTheme(bool dark)
    {
        if (dark)
        {
            setStyleSheet("QLabel#copyFeedbackBadge {"
                          "  background: #064e3b;"
                          "  color: #6ee7b7;"
                          "  border: 1px solid #10b981;"
                          "  border-radius: 4px;"
                          "  padding: 1px 7px;"
                          "  font-size: 11px;"
                          "  font-weight: 600;"
                          "}");
        }
        else
        {
            setStyleSheet("QLabel#copyFeedbackBadge {"
                          "  background: #ecfdf5;"
                          "  color: #065f46;"
                          "  border: 1px solid #059669;"
                          "  border-radius: 4px;"
                          "  padding: 1px 7px;"
                          "  font-size: 11px;"
                          "  font-weight: 600;"
                          "}");
        }
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
        const bool copied = isInspector && index.data(Qt::UserRole + 6).toBool();
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
                               copied ? QColor(dark ? "#34d399" : "#059669") :
                               changed ? QColor(dark ? "#388bfd" : "#0969da") :
                               selected || hovered ? QColor("#939bf0") : border;
        painter->setPen(QPen(outline, selected || invalid || changed || copied ? 1.5 : 1.0));
        const QColor boxBg = invalid ? QColor(dark ? "#482e3f" : "#fff0f1") :
                             copied ? QColor(dark ? "#0f2e24" : "#ecfdf5") :
                             selected ? QColor(dark ? "#293451" : "#f2f1ff") :
                             editable ? QColor(dark ? "#1e293b" : "#fafbfd") :
                             QColor(dark ? "#1b283d" : "#f4f7fb");
        painter->setBrush(boxBg);
        painter->drawRoundedRect(input, 5, 5);

        painter->setFont(styled.font);
        const QColor textColor = copied ? QColor(dark ? "#6ee7b7" : "#065f46") :
                                 changed ? QColor(dark ? "#58a6ff" : "#0969da") :
                                 isInspector ? QColor(dark ? "#c9d8eb" : "#24324a") :
                                 styled.palette.color(QPalette::Text);
        painter->setPen(textColor);
        const int rightPadding = copied ? 75 : 8;
        const QRect textRect = input.adjusted(8, 0, -rightPadding, 0);
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

            QFont font = cachedFixedFont();
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
        option->font = cachedFixedFont();
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
inline QString rxStatus(double age)
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
inline QPushButton *button(const QString &text, const char *name, QBoxLayout *layout)
{
    auto *control = new QPushButton(text);
    control->setObjectName(name);
    layout->addWidget(control);
    return control;
}

/**
 * @brief Table widget for TX cards with minimalist empty state rendering.
 */
class TxTableWidget : public QTableWidget
{
public:
    using QTableWidget::QTableWidget;

protected:
    /**
     * @brief Paint table contents and render empty state message when no rows are configured.
     * @param event Paint event.
     */
    void paintEvent(QPaintEvent *event) override
    {
        QTableWidget::paintEvent(event);
        if (rowCount() == 0)
        {
            QPainter painter(viewport());
            painter.setRenderHint(QPainter::Antialiasing, true);

            const bool dark = property("darkTheme").toBool();
            const QRect rect = viewport()->rect();
            const int cx = rect.center().x();
            const int cy = rect.center().y();

            // Minimalist Outgoing Transmission Glyph (Paper Airplane)
            const QColor iconColor = dark ? QColor("#607290") : QColor("#94a3b8");
            painter.setPen(QPen(iconColor, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.setBrush(Qt::NoBrush);

            // Center of icon at (cx, cy - 26)
            const int iconY = cy - 26;
            QPainterPath plane;
            plane.moveTo(cx + 14, iconY - 9);
            plane.lineTo(cx - 14, iconY - 3);
            plane.lineTo(cx - 4, iconY + 2);
            plane.lineTo(cx - 4, iconY + 9);
            plane.lineTo(cx + 1, iconY + 3);
            plane.lineTo(cx + 14, iconY - 9);
            plane.closeSubpath();
            painter.drawPath(plane);

            // Plane fold line
            painter.drawLine(QPointF(cx - 4, iconY + 2), QPointF(cx + 14, iconY - 9));

            // Subtle dashed trail behind plane
            painter.setPen(QPen(iconColor, 1.2, Qt::DashLine, Qt::RoundCap));
            painter.drawLine(QPointF(cx - 24, iconY + 6), QPointF(cx - 16, iconY + 2));

            // Title: "No messages configured"
            QFont titleFont = font();
            titleFont.setPixelSize(13);
            titleFont.setWeight(QFont::DemiBold);
            painter.setFont(titleFont);
            painter.setPen(dark ? QColor("#edf0fa") : QColor("#1e293b"));
            const QRect titleRect(rect.left() + 16, iconY + 20, rect.width() - 32, 20);
            painter.drawText(titleRect, Qt::AlignCenter, "No messages configured");

            // Subtitle: "Click '+ Add message' to create your first transmission"
            QFont subFont = font();
            subFont.setPixelSize(11);
            subFont.setWeight(QFont::Normal);
            painter.setFont(subFont);
            painter.setPen(dark ? QColor("#8992a6") : QColor("#64748b"));
            const QRect subRect(rect.left() + 16, iconY + 42, rect.width() - 32, 18);
            painter.drawText(subRect, Qt::AlignCenter, "Click '+ Add message' to create your first transmission");
        }
    }
};

/**
 * @brief Tree widget for RX telemetry streams with state-aware waiting state rendering.
 */
class RxTreeWidget : public QTreeWidget
{
public:
    using QTreeWidget::QTreeWidget;

    /**
     * @brief Update connection parameters for the waiting state view.
     * @param connected Whether serial link is open.
     * @param portName Name of the connected serial port.
     * @param baudRate Current baud rate.
     */
    void setConnectionInfo(bool connected, const QString &portName, const QString &baudRate)
    {
        if (connected_ != connected || portName_ != portName || baudRate_ != baudRate)
        {
            connected_ = connected;
            portName_ = portName;
            baudRate_ = baudRate;
            viewport()->update();
        }
    }

protected:
    /**
     * @brief Paint tree contents and render waiting state when no streams have been received.
     * @param event Paint event.
     */
    void paintEvent(QPaintEvent *event) override
    {
        QTreeWidget::paintEvent(event);
        if (topLevelItemCount() == 0)
        {
            QPainter painter(viewport());
            painter.setRenderHint(QPainter::Antialiasing, true);

            const bool dark = property("darkTheme").toBool();
            const QRect rect = viewport()->rect();
            const int cx = rect.center().x();
            const int cy = rect.center().y();
            const int iconY = cy - 28;

            if (!connected_)
            {
                // Disconnected State: Broken Cable / Plug Glyph
                const QColor iconColor = dark ? QColor("#607290") : QColor("#94a3b8");
                painter.setPen(QPen(iconColor, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                painter.setBrush(Qt::NoBrush);

                // Left plug
                painter.drawLine(QPointF(cx - 20, iconY), QPointF(cx - 10, iconY));
                painter.drawRoundedRect(QRectF(cx - 10, iconY - 6, 6, 12), 2, 2);
                painter.drawLine(QPointF(cx - 4, iconY - 3), QPointF(cx - 1, iconY - 3));
                painter.drawLine(QPointF(cx - 4, iconY + 3), QPointF(cx - 1, iconY + 3));

                // Right socket
                painter.drawRoundedRect(QRectF(cx + 4, iconY - 6, 6, 12), 2, 2);
                painter.drawLine(QPointF(cx + 10, iconY), QPointF(cx + 20, iconY));

                // Title: "Serial Disconnected"
                QFont titleFont = font();
                titleFont.setPixelSize(13);
                titleFont.setWeight(QFont::DemiBold);
                painter.setFont(titleFont);
                painter.setPen(dark ? QColor("#edf0fa") : QColor("#1e293b"));
                const QRect titleRect(rect.left() + 16, iconY + 20, rect.width() - 32, 20);
                painter.drawText(titleRect, Qt::AlignCenter, "Serial Disconnected");

                // Subtitle
                QFont subFont = font();
                subFont.setPixelSize(11);
                subFont.setWeight(QFont::Normal);
                painter.setFont(subFont);
                painter.setPen(dark ? QColor("#8992a6") : QColor("#64748b"));
                const QRect subRect(rect.left() + 16, iconY + 42, rect.width() - 32, 18);
                painter.drawText(subRect, Qt::AlignCenter, "Select a serial port and click Connect to start receiving telemetry");
            }
            else
            {
                // Connected State: Pure UART Digital Square Waveform (Emerald pulse)
                const QColor pulseColor = dark ? QColor("#34d399") : QColor("#059669");
                painter.setPen(QPen(pulseColor, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                painter.setBrush(Qt::NoBrush);

                // Serial UART Framing Waveform: Idle High -> Start bit Low -> Data 1 -> Data 0 -> Stop 1
                const double highY = iconY + 1;
                const double lowY = iconY + 13;
                QPainterPath wave;
                wave.moveTo(cx - 24, highY);
                wave.lineTo(cx - 15, highY); // Idle High
                wave.lineTo(cx - 15, lowY);  // Falling edge: Start bit (0)
                wave.lineTo(cx - 6, lowY);
                wave.lineTo(cx - 6, highY);  // Rising edge: Data bit (1)
                wave.lineTo(cx + 3, highY);
                wave.lineTo(cx + 3, lowY);   // Falling edge: Data bit (0)
                wave.lineTo(cx + 12, lowY);
                wave.lineTo(cx + 12, highY); // Rising edge: Stop bit (1)
                wave.lineTo(cx + 24, highY); // Idle High
                painter.drawPath(wave);

                // Title: "Connected to <port> (<baud>)"
                QFont titleFont = font();
                titleFont.setPixelSize(13);
                titleFont.setWeight(QFont::DemiBold);
                painter.setFont(titleFont);
                painter.setPen(dark ? QColor("#edf0fa") : QColor("#1e293b"));
                const QRect titleRect(rect.left() + 16, iconY + 20, rect.width() - 32, 20);
                const QString titleText = !portName_.isEmpty()
                    ? QString("Connected to %1 (%2)").arg(portName_, baudRate_)
                    : "Connected to Serial Port";
                painter.drawText(titleRect, Qt::AlignCenter, titleText);

                // Subtitle: "Waiting for incoming MAVLink packets on serial port..."
                QFont subFont = font();
                subFont.setPixelSize(11);
                subFont.setWeight(QFont::Normal);
                painter.setFont(subFont);
                painter.setPen(dark ? QColor("#8992a6") : QColor("#64748b"));
                const QRect subRect(rect.left() + 16, iconY + 42, rect.width() - 32, 18);
                painter.drawText(subRect, Qt::AlignCenter, "Waiting for incoming MAVLink packets on serial port...");
            }
        }
    }

private:
    bool connected_ = false;
    QString portName_;
    QString baudRate_;
};

/** @brief Configure a read-only, single-row-selection dashboard table. */
inline QTableWidget *table(const QStringList &headers, bool isTx = false)
{
    auto *result = isTx ? new TxTableWidget(0, headers.size()) : new QTableWidget(0, headers.size());
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
inline QIcon actionIcon(const QString &name, const QColor &color)
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
    else if (name == "deleteAll" || name == "removeMessage")
    {
        painter.setPen(QPen(color, 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
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
        painter.drawRoundedRect(QRectF(2.6, 3.4, 10.8, 2.0), 0.9, 0.9);

        // Body: connects directly to lid bottom with rounded bottom corners
        QPainterPath body;
        body.moveTo(3.8, 5.4);
        body.lineTo(3.8, 12.2);
        body.quadTo(3.8, 13.8, 5.4, 13.8);
        body.lineTo(10.6, 13.8);
        body.quadTo(12.2, 13.8, 12.2, 12.2);
        body.lineTo(12.2, 5.4);
        painter.drawPath(body);

        // Two vertical parallel ribs evenly spaced
        painter.drawLine(QPointF(6.4, 7.2), QPointF(6.4, 11.8));
        painter.drawLine(QPointF(9.6, 7.2), QPointF(9.6, 11.8));
    }
    else if (name == "editMessage")
    {
        QPolygonF pencil;
        pencil << QPointF(3, 10) << QPointF(10, 3) << QPointF(13, 6)
               << QPointF(6, 13) << QPointF(3, 13);
        painter.drawPolygon(pencil);
        painter.drawLine(9, 4, 12, 7);
    }
    else if (name == "duplicateMessage")
    {
        painter.setPen(QPen(color, 1.35, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath backSheet;
        backSheet.moveTo(5.5, 5.0);
        backSheet.lineTo(5.5, 2.5);
        backSheet.lineTo(13.0, 2.5);
        backSheet.lineTo(13.0, 11.0);
        backSheet.lineTo(10.5, 11.0);
        painter.drawPath(backSheet);
        painter.drawRoundedRect(QRectF(3.0, 5.0, 7.5, 8.5), 1.0, 1.0);
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

/**
 * @brief Dynamic drop-target card replacing the Delete All button during dragging,
 * offering animated expansion (28px -> 150px), dashed dropzone aesthetic, smooth
 * color lerping on hover, and smooth collapse on drop/cancel.
 */
class TrashDropCard : public QWidget
{
public:
    /**
     * @brief Construct the drop-target card with initial collapsed geometry and timer.
     * @param parent Owning parent widget.
     */
    explicit TrashDropCard(QWidget *parent = nullptr) : QWidget(parent)
    {
        setObjectName("trashDropCard");
        setAttribute(Qt::WA_Hover, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setMinimumSize(28, 28);
        setMaximumSize(500, 36);
        resize(28, 28);
        m_animTimer = new QTimer(this);
        m_animTimer->setTimerType(Qt::PreciseTimer);
        connect(m_animTimer, &QTimer::timeout, this, [this]()
        {
            onAnimStep();
        });
    }

    /**
     * @brief Start animated expansion with leftward translation towards targetLeft and expansion to targetWidth.
     * @param originPos Base coordinates of the delete button to expand from.
     * @param centerY Vertical center coordinate for expansion.
     * @param targetLeft Calculated left boundary after sliding left towards the Stop all button.
     * @param targetWidth Calculated target width spanning towards the Add message button.
     */
    void startExpandFromLeft(const QPoint &originPos, int centerY, int targetLeft, int targetWidth)
    {
        m_basePos = originPos;
        m_originX = originPos.x();
        m_centerY = centerY;
        m_targetLeft = targetLeft;
        m_targetWidth = std::max(targetWidth, 120);
        m_expanding = true;
        m_collapsing = false;
        m_hovered = false;
        m_hoverProgress = 0.0;
        m_expandProgress = 0.0;
        m_currentX = m_originX;
        m_currentWidth = 28;
        m_currentHeight = 28;
        m_onCollapseFinished = nullptr;
        m_expandTimer.restart();
        setGeometry(m_originX, m_centerY - 14, 28, 28);
        show();
        raise();
        if (!m_animTimer->isActive())
        {
            m_animTimer->start(8);
        }
        update();
    }

    /**
     * @brief Start animated expansion using default width parameters.
     * @param originPos Base coordinates of the delete button to expand from.
     */
    void startExpand(const QPoint &originPos)
    {
        startExpandFromLeft(originPos, originPos.y() + 14, originPos.x(), 190);
    }

    /**
     * @brief Start animated collapse back to 28x28 and invoke completion callback.
     * @param onFinished Optional callback invoked after collapse completes and widget hides.
     */
    void startCollapse(std::function<void()> onFinished = nullptr)
    {
        m_onCollapseFinished = std::move(onFinished);
        m_expanding = false;
        m_collapsing = true;
        m_hovered = false;
        m_collapseStartX = x();
        m_collapseStartWidth = width();
        m_collapseStartHeight = height();
        m_collapseTimer.restart();
        if (!m_animTimer->isActive())
        {
            m_animTimer->start(8);
        }
        update();
    }

    /**
     * @brief Set hover state to trigger smooth danger color and glow transition.
     * @param hovered True if mouse cursor is currently over the trash zone.
     */
    void setHovered(bool hovered)
    {
        if (m_hovered != hovered)
        {
            m_hovered = hovered;
            if (!m_animTimer->isActive())
            {
                m_animTimer->start(8);
            }
        }
    }

    /**
     * @brief Query whether the card is currently hovered.
     * @return True if hovered.
     */
    bool isHovered() const
    {
        return m_hovered;
    }

    /**
     * @brief Query the full expanded target width.
     * @return Full target width in pixels.
     */
    int fullWidth() const
    {
        return m_targetWidth;
    }

    /**
     * @brief Query the full expanded target height.
     * @return Full target height in pixels (32px).
     */
    int fullHeight() const
    {
        return 32;
    }

    /**
     * @brief Query current expansion progress (0.0 to 1.0).
     * @return Current progress value.
     */
    double expandProgress() const
    {
        return m_expandProgress;
    }

    /**
     * @brief Query current hover transition progress (0.0 to 1.0).
     * @return Current hover progress value.
     */
    double hoverProgress() const
    {
        return m_hoverProgress;
    }

protected:
    /**
     * @brief Render the drop card with frosted glass background, subtle top highlight, glow, and clipped text.
     * @param event Paint event parameters.
     */
    void paintEvent(QPaintEvent *) override
    {
        const bool dark = qApp->property("darkTheme").toBool();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        auto lerpColor = [](const QColor &c1, const QColor &c2, double t) -> QColor
        {
            t = std::clamp(t, 0.0, 1.0);
            return QColor::fromRgbF(
                c1.redF() + (c2.redF() - c1.redF()) * t,
                c1.greenF() + (c2.greenF() - c1.greenF()) * t,
                c1.blueF() + (c2.blueF() - c1.blueF()) * t,
                c1.alphaF() + (c2.alphaF() - c1.alphaF()) * t);
        };

        // Translucent frosted glass burgundy (dark) / rose milk (light)
        const QColor idleBg = dark ? QColor(45, 18, 24, 175) : QColor(254, 242, 242, 185);
        const QColor hoverBg = dark ? QColor(85, 24, 38, 225) : QColor(254, 226, 226, 230);

        const QColor idleBorder = dark ? QColor(244, 63, 94, 90) : QColor(252, 165, 165, 140);
        const QColor hoverBorder = dark ? QColor(251, 113, 133, 210) : QColor(239, 68, 68, 220);

        const QColor idleFg = dark ? QColor("#f87171") : QColor("#dc2626");
        const QColor hoverFg = dark ? QColor("#fda4af") : QColor("#b91c1c");

        const QColor bg = lerpColor(idleBg, hoverBg, m_hoverProgress);
        const QColor border = lerpColor(idleBorder, hoverBorder, m_hoverProgress);
        const QColor fg = lerpColor(idleFg, hoverFg, m_hoverProgress);

        const QRect cardRect = rect().adjusted(1, 1, -1, -2);

        // 1. Soft drop shadow on expansion (elevation above adjacent controls)
        if (m_expandProgress > 0.01)
        {
            QPainterPath shadowPath;
            shadowPath.addRoundedRect(QRectF(cardRect).translated(0, 1.5), 6, 6);
            const QColor shadowColor = dark ? QColor(0, 0, 0, int(75 * m_expandProgress))
                                            : QColor(15, 23, 42, int(25 * m_expandProgress));
            painter.fillPath(shadowPath, shadowColor);
        }

        // 2. Soft red glow on hover
        if (m_hoverProgress > 0.02)
        {
            QPainterPath glowPath;
            glowPath.addRoundedRect(QRectF(cardRect).adjusted(-1.5, -1.5, 1.5, 1.5), 7, 7);
            const QColor glowColor = dark ? QColor(251, 113, 133, int(45 * m_hoverProgress))
                                          : QColor(220, 38, 38, int(35 * m_hoverProgress));
            painter.fillPath(glowPath, glowColor);
        }

        // 3. Frosted glass plate and thin solid border (1.0px -> 1.5px on hover)
        QPen pen(border, 1.0 + 0.5 * m_hoverProgress, Qt::SolidLine);

        painter.setPen(pen);
        painter.setBrush(bg);
        painter.drawRoundedRect(cardRect, 6, 6);

        // 4. Subtle frosted glass top reflection highlight
        if (width() > 36)
        {
            QPen highlightPen(QColor(255, 255, 255, dark ? 25 : 65), 1.0);
            painter.setPen(highlightPen);
            painter.drawLine(cardRect.left() + 6, cardRect.top() + 1, cardRect.right() - 6, cardRect.top() + 1);
        }

        // 5. Trash icon (anchored at left edge, centered vertically)
        const QIcon icon = actionIcon("deleteAll", fg);
        const int iconY = (height() - 16) / 2;
        painter.drawPixmap(6, iconY, icon.pixmap(16, 16));

        // 6. Smooth text fade-in clipped to expanding card width
        if (width() > 36)
        {
            const double textAlpha = std::clamp((m_expandProgress - 0.15) / 0.85, 0.0, 1.0);
            if (textAlpha > 0.01)
            {
                painter.save();
                painter.setOpacity(textAlpha);
                QFont font = this->font();
                font.setPixelSize(11);
                font.setWeight(m_hoverProgress > 0.5 ? QFont::DemiBold : QFont::Medium);
                painter.setFont(font);
                painter.setPen(fg);
                painter.drawText(QRect(28, 0, width() - 32, height()),
                                 Qt::AlignVCenter | Qt::AlignLeft, "Drop here to delete");
                painter.restore();
            }
        }
    }

private:
    /**
     * @brief Advance width/height expansion/collapse and hover color interpolation.
     */
    void onAnimStep()
    {
        bool needContinue = false;

        if (m_expanding)
        {
            const qint64 elapsed = m_expandTimer.elapsed();
            const double duration = 220.0;
            const double t = std::clamp(double(elapsed) / duration, 0.0, 1.0);
            const double ease = 1.0 - std::pow(1.0 - t, 2.0);
            m_expandProgress = ease;
            m_currentX = int(m_originX + (m_targetLeft - m_originX) * ease);
            m_currentWidth = int(28 + (m_targetWidth - 28) * ease);
            m_currentHeight = int(28 + (32 - 28) * ease);
            setGeometry(m_currentX, m_centerY - m_currentHeight / 2,
                        m_currentWidth, m_currentHeight);

            if (t >= 1.0)
            {
                m_expanding = false;
                m_expandProgress = 1.0;
                m_currentX = m_targetLeft;
                m_currentWidth = m_targetWidth;
                m_currentHeight = 32;
                setGeometry(m_targetLeft, m_centerY - 16, m_targetWidth, 32);
            }
            else
            {
                needContinue = true;
            }
        }
        else if (m_collapsing)
        {
            const qint64 elapsed = m_collapseTimer.elapsed();
            const double duration = 180.0;
            const double t = std::clamp(double(elapsed) / duration, 0.0, 1.0);
            const double ease = 1.0 - std::pow(1.0 - t, 2.0);
            m_expandProgress = 1.0 - ease;
            m_currentX = int(m_collapseStartX + (m_originX - m_collapseStartX) * ease);
            m_currentWidth = int(m_collapseStartWidth + (28 - m_collapseStartWidth) * ease);
            m_currentHeight = int(m_collapseStartHeight + (28 - m_collapseStartHeight) * ease);
            setGeometry(m_currentX, m_centerY - m_currentHeight / 2,
                        m_currentWidth, m_currentHeight);

            if (t >= 1.0)
            {
                m_collapsing = false;
                m_expandProgress = 0.0;
                m_currentX = m_originX;
                m_currentWidth = 28;
                m_currentHeight = 28;
                setGeometry(m_originX, m_centerY - 14, 28, 28);
                hide();
                if (m_onCollapseFinished)
                {
                    auto cb = m_onCollapseFinished;
                    m_onCollapseFinished = nullptr;
                    cb();
                }
            }
            else
            {
                needContinue = true;
            }
        }

        const double targetHover = m_hovered ? 1.0 : 0.0;
        if (std::abs(m_hoverProgress - targetHover) > 0.01)
        {
            m_hoverProgress += (targetHover - m_hoverProgress) * 0.12;
            if (std::abs(m_hoverProgress - targetHover) <= 0.01)
            {
                m_hoverProgress = targetHover;
            }
            else
            {
                needContinue = true;
            }
        }
        else
        {
            m_hoverProgress = targetHover;
        }

        update();

        if (!needContinue)
        {
            m_animTimer->stop();
        }
    }

    QPoint m_basePos;
    int m_originX = 0;
    int m_centerY = 0;
    int m_targetLeft = 0;
    int m_targetWidth = 190;
    int m_currentX = 0;
    int m_currentWidth = 28;
    int m_currentHeight = 28;
    int m_collapseStartX = 0;
    int m_collapseStartWidth = 190;
    int m_collapseStartHeight = 32;
    double m_expandProgress = 0.0;
    double m_hoverProgress = 0.0;
    bool m_hovered = false;
    bool m_expanding = false;
    bool m_collapsing = false;
    QTimer *m_animTimer = nullptr;
    QElapsedTimer m_expandTimer;
    QElapsedTimer m_collapseTimer;
    std::function<void()> m_onCollapseFinished;
};

/**
 * @brief Floating frosted acrylic toast notification with progress countdown and undo button.
 */
class WorkbenchToast : public QWidget
{
public:
    /**
     * @brief Construct the floating undo capsule widget and animation timers.
     * @param parent Owning parent window.
     */
    explicit WorkbenchToast(QWidget *parent = nullptr) : QWidget(parent)
    {
        setObjectName("workbenchToast");
        setAttribute(Qt::WA_TranslucentBackground, true);
        setCursor(Qt::PointingHandCursor);
        setFixedSize(72, 28);

        m_tickTimer = new QTimer(this);
        connect(m_tickTimer, &QTimer::timeout, this, [this]()
        {
            onTick();
        });

        m_animTimer = new QTimer(this);
        m_animTimer->setTimerType(Qt::PreciseTimer);
        connect(m_animTimer, &QTimer::timeout, this, [this]()
        {
            onAnimStep();
        });

        updateThemeStyle(false);
        hide();
    }

    /**
     * @brief Register callback invoked when the user clicks the Undo button.
     * @param cb Callback function.
     */
    void setOnUndoClicked(std::function<void()> cb)
    {
        m_onUndoClicked = std::move(cb);
    }

    /**
     * @brief Set the target anchored position at the bottom-left of the window.
     * @param pos Top-left coordinate.
     */
    void setTargetPosition(const QPoint &pos)
    {
        m_targetPos = pos;
        if (!m_animTimer->isActive())
        {
            move(m_targetPos);
        }
    }

    /**
     * @brief Update stylesheets for child buttons according to the active theme.
     * @param dark True for dark theme; false for light theme.
     */
    void updateThemeStyle(bool dark)
    {
        setProperty("darkTheme", dark);
        update();
    }

    /**
     * @brief Display the toast with message and restart the 4s progress countdown.
     * @param text Notification message to display.
     */
    void showToast(const QString &text)
    {
        Q_UNUSED(text);
        m_totalDurationMs = 4000;
        m_remainingMs = 4000;
        m_pausedRemainingMs = 4000;
        m_hovered = false;
        m_isDismissing = false;
        m_animProgress = 0.0;
        updateThemeStyle(property("darkTheme").toBool());

        m_elapsedTimer.restart();
        m_animElapsed.restart();

        if (!m_tickTimer->isActive())
        {
            m_tickTimer->start(16);
        }
        if (!m_animTimer->isActive())
        {
            m_animTimer->start(8);
        }

        move(m_targetPos.x(), m_targetPos.y() + 20);
        show();
        raise();
        update();
    }

    /**
     * @brief Reset the countdown timer back to full duration without re-animating widget entry.
     */
    void resetCountdown()
    {
        m_totalDurationMs = 4000;
        m_remainingMs = 4000;
        m_pausedRemainingMs = 4000;
        m_isDismissing = false;
        m_animProgress = 1.0;
        if (m_animTimer->isActive())
        {
            m_animTimer->stop();
        }
        move(m_targetPos);
        m_elapsedTimer.restart();
        if (!m_tickTimer->isActive())
        {
            m_tickTimer->start(16);
        }
        show();
        raise();
        update();
    }

    /**
     * @brief Trigger the slide-down and fade-out dismissal animation.
     */
    void dismiss()
    {
        if (m_isDismissing || !isVisible())
        {
            return;
        }
        m_isDismissing = true;
        m_animElapsed.restart();
        if (!m_animTimer->isActive())
        {
            m_animTimer->start(8);
        }
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
        {
            if (m_onUndoClicked)
            {
                m_onUndoClicked();
            }
            dismiss();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::TextAntialiasing, true);

        p.setOpacity(m_animProgress);

        const bool dark = property("darkTheme").toBool();
        const qreal x0 = 1.0;
        const qreal y0 = 1.0;
        const qreal w = width() - 2.0;
        const qreal h = height() - 2.0;
        const qreal r = h / 2.0;

        // 1. Frosted capsule background with hover brightening
        const QColor bg = dark ? (m_hovered ? QColor(42, 56, 78, 245) : QColor(30, 41, 59, 235))
                               : (m_hovered ? QColor(255, 255, 255, 255) : QColor(255, 255, 255, 242));
        const QColor border = dark ? (m_hovered ? QColor(71, 85, 105, 240) : QColor(51, 65, 85, 220))
                                   : (m_hovered ? QColor(203, 213, 225, 255) : QColor(226, 232, 240, 240));
        p.setPen(QPen(border, 1.0));
        p.setBrush(bg);
        p.drawRoundedRect(QRectF(x0, y0, w, h), r, r);

        // 2. Continuous perimeter countdown contour (analytical path with zero jaggies)
        const double ratio = std::clamp(double(m_remainingMs) / double(m_totalDurationMs), 0.0, 1.0);
        if (ratio > 0.001)
        {
            constexpr qreal pi = 3.14159265358979323846;
            const qreal cx = x0 + w / 2.0;
            const qreal L1 = w / 2.0 - r;
            const qreal L2 = pi * r;
            const qreal L3 = w - 2.0 * r;
            const qreal L4 = pi * r;
            const qreal Ltotal = 2.0 * (w - 2.0 * r) + 2.0 * pi * r;
            const qreal d = Ltotal * ratio;

            QPainterPath path;
            path.moveTo(cx, y0);

            if (d <= L1)
            {
                path.lineTo(cx + d, y0);
            }
            else
            {
                path.lineTo(x0 + w - r, y0);
                qreal rem = d - L1;
                if (rem <= L2)
                {
                    const qreal sweep = -(rem / L2) * 180.0;
                    path.arcTo(QRectF(x0 + w - 2.0 * r, y0, 2.0 * r, 2.0 * r), 90.0, sweep);
                }
                else
                {
                    path.arcTo(QRectF(x0 + w - 2.0 * r, y0, 2.0 * r, 2.0 * r), 90.0, -180.0);
                    rem -= L2;
                    if (rem <= L3)
                    {
                        path.lineTo(x0 + w - r - rem, y0 + h);
                    }
                    else
                    {
                        path.lineTo(x0 + r, y0 + h);
                        rem -= L3;
                        if (rem <= L4)
                        {
                            const qreal sweep = -(rem / L4) * 180.0;
                            path.arcTo(QRectF(x0, y0, 2.0 * r, 2.0 * r), 270.0, sweep);
                        }
                        else
                        {
                            path.arcTo(QRectF(x0, y0, 2.0 * r, 2.0 * r), 270.0, -180.0);
                            rem -= L4;
                            if (rem > 0.0)
                            {
                                path.lineTo(std::min(cx, x0 + r + rem), y0);
                            }
                            if (d >= Ltotal - 0.5)
                            {
                                path.closeSubpath();
                            }
                        }
                    }
                }
            }

            const QColor timerColor = dark ? (m_hovered ? QColor("#93c5fd") : QColor("#60a5fa"))
                                           : (m_hovered ? QColor("#1d4ed8") : QColor("#3b82f6"));
            QPen timerPen(timerColor, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            p.strokePath(path, timerPen);
        }

        // 3. "Undo" Text centered
        const QColor accent = dark ? (m_hovered ? QColor("#93c5fd") : QColor("#60a5fa"))
                                   : (m_hovered ? QColor("#1d4ed8") : QColor("#2563eb"));
        QFont font = p.font();
        font.setPixelSize(11);
        font.setWeight(QFont::DemiBold);
        p.setFont(font);
        p.setPen(accent);
        p.drawText(QRectF(0.0, 0.0, width(), height()), Qt::AlignCenter, "Undo");
    }

    void enterEvent(QEnterEvent *event) override
    {
        m_hovered = true;
        m_pausedRemainingMs = m_remainingMs;
        update();
        QWidget::enterEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        m_hovered = false;
        m_elapsedTimer.restart();
        m_totalDurationMs = m_pausedRemainingMs;
        update();
        QWidget::leaveEvent(event);
    }

private:
    void onTick()
    {
        if (!m_hovered && !m_isDismissing)
        {
            const qint64 elapsed = m_elapsedTimer.elapsed();
            m_remainingMs = std::max(0, m_totalDurationMs - int(elapsed));
            update();
            if (m_remainingMs <= 0)
            {
                dismiss();
            }
        }
    }

    void onAnimStep()
    {
        const qint64 elapsed = m_animElapsed.elapsed();
        const double duration = 180.0;
        const double t = std::clamp(double(elapsed) / duration, 0.0, 1.0);

        if (!m_isDismissing)
        {
            const double ease = 1.0 - std::pow(1.0 - t, 2.0);
            m_animProgress = ease;
            const int offsetY = int((1.0 - ease) * 20.0);
            move(m_targetPos.x(), m_targetPos.y() + offsetY);
            if (t >= 1.0)
            {
                m_animProgress = 1.0;
                move(m_targetPos);
                m_animTimer->stop();
            }
        }
        else
        {
            const double ease = t * t;
            m_animProgress = std::clamp(1.0 - ease, 0.0, 1.0);
            const int offsetY = int(ease * 20.0);
            move(m_targetPos.x(), m_targetPos.y() + offsetY);
            if (t >= 1.0)
            {
                m_animProgress = 0.0;
                m_animTimer->stop();
                m_tickTimer->stop();
                hide();
            }
        }
        update();
    }

    int m_totalDurationMs = 4000;
    int m_remainingMs = 4000;
    int m_pausedRemainingMs = 4000;
    bool m_hovered = false;
    bool m_isDismissing = false;
    double m_animProgress = 0.0;
    QPoint m_targetPos;
    QElapsedTimer m_elapsedTimer;
    QTimer *m_tickTimer = nullptr;
    QTimer *m_animTimer = nullptr;
    QElapsedTimer m_animElapsed;
    std::function<void()> m_onUndoClicked;
};

/**
 * @brief Update a cell without destroying selection or replacing its item.
 * @param widget Target table widget.
 * @param row Row index.
 * @param column Column index.
 * @param text Cell text value.
 * @return True if cell text was actually updated, false if identical.
 */
inline bool cell(QTableWidget *widget, int row, int column, const QString &text)
{
    auto *item = widget->item(row, column);
    if (!item)
    {
        item = new QTableWidgetItem;
        item->setFont(cachedFixedFont());
        widget->setItem(row, column, item);
    }
    if (item->text() != text)
    {
        item->setText(text);
        return true;
    }
    return false;
}

/** @brief Resolve a generated message name without fabricating unknown identities. */
inline QString messageName(uint32_t id)
{
    const auto *info = mavlink_get_message_info_by_id(id);
    return info ? QString::fromLatin1(info->name) : "Unknown dialect message";
}

