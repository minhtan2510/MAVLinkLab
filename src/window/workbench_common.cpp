/**
 * @file workbench_common.cpp
 * @brief Workbench common helpers compilation unit.
 */

#include "workbench_common.h"
#include "mavlink_enums.h"
#include <ardupilotmega/mavlink.h>
#include <QApplication>
#include <QMenu>
#include <QPushButton>
#include <QBoxLayout>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QSerialPortInfo>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QTextLayout>
#include <algorithm>
#include <limits>

/**
 * @brief Check whether a serial port represents an actively connected hardware device.
 * @param info Serial port metadata.
 * @return True if the device is a plugged USB converter, active onboard UART, or genuine hardware port.
 */
bool isPluggedPort(const QSerialPortInfo &info)
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
const QFont &cachedFixedFont()
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
QString mavlinkEnumHint(uint32_t msgid, const QString &field, const QString &val)
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
void configureTranslucentMenu(QMenu *menu)
{
    if (!menu)
    {
        return;
    }
    menu->setWindowFlags(menu->windowFlags() | Qt::FramelessWindowHint);
    menu->setAttribute(Qt::WA_TranslucentBackground, true);
}

/**
 * @brief Wrap a TX identity into at most two lines, eliding only the final overflow.
 * @param text TX identity title string.
 * @param font Base font used for metric calculation.
 * @param width Available bounding width.
 * @return List of lines (1 or 2 entries).
 */
QStringList txTitleLines(const QString &text, const QFont &font, int width)
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
QRect txDragHandleRect(const QRect &card)
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
int txVisualToSourceRow(int visualRow, int sourceRow, int targetRow)
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

/**
 * @brief Calculate the clickable target-rate pill rectangle on a TX card.
 * @param card Bounding rectangle of the card.
 * @param idStr Message ID string.
 * @param nameStr Message name string.
 * @param hzStr Rate in Hz string.
 * @param baseFont Base font for metric calculations.
 * @return Clickable pill bounding rectangle.
 */
QRect txRatePillRect(const QRect &card, const QString &idStr, const QString &nameStr, const QString &hzStr, const QFont &baseFont)
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

/**
 * @brief Render a crisp vector 'x' icon for search field clear buttons.
 * @param dark Whether dark theme palette is active.
 * @return Theme-aware QIcon with normal and active hover states.
 */
QIcon clearButtonIcon(bool dark)
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

/**
 * @brief Map receive age to a readable status without exposing a countdown.
 * @param age Elapsed age in seconds since last reception.
 * @return Status string ("Active", "Stale", or "Inactive").
 */
QString rxStatus(double age)
{
    return age <= 1.2 ? QString("Active") : age <= 3.0 ? QString("Stale") : QString("Inactive");
}

/**
 * @brief Create a labeled button with a stable automation identity.
 * @param text Button label text.
 * @param name Automation object name.
 * @param layout Target layout to add the button to.
 * @return Created QPushButton pointer.
 */
QPushButton *button(const QString &text, const char *name, QBoxLayout *layout)
{
    auto *control = new QPushButton(text);
    control->setObjectName(name);
    layout->addWidget(control);
    return control;
}

/**
 * @brief Draw a small consistent line icon using local theme colors.
 * @param name Icon identifier symbol.
 * @param color Stroke and accent color.
 * @return Rendered QIcon.
 */
QIcon actionIcon(const QString &name, const QColor &color)
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
    else if (name == "pauseDisplay")
    {
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawRoundedRect(QRectF(3.8, 3.5, 2.7, 9.0), 1.0, 1.0);
        painter.drawRoundedRect(QRectF(9.5, 3.5, 2.7, 9.0), 1.0, 1.0);
    }
    else if (name == "resumeDisplay")
    {
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        QPainterPath path;
        path.moveTo(4.4, 3.1);
        path.lineTo(12.6, 8.0);
        path.lineTo(4.4, 12.9);
        path.closeSubpath();
        painter.drawPath(path);
    }
    else if (name == "clearStatistics")
    {
        painter.setPen(QPen(color, 1.45, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        QPainterPath tag;
        tag.moveTo(1.5, 8.0);
        tag.lineTo(4.7, 3.1);
        tag.lineTo(14.1, 3.1);
        tag.lineTo(14.1, 12.9);
        tag.lineTo(4.7, 12.9);
        tag.closeSubpath();
        painter.drawPath(tag);

        painter.drawLine(QPointF(6.7, 6.1), QPointF(10.5, 9.9));
        painter.drawLine(QPointF(10.5, 6.1), QPointF(6.7, 9.9));
    }
    return QIcon(pixmap);
}

/**
 * @brief Update a cell without destroying selection or replacing its item.
 * @param widget Target table widget.
 * @param row Row index.
 * @param column Column index.
 * @param text Cell text value.
 * @return True if cell text was actually updated, false if identical.
 */
bool cell(QTableWidget *widget, int row, int column, const QString &text)
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

/**
 * @brief Resolve a generated message name without fabricating unknown identities.
 * @param id MAVLink message identity ID.
 * @return Resolved human-readable message name.
 */
QString messageName(uint32_t id)
{
    const auto *info = mavlink_get_message_info_by_id(id);
    return info ? QString::fromLatin1(info->name) : "Unknown dialect message";
}
