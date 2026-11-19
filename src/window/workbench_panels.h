/**
 * @file workbench_panels.h
 * @brief Interactive specialized panel widgets: drag dropzone, undo toast, table/tree empty states declarations.
 */

#pragma once

#include <QLineEdit>
#include <QListWidgetItem>
#include <QLabel>
#include <QHeaderView>
#include <QTableWidget>
#include <QTreeWidget>
#include <QWidget>
#include <QTimer>
#include <QElapsedTimer>
#include <QPoint>
#include <functional>

class QPaintEvent;
class QMouseEvent;
class QEnterEvent;
class QShowEvent;
class QResizeEvent;
class QEvent;

/** @brief Draw a theme-aware search glyph while retaining native clear and keyboard actions. */
class CatalogSearch : public QLineEdit
{
public:
    explicit CatalogSearch(QWidget *parent = nullptr);
    void updateClearButtonIcon();

protected:
    void changeEvent(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void updatePlaceholderColor();
};


/** @brief Sort catalog matches by relevance, then by numeric message ID. */
class CatalogItem : public QListWidgetItem
{
public:
    using QListWidgetItem::QListWidgetItem;
    bool operator<(const QListWidgetItem &other) const override;
};


/** @brief Floating badge providing transient visual feedback when copying decoded MAVLink values. */
class CopyFeedbackBadge : public QLabel
{
public:
    explicit CopyFeedbackBadge(QWidget *parent = nullptr);
    void refreshTheme(bool dark);
};


/** @brief Keep a locked RX column layout fitted to the viewport. */
class RxHeader : public QHeaderView
{
public:
    explicit RxHeader(QWidget *parent);
    void resetColumns();

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    bool adjusting = false;
};


/**
 * @brief Table widget for TX cards with minimalist empty state rendering.
 */
class TxTableWidget : public QTableWidget
{
public:
    using QTableWidget::QTableWidget;

protected:
    void paintEvent(QPaintEvent *event) override;
};


/**
 * @brief Tree widget for RX telemetry streams with state-aware waiting state rendering.
 */
class RxTreeWidget : public QTreeWidget
{
public:
    using QTreeWidget::QTreeWidget;

    void setConnectionInfo(bool connected, const QString &portName, const QString &baudRate);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    bool connected_ = false;
    QString portName_;
    QString baudRate_;
};


/**
 * @brief Configure a read-only, single-row-selection dashboard table.
 * @param headers List of column header names.
 * @param isTx True if configuring the TX cards table.
 * @return Created QTableWidget pointer.
 */
QTableWidget *table(const QStringList &headers, bool isTx = false);


/**
 * @brief Dynamic drop-target card replacing the Delete All button during dragging,
 * offering animated expansion (28px -> 150px), dashed dropzone aesthetic, smooth
 * color lerping on hover, and smooth collapse on drop/cancel.
 */
class TrashDropCard : public QWidget
{
public:
    explicit TrashDropCard(QWidget *parent = nullptr);

    void startExpandFromLeft(const QPoint &originPos, int centerY, int targetLeft, int targetWidth);
    void startExpand(const QPoint &originPos);
    void startCollapse(std::function<void()> onFinished = nullptr);
    void setHovered(bool hovered);
    bool isHovered() const;
    int fullWidth() const;
    int fullHeight() const;
    double expandProgress() const;
    double hoverProgress() const;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void onAnimStep();

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
    explicit WorkbenchToast(QWidget *parent = nullptr);

    void setOnUndoClicked(std::function<void()> cb);
    void setTargetPosition(const QPoint &pos);
    void updateThemeStyle(bool dark);
    void showToast(const QString &text);
    void resetCountdown();
    void dismiss();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    void onTick();
    void onAnimStep();

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
