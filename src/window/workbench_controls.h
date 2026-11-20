/**
 * @file workbench_controls.h
 * @brief Custom theme-aware form controls, menus, dropdowns, spinboxes, and inputs declarations.
 */

#pragma once

#include <QMenu>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QStyledItemDelegate>
#include <QStyleOptionSpinBox>
#include <QPainter>

class WorkbenchComboBox;

/**
 * @brief Custom menu supporting decoupled, right-aligned keycap pill badges for keyboard shortcuts.
 */
class WorkbenchMenu : public QMenu
{
public:
    using QMenu::QMenu;

protected:
    void initStyleOption(QStyleOptionMenuItem *option, const QAction *action) const override;
    void paintEvent(QPaintEvent *event) override;
};


/**
 * @brief Custom delegate for combo popup items rendering selection highlight, hover, and vector checkmark.
 */
class WorkbenchComboDelegate : public QStyledItemDelegate
{
public:
    explicit WorkbenchComboDelegate(WorkbenchComboBox *combo);

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    bool helpEvent(QHelpEvent *event, QAbstractItemView *view,
                   const QStyleOptionViewItem &option, const QModelIndex &index) override;

private:
    WorkbenchComboBox *m_combo = nullptr;
};


/** @brief Keep native combo interaction with a theme-aware vector chevron and persistent item highlight. */
class WorkbenchComboBox : public QComboBox
{
public:
    explicit WorkbenchComboBox(QWidget *parent = nullptr);

    int activeIndex() const;
    void showPopup() override;
    void hidePopup() override;
    bool eventFilter(QObject *watched, QEvent *event) override;

protected:
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
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

using WorkbenchDoubleSpinBox = WorkbenchSpinBox<QDoubleSpinBox>;


/** @brief Custom theme-aware checkbox with a crisp vector checkmark and focus indicator. */
class WorkbenchCheckBox : public QCheckBox
{
public:
    explicit WorkbenchCheckBox(const QString &text, QWidget *parent = nullptr);
    explicit WorkbenchCheckBox(QWidget *parent = nullptr);

    QSize sizeHint() const override;

protected:
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
};


/** @brief Line edit that maintains a theme-aware dimmed placeholder text color. */
class WorkbenchLineEdit : public QLineEdit
{
public:
    explicit WorkbenchLineEdit(QWidget *parent = nullptr);
    explicit WorkbenchLineEdit(const QString &contents, QWidget *parent = nullptr);

protected:
    void showEvent(QShowEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void updatePlaceholderColor();
};
