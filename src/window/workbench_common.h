/**
 * @file workbench_common.h
 * @brief Common helper functions, vector icons, math helpers, and font caching declarations.
 */

#pragma once

#include <QString>
#include <QStringList>
#include <QFont>
#include <QRect>
#include <QIcon>
#include <cstdint>

class QMenu;
class QPushButton;
class QBoxLayout;
class QTableWidget;
class QSerialPortInfo;

/**
 * @brief Check whether a serial port represents an actively connected hardware device.
 * @param info Serial port metadata.
 * @return True if the device is a plugged USB converter, active onboard UART, or genuine hardware port.
 */
bool isPluggedPort(const QSerialPortInfo &info);

/**
 * @brief Return a cached fixed-pitch system font to avoid repetitive font database queries.
 * @return Cached QFont configured for fixed-width numeric and telemetry display.
 */
const QFont &cachedFixedFont();

/**
 * @brief Provide human-readable enum descriptions for common MAVLink fields.
 * @param msgid MAVLink message identity.
 * @param field Field name string.
 * @param val Decimal string value.
 * @return Concise description or empty string.
 */
QString mavlinkEnumHint(uint32_t msgid, const QString &field, const QString &val);

/**
 * @brief Configure a popup menu with frameless window flags and translucent background for clean rounded corners.
 * @param menu The popup menu widget to configure.
 */
void configureTranslucentMenu(QMenu *menu);

/**
 * @brief Wrap a TX identity into at most two lines, eliding only the final overflow.
 * @param text TX identity title string.
 * @param font Base font used for metric calculation.
 * @param width Available bounding width.
 * @return List of lines (1 or 2 entries).
 */
QStringList txTitleLines(const QString &text, const QFont &font, int width);

/**
 * @brief Calculate the bounding rectangle of the drag handle on a TX card.
 * @param card Visual bounding rectangle of the card.
 * @return Bounding rectangle for mouse hit-testing and grip drawing.
 */
QRect txDragHandleRect(const QRect &card);

/**
 * @brief Map a visual row index to its corresponding source data row during drag-and-drop.
 * @param visualRow The visual row being rendered.
 * @param sourceRow The original row of the card currently being dragged.
 * @param targetRow The current target drop row index where placeholder should appear.
 * @return The source row index to read data from, or -1 if this visual row is the placeholder slot.
 */
int txVisualToSourceRow(int visualRow, int sourceRow, int targetRow);

/**
 * @brief Calculate the clickable target-rate pill rectangle on a TX card.
 * @param card Bounding rectangle of the card.
 * @param idStr Message ID string.
 * @param nameStr Message name string.
 * @param hzStr Rate in Hz string.
 * @param baseFont Base font for metric calculations.
 * @return Clickable pill bounding rectangle.
 */
QRect txRatePillRect(const QRect &card, const QString &idStr, const QString &nameStr, const QString &hzStr, const QFont &baseFont);

/**
 * @brief Render a crisp vector 'x' icon for search field clear buttons.
 * @param dark Whether dark theme palette is active.
 * @return Theme-aware QIcon with normal and active hover states.
 */
QIcon clearButtonIcon(bool dark);

/**
 * @brief Map receive age to a readable status without exposing a countdown.
 * @param age Elapsed age in seconds since last reception.
 * @return Status string ("Active", "Stale", or "Inactive").
 */
QString rxStatus(double age);

/**
 * @brief Create a labeled button with a stable automation identity.
 * @param text Button label text.
 * @param name Automation object name.
 * @param layout Target layout to add the button to.
 * @return Created QPushButton pointer.
 */
QPushButton *button(const QString &text, const char *name, QBoxLayout *layout);

/**
 * @brief Draw a small consistent line icon using local theme colors.
 * @param name Icon identifier symbol.
 * @param color Stroke and accent color.
 * @return Rendered QIcon.
 */
QIcon actionIcon(const QString &name, const QColor &color);

/**
 * @brief Update a cell without destroying selection or replacing its item.
 * @param widget Target table widget.
 * @param row Row index.
 * @param column Column index.
 * @param text Cell text value.
 * @return True if cell text was actually updated, false if identical.
 */
bool cell(QTableWidget *widget, int row, int column, const QString &text);

/**
 * @brief Resolve a generated message name without fabricating unknown identities.
 * @param id MAVLink message identity ID.
 * @return Resolved human-readable message name.
 */
QString messageName(uint32_t id);
