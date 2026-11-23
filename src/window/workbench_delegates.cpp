/**
 * @file workbench_delegates.cpp
 * @brief Item view delegates for TX cards, decoded fields, message catalog, and RX tree implementation.
 */

#include "workbench_delegates.h"
#include "workbench_common.h"
#include "workbench_controls.h"
#include "window.h"
#include "mavlink_enums.h"
#include "core.h"
#include <ardupilotmega/mavlink.h>
#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QKeyEvent>
#include <QCompleter>
#include <QToolTip>
#include <cmath>
#include <algorithm>

// --- TxCardDelegate ---

/** @brief Paint state-specific cards with a compact target-rate badge and distinct actions. */
void TxCardDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    const bool dark = option.widget ? option.widget->property("darkTheme").toBool() : false;
    const QColor panelBg(dark ? "#161b22" : "#ffffff");
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
    const bool isDeletingThis = (delRow >= 0 && index.row() == delRow);

    // Subtle 3D Floating Elevation Shadow (Ambient + Drop + Contact edge layers)
    if (!isDeletingThis)
    {
        painter->setPen(Qt::NoPen);
        const int shadowSpread = hovered ? 3 : 2;

        // Ambient soft shadow
        const QColor ambientShadow = dark ? QColor(0, 0, 0, hovered ? 38 : 30) :
                                            QColor(15, 23, 42, hovered ? 14 : 10);
        painter->setBrush(ambientShadow);
        painter->drawRoundedRect(card.adjusted(0, 1, 0, shadowSpread + 1), 8, 8);

        // Direct downward drop shadow
        const QColor dropShadow = dark ? QColor(0, 0, 0, hovered ? 60 : 45) :
                                         QColor(15, 23, 42, hovered ? 24 : 18);
        painter->setBrush(dropShadow);
        painter->drawRoundedRect(card.adjusted(0, 1, 0, shadowSpread), 8, 8);

        // Contact edge shadow directly under bottom edge
        const QColor contactShadow = dark ? QColor(0, 0, 0, hovered ? 85 : 65) :
                                            QColor(15, 23, 42, hovered ? 34 : 26);
        painter->setBrush(contactShadow);
        painter->drawRoundedRect(card.adjusted(1, 1, -1, hovered ? 2 : 1), 8, 8);
    }

    const QColor stateAccent = error ? QColor(dark ? "#ef8799" : "#bd405b") :
                               running ? QColor(dark ? "#57c99a" : "#249569") : QColor(dark ? "#3d506e" : "#b8c8dc");

    // Subtle 3D Surface Gradient (Soft top illumination to bottom depth)
    QLinearGradient surfaceGrad(card.topLeft(), card.bottomLeft());
    if (dark)
    {
        if (hovered)
        {
            surfaceGrad.setColorAt(0.0, QColor("#29364e"));
            surfaceGrad.setColorAt(1.0, QColor("#202a3c"));
        }
        else
        {
            surfaceGrad.setColorAt(0.0, QColor("#242f44"));
            surfaceGrad.setColorAt(1.0, QColor("#1c2435"));
        }
    }
    else
    {
        if (hovered)
        {
            surfaceGrad.setColorAt(0.0, QColor("#ffffff"));
            surfaceGrad.setColorAt(1.0, QColor("#f1f5f9"));
        }
        else
        {
            surfaceGrad.setColorAt(0.0, QColor("#ffffff"));
            surfaceGrad.setColorAt(1.0, QColor("#f8fafc"));
        }
    }

    const QColor cardBorder = (selected && !isDeletingThis) ? QColor("#60a5fa") :
                              (hovered ? QColor(dark ? "#3d4e6d" : "#cbd5e1") : QColor(dark ? "#2d3a52" : "#e2e8f0"));
    painter->setPen(QPen(cardBorder, (selected && !isDeletingThis) ? 1.5 : 1.2));
    painter->setBrush(surfaceGrad);
    painter->drawRoundedRect(card, 8, 8);

    // Top Rim Light (Soft specular inset highlight on the upper curved rim)
    if (!selected && !isDeletingThis)
    {
        painter->save();
        painter->setClipRect(QRect(card.left(), card.top(), card.width(), 8), Qt::IntersectClip);
        const QColor rimColor = dark ? QColor(255, 255, 255, hovered ? 28 : 18) :
                                       QColor(255, 255, 255, 120);
        painter->setPen(QPen(rimColor, 1.0));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(card.adjusted(1, 1, -1, -1), 7, 7);
        painter->restore();
    }

    // Refined slim 3px state capsule pill inset along left edge
    const QRectF accentPill(card.left() + 8, card.top() + 10, 3, card.height() - 20);
    painter->setPen(Qt::NoPen);
    painter->setBrush(stateAccent);
    painter->drawRoundedRect(accentPill, 1.5, 1.5);

    // Status badge button on top right
    const QRect badge(card.right() - 88, card.top() + 9, 80, 22);
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

    // Line 1: Title -> Target Rate pill -> Payload badge -> [gap] -> Status badge
    const int textLeft = card.left() + 24;

    // Calculate pill metrics
    const uint32_t msgId = value(0).toUInt();
    const auto *metadata = mavlink_get_msg_entry(msgId);
    const uint8_t payloadLen = metadata ? metadata->max_msg_len : 0;
    const QString payloadText = QString("%1 B").arg(payloadLen);
    QFont payloadFont = option.font;
    payloadFont.setPixelSize(11);
    payloadFont.setWeight(QFont::Medium);
    const int payloadWidth = QFontMetrics(payloadFont).horizontalAdvance(payloadText) + 14;

    const QString rateText = value(3) + " Hz";
    QFont pillFont = option.font;
    pillFont.setPixelSize(11);
    pillFont.setWeight(QFont::Medium);
    const int pillWidth = QFontMetrics(pillFont).horizontalAdvance(rateText) + 22;

    const int requiredPillsWidth = 8 + pillWidth + 8 + payloadWidth;
    const int maxTitleWidth = std::max(20, badge.left() - 8 - requiredPillsWidth - textLeft);

    // Title text: strictly single line, elided if necessary
    QFont titleFont = option.font;
    titleFont.setPixelSize(13);
    titleFont.setWeight(QFont::Normal);
    painter->setFont(titleFont);
    painter->setPen(foreground);

    const QString title = QString("[%1] %2").arg(value(0), value(1));
    const int titleAdvance = QFontMetrics(titleFont).horizontalAdvance(title);

    QString displayTitle = title;
    int actualTitleAdvance = titleAdvance;
    if (titleAdvance > maxTitleWidth)
    {
        displayTitle = QFontMetrics(titleFont).elidedText(title, Qt::ElideRight, maxTitleWidth);
        actualTitleAdvance = QFontMetrics(titleFont).horizontalAdvance(displayTitle);
    }
    painter->drawText(QRect(textLeft, badge.top(), actualTitleAdvance, badge.height()),
                      Qt::AlignVCenter | Qt::AlignLeft, displayTitle);

    // Draw compact interactive Target Hz pill with dropdown chevron (adjacent to title)
    const QRect pillRect = txRatePillRect(card, value(0), value(1), value(3), option.font);
    if (pillRect.isValid())
    {
        painter->setPen(QPen(QColor(dark ? "#384a65" : "#cbd7e8"), 1));
        painter->setBrush(QColor(dark ? "#233144" : "#e6effa"));
        painter->drawRoundedRect(pillRect, 5, 5);
        painter->setFont(pillFont);
        painter->setPen(QColor(dark ? "#b0cbef" : "#2b5687"));
        painter->drawText(pillRect.adjusted(7, 0, -12, 0), Qt::AlignVCenter | Qt::AlignLeft, rateText);

        // Draw crisp vector down-chevron indicating dropdown switcher
        painter->setPen(QPen(QColor(dark ? "#7a90b5" : "#5b7da8"), 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const int cx = pillRect.right() - 8;
        const int cy = pillRect.center().y();
        painter->drawLine(QPoint(cx - 3, cy - 1), QPoint(cx, cy + 2));
        painter->drawLine(QPoint(cx, cy + 2), QPoint(cx + 3, cy - 1));
    }

    // Draw schema payload wire length badge (adjacent to Target Rate pill)
    const int payloadLeft = pillRect.isValid() ? (pillRect.right() + 8) : (textLeft + actualTitleAdvance + 8);
    if (payloadLeft + payloadWidth <= badge.left() - 8)
    {
        const QRect payloadRect(payloadLeft, badge.top(), payloadWidth, 22);
        painter->setPen(QPen(QColor(dark ? "#384a65" : "#cbd7e8"), 1));
        painter->setBrush(QColor(dark ? "#1a2536" : "#f1f5f9"));
        painter->drawRoundedRect(payloadRect, 5, 5);
        painter->setFont(payloadFont);
        painter->setPen(QColor(dark ? "#8ca0be" : "#475569"));
        painter->drawText(payloadRect, Qt::AlignCenter, payloadText);
    }

    // Line 2: Compact metrics row (Sys · Comp · Actual · Frames)
    const int line2Y = card.top() + 36;
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

/**
 * @brief Display full message title in tooltip when the card title is elided.
 * @param event Help event containing mouse coordinates and trigger type.
 * @param view Owning abstract item view.
 * @param option Style options for the target item.
 * @param index Model index being hovered.
 * @return True if tooltip was handled/shown, false otherwise.
 */
bool TxCardDelegate::helpEvent(QHelpEvent *event, QAbstractItemView *view,
                               const QStyleOptionViewItem &option, const QModelIndex &index)
{
    if (event && event->type() == QEvent::ToolTip && index.isValid())
    {
        const QString idStr = index.sibling(index.row(), 0).data().toString();
        const QString nameStr = index.sibling(index.row(), 1).data().toString();
        const QString hzStr = index.sibling(index.row(), 3).data().toString();
        const QString fullTitle = QString("[%1] %2").arg(idStr, nameStr);

        const QRect card = MainWindow::txCardRect(option.rect);
        const int textLeft = card.left() + 24;
        const QRect badge(card.right() - 88, card.top() + 9, 80, 22);

        const uint32_t msgId = idStr.toUInt();
        const auto *metadata = mavlink_get_msg_entry(msgId);
        const uint8_t payloadLen = metadata ? metadata->max_msg_len : 0;
        const QString payloadText = QString("%1 B").arg(payloadLen);
        QFont payloadFont = option.font;
        payloadFont.setPixelSize(11);
        payloadFont.setWeight(QFont::Medium);
        const int payloadWidth = QFontMetrics(payloadFont).horizontalAdvance(payloadText) + 14;

        const QString rateText = hzStr + " Hz";
        QFont pillFont = option.font;
        pillFont.setPixelSize(11);
        pillFont.setWeight(QFont::Medium);
        const int pillWidth = QFontMetrics(pillFont).horizontalAdvance(rateText) + 22;

        const int requiredPillsWidth = 8 + pillWidth + 8 + payloadWidth;
        const int maxTitleWidth = std::max(20, badge.left() - 8 - requiredPillsWidth - textLeft);

        QFont titleFont = option.font;
        titleFont.setPixelSize(13);
        titleFont.setWeight(QFont::Normal);

        if (QFontMetrics(titleFont).horizontalAdvance(fullTitle) > maxTitleWidth)
        {
            QToolTip::showText(event->globalPos(), fullTitle, view, card);
            return true;
        }
        QToolTip::hideText();
    }
    return QStyledItemDelegate::helpEvent(event, view, option, index);
}

// --- CatalogDelegate ---

/** @brief Draw two text columns with independent metadata and selection emphasis. */
void CatalogDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
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
QSize CatalogDelegate::sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const
{
    return QSize(180, 30);
}

// --- PayloadIdentityDelegate ---

/** @brief Keep parameter names readable and type annotations visually secondary. */
void PayloadIdentityDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
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

// --- ValueDelegate ---

/** @brief Keep editable payload values and decoded view-only cells visible and cleanly styled. */
void ValueDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
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
void ValueDelegate::updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option,
                                         const QModelIndex &index) const
{
    if (option.widget && option.widget->objectName() == "payloadFields")
    {
        editor->setGeometry(QRect(option.rect.left() + 8, option.rect.top() + 4, option.rect.width() - 16, 32));
        return;
    }
    QStyledItemDelegate::updateEditorGeometry(editor, option, index);
}

/** @brief Forward return/enter key from combo line edit to commit the active editor. */
bool ValueDelegate::eventFilter(QObject *object, QEvent *event)
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
QWidget *ValueDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                                     const QModelIndex &index) const
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
void ValueDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
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
void ValueDelegate::setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const
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

/** @brief Emphasize values while leaving native selection, focus and editing intact. */
void ValueDelegate::initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const
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

// --- RxStatusIcon ---

/** @brief Retain the theme-aware dot color. */
RxStatusIcon::RxStatusIcon(const QColor &color)
    : color(color)
{
}

/** @brief Clone the icon engine for Qt icon ownership. */
QIconEngine *RxStatusIcon::clone() const
{
    return new RxStatusIcon(color);
}

/** @brief Draw a small antialiased dot at the requested icon size. */
void RxStatusIcon::paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State)
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

// --- RxItemDelegate ---

/**
 * @brief Paint cells while suppressing dotted focus outlines and rendering badges for payload and rate.
 * @param painter Active painter.
 * @param option Style options.
 * @param index Model index.
 */
void RxItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QStyleOptionViewItem opt(option);
    opt.state &= ~QStyle::State_HasFocus;

    const bool isChild = index.parent().isValid();
    if (isChild && (index.column() == 1 || index.column() == 2))
    {
        opt.text.clear();
        QStyledItemDelegate::paint(painter, opt, index);

        const bool dark = option.palette.color(QPalette::Window).lightness() < 128;
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        const QRect rect = option.rect;
        const int pillHeight = 20;
        const int pillY = rect.top() + (rect.height() - pillHeight) / 2;

        if (index.column() == 1)
        {
            const QString text = index.data(Qt::DisplayRole).toString();
            QFont font = cachedFixedFont();
            font.setPixelSize(11);
            font.setWeight(QFont::Medium);
            painter->setFont(font);

            const QFontMetrics fm(font);
            const int textWidth = fm.horizontalAdvance(text);
            const int pillWidth = textWidth + 14;
            const int pillX = rect.left() + (rect.width() - pillWidth) / 2;
            const QRectF pillRect(pillX, pillY, pillWidth, pillHeight);

            const QColor bg = dark ? QColor(255, 255, 255, 12) : QColor(0, 0, 0, 8);
            const QColor border = dark ? QColor(255, 255, 255, 24) : QColor(0, 0, 0, 18);
            const QColor textCol = dark ? QColor("#94a3b8") : QColor("#64748b");

            painter->setPen(QPen(border, 1.0));
            painter->setBrush(bg);
            painter->drawRoundedRect(pillRect, 4.5, 4.5);

            painter->setPen(textCol);
            painter->drawText(pillRect, Qt::AlignCenter, text);
        }
        else if (index.column() == 2)
        {
            const double hz = index.data(Qt::UserRole).toDouble();
            const QString text = index.data(Qt::DisplayRole).toString() + " Hz";
            QFont font = cachedFixedFont();
            font.setPixelSize(11);
            font.setWeight(QFont::Medium);
            painter->setFont(font);

            const QFontMetrics fm(font);
            const int textWidth = fm.horizontalAdvance(text);
            const int pillWidth = textWidth + 14;
            const int pillX = rect.left() + (rect.width() - pillWidth) / 2;
            const QRectF pillRect(pillX, pillY, pillWidth, pillHeight);

            QColor bg;
            QColor border;
            QColor textCol;
            if (hz > 0.0)
            {
                bg = dark ? QColor(52, 211, 153, 24) : QColor(21, 128, 61, 20);
                border = dark ? QColor(52, 211, 153, 65) : QColor(21, 128, 61, 50);
                textCol = dark ? QColor("#34d399") : QColor("#15803d");
            }
            else
            {
                bg = dark ? QColor(255, 255, 255, 8) : QColor(0, 0, 0, 6);
                border = dark ? QColor(255, 255, 255, 18) : QColor(0, 0, 0, 14);
                textCol = dark ? QColor("#64748b") : QColor("#94a3b8");
            }

            painter->setPen(QPen(border, 1.0));
            painter->setBrush(bg);
            painter->drawRoundedRect(pillRect, 4.5, 4.5);

            painter->setPen(textCol);
            painter->drawText(pillRect, Qt::AlignCenter, text);
        }

        painter->restore();
        return;
    }

    QStyledItemDelegate::paint(painter, opt, index);
}

/** @brief Decorate Message with a theme-aware status dot while retaining native text layout. */
void RxItemDelegate::initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const
{
    QStyledItemDelegate::initStyleOption(option, index);
    if (index.parent().isValid() && (index.column() == 1 || index.column() == 2))
    {
        option->text.clear();
        return;
    }
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
