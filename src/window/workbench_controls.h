/**
 * @file workbench_controls.h
 * @brief Custom theme-aware form controls, menus, dropdowns, spinboxes, and inputs.
 */

#pragma once

#include "workbench_common.h"

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

