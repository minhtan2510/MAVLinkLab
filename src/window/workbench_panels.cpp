/**
 * @file workbench_panels.cpp
 * @brief Interactive specialized panel widgets: drag dropzone, undo toast, table/tree empty states implementation.
 */

#include "workbench_panels.h"
#include "workbench_common.h"
#include "workbench_controls.h"
#include "workbench_delegates.h"
#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QShowEvent>
#include <QResizeEvent>
#include <cmath>
#include <algorithm>
#include <array>

// --- CatalogSearch ---

/** @brief Reserve space for the search glyph and enable the standard clear action with vector 'x' icon. */
CatalogSearch::CatalogSearch(QWidget *parent)
    : QLineEdit(parent)
{
    setTextMargins(22, 0, 0, 0);
    setClearButtonEnabled(true);
    setAttribute(Qt::WA_InputMethodEnabled, false);
    setInputMethodHints(Qt::ImhLatinOnly | Qt::ImhNoPredictiveText);
    updatePlaceholderColor();
    updateClearButtonIcon();
}

/** @brief Apply custom vector 'x' icon to the embedded clear button. */
void CatalogSearch::updateClearButtonIcon()
{
    auto *clearBtn = findChild<QToolButton *>();
    if (clearBtn)
    {
        const bool dark = palette().color(QPalette::Window).lightness() < 128;
        clearBtn->setIcon(clearButtonIcon(dark));
    }
}

/** @brief Keep search hints and clear button subdued when the application theme changes. */
void CatalogSearch::changeEvent(QEvent *event)
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
void CatalogSearch::updatePlaceholderColor()
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
void CatalogSearch::paintEvent(QPaintEvent *event)
{
    QLineEdit::paintEvent(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(palette().color(QPalette::PlaceholderText), 1.4));
    const QPointF center(15, height() / 2.0 - 1);
    painter.drawEllipse(center, 4, 4);
    painter.drawLine(center + QPointF(3, 3), center + QPointF(7, 7));
}

// --- CatalogItem ---

/** @brief Keep exact and prefix matches ahead of substring matches without changing identity. */
bool CatalogItem::operator<(const QListWidgetItem &other) const
{
    const int rank = data(Qt::UserRole + 4).toInt();
    const int otherRank = other.data(Qt::UserRole + 4).toInt();
    return rank != otherRank ? rank < otherRank : data(Qt::UserRole).toUInt() < other.data(Qt::UserRole).toUInt();
}

// --- CopyFeedbackBadge ---

/** @brief Construct CopyFeedbackBadge widget. */
CopyFeedbackBadge::CopyFeedbackBadge(QWidget *parent)
    : QLabel(parent)
{
    setObjectName("copyFeedbackBadge");
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAlignment(Qt::AlignCenter);
    hide();
}

/** @brief Refresh badge styling according to the active theme palette. */
void CopyFeedbackBadge::refreshTheme(bool dark)
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

// --- RxHeader ---

/** @brief Disable user resizing and keep all width changes under layout control. */
RxHeader::RxHeader(QWidget *parent)
    : QHeaderView(Qt::Horizontal, parent)
{
    setSectionsMovable(false);
    setSectionsClickable(true);
    setSortIndicatorShown(false);
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
void RxHeader::resetColumns()
{
    if (adjusting || count() != 4 || !isVisible())
    {
        return;
    }
    adjusting = true;
    const int available = qMax(viewport()->width(), 4);
    const std::array<int, 4> minima{180, 75, 85, 80};
    std::array<int, 4> widths{};
    const int fixedTotal = minima[1] + minima[2] + minima[3];
    const int totalMinima = minima[0] + fixedTotal;
    if (available >= totalMinima)
    {
        widths[0] = qBound(minima[0], available / 2, available - fixedTotal);
        const int surplus = available - widths[0] - fixedTotal;
        for (int column = 1; column < 4; ++column)
        {
            widths[column] = minima[column] + surplus / 3 + (column <= surplus % 3 ? 1 : 0);
        }
    }
    else
    {
        int remaining = available;
        for (int column = 0; column < 3; ++column)
        {
            widths[column] = qMax(1, minima[column] * available / totalMinima);
            remaining -= widths[column];
        }
        widths[3] = remaining;
    }
    for (int column = 0; column < 4; ++column)
    {
        resizeSection(column, widths[column]);
    }
    adjusting = false;
}

/** @brief Fit the locked layout after visible geometry is available. */
void RxHeader::showEvent(QShowEvent *event)
{
    QHeaderView::showEvent(event);
    resetColumns();
}

/** @brief Keep the locked layout fitted when the viewport changes width. */
void RxHeader::resizeEvent(QResizeEvent *event)
{
    QHeaderView::resizeEvent(event);
    resetColumns();
}

// --- TxTableWidget ---

/**
 * @brief Paint table contents and render empty state message when no rows are configured.
 * @param event Paint event.
 */
void TxTableWidget::paintEvent(QPaintEvent *event)
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

// --- RxTreeWidget ---

/**
 * @brief Update connection parameters for the waiting state view.
 * @param connected Whether serial link is open.
 * @param portName Name of the connected serial port.
 * @param baudRate Current baud rate.
 */
void RxTreeWidget::setConnectionInfo(bool connected, const QString &portName, const QString &baudRate)
{
    if (connected_ != connected || portName_ != portName || baudRate_ != baudRate)
    {
        connected_ = connected;
        portName_ = portName;
        baudRate_ = baudRate;
        viewport()->update();
    }
}

/**
 * @brief Paint tree branch indicator with an antialiased vector chevron for top-level groups.
 * @param painter Active painter.
 * @param rect Branch rectangle.
 * @param index Model index of the row.
 */
void RxTreeWidget::drawBranches(QPainter *painter, const QRect &rect, const QModelIndex &index) const
{
    if (index.parent().isValid())
    {
        QTreeWidget::drawBranches(painter, rect, index);
        return;
    }

    const bool dark = property("darkTheme").toBool();
    const bool selected = selectionModel() && selectionModel()->isSelected(index);
    const QPoint cursorPos = viewport()->mapFromGlobal(QCursor::pos());
    const bool rowHovered = (index == indexAt(cursorPos));

    painter->save();
    if (selected)
    {
        painter->fillRect(rect, dark ? QColor("#1e293b") : QColor("#e0edff"));
    }
    else if (rowHovered)
    {
        painter->fillRect(rect, dark ? QColor("#1c212c") : QColor("#f8fafc"));
    }

    // Match the 1px bottom row divider from QTreeWidget#rxTree::item
    painter->setPen(QPen(dark ? QColor("#161b22") : QColor("#f8fafc"), 1.0));
    painter->drawLine(rect.bottomLeft(), rect.bottomRight());

    painter->setRenderHint(QPainter::Antialiasing, true);

    const double cx = rect.left() + rect.width() / 2.0 + 4.0;
    const double cy = rect.top() + rect.height() / 2.0;

    const QColor chevronColor = (selected || rowHovered)
        ? (dark ? QColor(226, 232, 240, 217) : QColor(30, 41, 59, 217))
        : (dark ? QColor(148, 163, 184, 140) : QColor(100, 116, 139, 140));
    painter->setPen(QPen(chevronColor, 1.25, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::NoBrush);

    QPainterPath chevron;
    if (isExpanded(index))
    {
        chevron.moveTo(cx - 3.5, cy - 1.75);
        chevron.lineTo(cx, cy + 2.25);
        chevron.lineTo(cx + 3.5, cy - 1.75);
    }
    else
    {
        chevron.moveTo(cx - 1.75, cy - 3.5);
        chevron.lineTo(cx + 2.25, cy);
        chevron.lineTo(cx - 1.75, cy + 3.5);
    }
    painter->drawPath(chevron);
    painter->restore();
}

/**
 * @brief Paint tree contents and render waiting state when no streams have been received.
 * @param event Paint event.
 */
void RxTreeWidget::paintEvent(QPaintEvent *event)
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

// --- table function ---

/**
 * @brief Configure a read-only, single-row-selection dashboard table.
 * @param headers List of column header names.
 * @param isTx True if configuring the TX cards table.
 * @return Created QTableWidget pointer.
 */
QTableWidget *table(const QStringList &headers, bool isTx)
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

// --- TrashDropCard ---

/**
 * @brief Construct the drop-target card with initial collapsed geometry and timer.
 * @param parent Owning parent widget.
 */
TrashDropCard::TrashDropCard(QWidget *parent)
    : QWidget(parent)
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
void TrashDropCard::startExpandFromLeft(const QPoint &originPos, int centerY, int targetLeft, int targetWidth)
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
void TrashDropCard::startExpand(const QPoint &originPos)
{
    startExpandFromLeft(originPos, originPos.y() + 14, originPos.x(), 190);
}

/**
 * @brief Start animated collapse back to 28x28 and invoke completion callback.
 * @param onFinished Optional callback invoked after collapse completes and widget hides.
 */
void TrashDropCard::startCollapse(std::function<void()> onFinished)
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
void TrashDropCard::setHovered(bool hovered)
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
bool TrashDropCard::isHovered() const
{
    return m_hovered;
}

/**
 * @brief Query the full expanded target width.
 * @return Full target width in pixels.
 */
int TrashDropCard::fullWidth() const
{
    return m_targetWidth;
}

/**
 * @brief Query the full expanded target height.
 * @return Full target height in pixels (32px).
 */
int TrashDropCard::fullHeight() const
{
    return 32;
}

/**
 * @brief Query current expansion progress (0.0 to 1.0).
 * @return Current progress value.
 */
double TrashDropCard::expandProgress() const
{
    return m_expandProgress;
}

/**
 * @brief Query current hover transition progress (0.0 to 1.0).
 * @return Current hover progress value.
 */
double TrashDropCard::hoverProgress() const
{
    return m_hoverProgress;
}

/**
 * @brief Render the drop card with frosted glass background, subtle top highlight, glow, and clipped text.
 * @param event Paint event parameters.
 */
void TrashDropCard::paintEvent(QPaintEvent *)
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

/**
 * @brief Advance width/height expansion/collapse and hover color interpolation.
 */
void TrashDropCard::onAnimStep()
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

// --- WorkbenchToast ---

/**
 * @brief Construct the floating undo capsule widget and animation timers.
 * @param parent Owning parent window.
 */
WorkbenchToast::WorkbenchToast(QWidget *parent)
    : QWidget(parent)
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
void WorkbenchToast::setOnUndoClicked(std::function<void()> cb)
{
    m_onUndoClicked = std::move(cb);
}

/**
 * @brief Set the target anchored position at the bottom-left of the window.
 * @param pos Top-left coordinate.
 */
void WorkbenchToast::setTargetPosition(const QPoint &pos)
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
void WorkbenchToast::updateThemeStyle(bool dark)
{
    setProperty("darkTheme", dark);
    update();
}

/**
 * @brief Display the toast with message and restart the 4s progress countdown.
 * @param text Notification message to display.
 */
void WorkbenchToast::showToast(const QString &text)
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

    move(m_targetPos.x(), m_targetPos.y() + 8);
    show();
    raise();
    update();
}

/**
 * @brief Reset the countdown timer back to full duration without re-animating widget entry.
 */
void WorkbenchToast::resetCountdown()
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
void WorkbenchToast::dismiss()
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

/** @brief Handle mouse click on toast capsule to trigger undo. */
void WorkbenchToast::mousePressEvent(QMouseEvent *event)
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

/** @brief Render countdown border and Undo label. */
void WorkbenchToast::paintEvent(QPaintEvent *event)
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

/** @brief Pause timer on hover entry. */
void WorkbenchToast::enterEvent(QEnterEvent *event)
{
    m_hovered = true;
    m_pausedRemainingMs = m_remainingMs;
    update();
    QWidget::enterEvent(event);
}

/** @brief Resume timer on hover leave. */
void WorkbenchToast::leaveEvent(QEvent *event)
{
    m_hovered = false;
    m_elapsedTimer.restart();
    m_totalDurationMs = m_pausedRemainingMs;
    update();
    QWidget::leaveEvent(event);
}

/** @brief Advance countdown tick. */
void WorkbenchToast::onTick()
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

/** @brief Advance sliding and fade animation steps. */
void WorkbenchToast::onAnimStep()
{
    const qint64 elapsed = m_animElapsed.elapsed();
    const double duration = 180.0;
    const double t = std::clamp(double(elapsed) / duration, 0.0, 1.0);

    if (!m_isDismissing)
    {
        const double ease = 1.0 - std::pow(1.0 - t, 2.0);
        m_animProgress = ease;
        const int offsetY = int((1.0 - ease) * 8.0);
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
        const int offsetY = int(ease * 8.0);
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
