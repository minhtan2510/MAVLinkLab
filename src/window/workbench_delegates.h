/**
 * @file workbench_delegates.h
 * @brief Item view delegates for TX cards, decoded fields, message catalog, and RX tree declarations.
 */

#pragma once

#include <QStyledItemDelegate>
#include <QIconEngine>
#include <QColor>
#include <QHelpEvent>

/** @brief Present each TX entry as a readable card backed by the existing row model. */
class TxCardDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    /** @brief Paint state-specific cards with a compact target-rate badge and distinct actions. */
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    /** @brief Display full message title in tooltip when the card title is elided. */
    bool helpEvent(QHelpEvent *event, QAbstractItemView *view,
                   const QStyleOptionViewItem &option, const QModelIndex &index) override;
};


/** @brief Align catalog IDs and names on a soft selected surface in either theme. */
class CatalogDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    /** @brief Draw two text columns with independent metadata and selection emphasis. */
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    /** @brief Provide compact desktop rows with a stable keyboard target. */
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};


/** @brief Paint payload identities with quieter type metadata and consistent row separators. */
class PayloadIdentityDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    /** @brief Keep parameter names readable and type annotations visually secondary. */
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};


/** @brief Give decoded and editable values a distinct visual column without changing data. */
class ValueDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    /** @brief Keep editable payload values and decoded view-only cells visible and cleanly styled. */
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    /** @brief Align the native payload editor with the painted input surface. */
    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option,
                              const QModelIndex &index) const override;

    /** @brief Forward return/enter key from combo line edit to commit the active editor. */
    bool eventFilter(QObject *object, QEvent *event) override;

    /** @brief Create a standard text editor or editable dropdown for enum fields. */
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override;

    /** @brief Prepopulate the editor with the current table value. */
    void setEditorData(QWidget *editor, const QModelIndex &index) const override;

    /** @brief Write the chosen or typed value back to the payload model. */
    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override;

protected:
    /** @brief Emphasize values while leaving native selection, focus and editing intact. */
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override;
};


/** @brief Paint a status dot as a scalable vector decoration. */
class RxStatusIcon : public QIconEngine
{
public:
    /** @brief Retain the theme-aware dot color. */
    explicit RxStatusIcon(const QColor &color);

    /** @brief Clone the icon engine for Qt icon ownership. */
    QIconEngine *clone() const override;

    /** @brief Draw a small antialiased dot at the requested icon size. */
    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State state) override;

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
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;

protected:
    /** @brief Decorate Message with a theme-aware status dot while retaining native text layout. */
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override;
};
