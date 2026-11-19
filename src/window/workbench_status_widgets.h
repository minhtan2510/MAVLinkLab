/**
 * @file workbench_status_widgets.h
 * @brief Dynamic status indicators, connection state badges, and metrics chips.
 */

#pragma once

#include "workbench_common.h"

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

