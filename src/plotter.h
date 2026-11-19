#ifndef MAVLINK_LAB_PLOTTER_H
#define MAVLINK_LAB_PLOTTER_H

#include "core.h"

#include <QWidget>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QColor>
#include <QString>
#include <QPoint>
#include <functional>
#include <vector>
#include <utility>

struct Snapshot;
class QCloseEvent;

/**
 * @brief Representation of an individual telemetry channel displayed on the real-time plotter.
 */
struct PlotChannel
{
    uint32_t sysid = 0;
    uint8_t compid = 0;
    uint32_t msgid = 0;
    QString fieldName;
    QString label;
    QColor color;
    bool visible = true;
    Mav::PlotBuffer buffer{2048};
    double latestValue = 0.0;
    bool hasValue = false;
};

/**
 * @brief Antialiased real-time strip-chart canvas rendering multiple telemetry waveforms.
 */
class PlotCanvasWidget : public QWidget
{
public:
    /**
     * @brief Construct the real-time strip-chart canvas.
     * @param parent Owning QWidget container.
     */
    explicit PlotCanvasWidget(QWidget *parent = nullptr);

    /**
     * @brief Set reference to the active channel list.
     * @param channels Pointer to active plot channels.
     */
    void setChannels(const std::vector<PlotChannel> *channels);

    /**
     * @brief Configure display time window in seconds (e.g. 10, 30, 60, 120).
     * @param seconds Window width in seconds.
     */
    void setTimeWindow(int seconds);

    /**
     * @brief Enable or disable vertical auto-scaling.
     * @param autoScale True to auto-scale Y bounds, false to lock current bounds.
     */
    void setAutoScale(bool autoScale);

    /**
     * @brief Freeze or resume real-time scrolling.
     * @param paused True to pause scrolling, false to resume real-time tracking.
     */
    void setPaused(bool paused);

    /**
     * @brief Refresh canvas theme colors.
     * @param dark True for dark theme, false for light theme.
     */
    void setDarkTheme(bool dark);

    /**
     * @brief Return the active time window in seconds.
     * @return Duration in seconds.
     */
    int timeWindow() const;

    /**
     * @brief Return whether auto-scale is active.
     * @return True if auto-scaling is enabled.
     */
    bool isAutoScale() const;

    /**
     * @brief Return whether plot rendering is paused.
     * @return True if canvas is frozen/paused.
     */
    bool isPaused() const;

    /**
     * @brief Return currently calculated or locked Y-axis value bounds.
     * @return Pair of {min, max} values.
     */
    std::pair<double, double> currentYRange() const;

protected:
    /**
     * @brief Track cursor position for inspection hairline.
     * @param event Mouse movement event.
     */
    void mouseMoveEvent(QMouseEvent *event) override;

    /**
     * @brief Clear hover inspection state when cursor leaves the canvas.
     * @param event Leave event.
     */
    void leaveEvent(QEvent *event) override;

    /**
     * @brief Render background grid, axes, channel curves, and inspection tooltip.
     * @param event Paint event.
     */
    void paintEvent(QPaintEvent *event) override;

private:
    const std::vector<PlotChannel> *channels_ = nullptr;
    int timeWindowSec_ = 30;
    bool autoScale_ = true;
    bool paused_ = false;
    bool dark_ = false;
    int64_t pauseTimeUs_ = 0;
    double cachedYMin_ = -1.0;
    double cachedYMax_ = 1.0;
    QPoint hoverPos_{-1, -1};
    bool isHovering_ = false;
};

/**
 * @brief Complete telemetry plotter container holding channel controls, indicators and drawing canvas.
 */
class TelemetryPlotWidget : public QFrame
{
public:
    std::function<void()> onClosed;
    std::function<void()> onChannelsChanged;
    std::function<void(bool floating)> onFloatingChanged;

    /**
     * @brief Construct the telemetry plotter toolbar, canvas, and control container.
     * @param parent Owning QWidget container.
     */
    explicit TelemetryPlotWidget(QWidget *parent = nullptr);

    /**
     * @brief Add a telemetry field subscription to the plotter.
     * @param sysid MAVLink System ID.
     * @param compid MAVLink Component ID.
     * @param msgid MAVLink Message ID.
     * @param fieldName Symbolic name of the numeric field.
     * @param label Display label for tags and legend.
     * @return True if channel was successfully added, false if duplicate or limit reached.
     */
    bool addChannel(uint32_t sysid, uint8_t compid, uint32_t msgid,
                    const QString &fieldName, const QString &label);

    /**
     * @brief Remove a telemetry field subscription by key.
     * @param sysid MAVLink System ID.
     * @param compid MAVLink Component ID.
     * @param msgid MAVLink Message ID.
     * @param fieldName Symbolic name of the field to remove.
     * @return True if channel was found and removed, false otherwise.
     */
    bool removeChannel(uint32_t sysid, uint8_t compid, uint32_t msgid,
                       const QString &fieldName);

    /**
     * @brief Remove channel at specified zero-based index.
     * @param index Index of channel to remove.
     */
    void removeChannelAt(size_t index);

    /**
     * @brief Check whether a specific channel key is already plotted.
     * @param sysid MAVLink System ID.
     * @param compid MAVLink Component ID.
     * @param msgid MAVLink Message ID.
     * @param fieldName Symbolic name of the field.
     * @return True if channel exists.
     */
    bool hasChannel(uint32_t sysid, uint8_t compid, uint32_t msgid,
                    const QString &fieldName) const;

    /**
     * @brief Return count of active plotted channels.
     * @return Channel count (0 to 6).
     */
    size_t channelCount() const;

    /**
     * @brief Return const reference to vector of channels.
     * @return Const reference to channels.
     */
    const std::vector<PlotChannel> &channels() const;

    /**
     * @brief Return mutable reference to vector of channels.
     * @return Mutable reference to channels.
     */
    std::vector<PlotChannel> &channels();

    /**
     * @brief Ingest a snapshot of received messages and parameters into active plot channels.
     * @param snapshot Immutable MAVLink receiver snapshot.
     * @param nowUs Optional custom timestamp in microseconds (0 to use steady_clock::now).
     */
    void processSnapshot(const Snapshot &snapshot, int64_t nowUs = 0);

    /**
     * @brief Update tags and refresh canvas after adding samples.
     */
    void updateCanvas();

    /**
     * @brief Clear data from all channel buffers.
     */
    void clearAllData();

    /**
     * @brief Apply active light or dark theme styling.
     * @param dark True for dark theme, false for light theme.
     */
    void setDarkTheme(bool dark);

    /**
     * @brief Export all channel timeseries samples into a CSV file.
     * @param targetPath Path to destination file (or empty to prompt file dialog).
     * @return True if export succeeded, false on failure or cancellation.
     */
    bool exportCsv(const QString &targetPath = QString()) const;

    /**
     * @brief Return pointer to the underlying canvas widget.
     * @return Pointer to PlotCanvasWidget.
     */
    PlotCanvasWidget *canvas() const;

    /**
     * @brief Return pointer to pause toggle button.
     * @return Pointer to pause button.
     */
    QPushButton *pauseButton() const;

    /**
     * @brief Return pointer to auto-scale toggle button.
     * @return Pointer to auto-scale button.
     */
    QPushButton *autoScaleButton() const;

    /**
     * @brief Return pointer to clear buffer button.
     * @return Pointer to clear button.
     */
    QPushButton *clearButton() const;

    /**
     * @brief Return pointer to CSV export button.
     * @return Pointer to export button.
     */
    QPushButton *exportButton() const;

    /**
     * @brief Configure whether the plotter is floating as an independent window or docked.
     * @param floating True to pop-out as standalone window, false to dock into parent container.
     */
    void setFloating(bool floating);

    /**
     * @brief Return whether the plotter is currently in floating window mode.
     * @return True if floating, false if docked.
     */
    bool isFloating() const;

    /**
     * @brief Return pointer to the pop-out / dock button.
     * @return Pointer to pop-out button.
     */
    QPushButton *popoutButton() const;

    /**
     * @brief Return pointer to close plotter button.
     * @return Pointer to close button.
     */
    QPushButton *closeButton() const;

    /**
     * @brief Return pointer to time range combo box.
     * @return Pointer to time range combo box.
     */
    QComboBox *timeRangeBox() const;

protected:
    /**
     * @brief Handle window close event when in floating mode, docking back and notifying owner.
     * @param event Close event.
     */
    void closeEvent(QCloseEvent *event) override;

private:
    void rebuildChannelTags();
    void updateChannelTagValues();
    QColor channelColor(size_t index) const;

    struct TagWidgetEntry
    {
        QFrame *frame = nullptr;
        QLabel *valueLabel = nullptr;
    };
    std::vector<TagWidgetEntry> tagWidgets_;
    std::vector<PlotChannel> channels_;
    PlotCanvasWidget *canvas_ = nullptr;
    QWidget *tagsContainer_ = nullptr;
    QHBoxLayout *tagsLayout_ = nullptr;
    QComboBox *timeRangeBox_ = nullptr;
    QPushButton *autoScaleBtn_ = nullptr;
    QPushButton *pauseBtn_ = nullptr;
    QPushButton *clearBtn_ = nullptr;
    QPushButton *exportBtn_ = nullptr;
    QPushButton *popoutBtn_ = nullptr;
    QPushButton *closeBtn_ = nullptr;
    bool dark_ = false;
    bool isFloating_ = false;
};

#endif // MAVLINK_LAB_PLOTTER_H
