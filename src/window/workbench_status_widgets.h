/**
 * @file workbench_status_widgets.h
 * @brief Dynamic status indicators, connection state badges, and metrics chips declarations.
 */

#pragma once

#include <QWidget>
#include <QLabel>
#include <QString>
#include <cstdint>

/**
 * @brief Dynamic visual status indicator for configured and active TX messages.
 * Renders dual rounded pill badges: total messages count and live running state.
 */
class TxSummaryWidget : public QWidget
{
public:
    explicit TxSummaryWidget(QWidget *parent = nullptr);

    void setStats(int total, int running, double estimatedBps = 0.0, double utilization = 0.0);
    int total() const;
    int running() const;
    QString text() const;
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

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

    explicit ConnectionStatusWidget(QWidget *parent = nullptr);

    void setStatus(const QString &state, bool open);
    void setState(State state);
    State state() const;
    QString text() const;
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void updateTooltip();
    State m_state = State::Disconnected;
};


/**
 * @brief Modern status bar widget displaying segmented metric chips.
 * Visualizes RX/TX throughput, CRC health, queue backpressure, and overflow.
 */
class StatusBarMetricsWidget : public QWidget
{
public:
    explicit StatusBarMetricsWidget(QWidget *parent = nullptr);

    void updateMetrics(double rxBps, double txBps, uint64_t crc, uint64_t pending, uint64_t overflow, bool dark,
                       double rxUtil = 0.0, double txUtil = 0.0, int baud = 0);
    void setRateUnit(int unit);
    static QString formatRate(double bps, int unit);
    void setDarkTheme(bool dark);

private:
    QLabel *createChip(const QString &text, const QString &tooltip);
    void applyStyles();

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
    explicit StatusBarMessageWidget(const QString &text = "Ready", QWidget *parent = nullptr);

    void setText(const QString &text);
    void setDarkTheme(bool dark);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    bool m_dark = false;
};
