#include "plotter.h"
#include "worker.h"

#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QFontDatabase>
#include <cmath>
#include <algorithm>
#include <limits>
#include <chrono>
#include <array>
#include <map>

#include <ardupilotmega/mavlink.h>

/**
 * @brief Construct the real-time strip-chart canvas.
 * @param parent Owning QWidget container.
 */
PlotCanvasWidget::PlotCanvasWidget(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("plotCanvas");
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setMouseTracking(true);
    setMinimumSize(200, 120);
}

/**
 * @brief Set reference to the active channel list.
 * @param channels Pointer to active plot channels.
 */
void PlotCanvasWidget::setChannels(const std::vector<PlotChannel> *channels)
{
    channels_ = channels;
    update();
}

/**
 * @brief Configure display time window in seconds (e.g. 10, 30, 60, 120).
 * @param seconds Window width in seconds.
 */
void PlotCanvasWidget::setTimeWindow(int seconds)
{
    timeWindowSec_ = seconds > 0 ? seconds : 30;
    update();
}

/**
 * @brief Enable or disable vertical auto-scaling.
 * @param autoScale True to auto-scale Y bounds, false to lock current bounds.
 */
void PlotCanvasWidget::setAutoScale(bool autoScale)
{
    autoScale_ = autoScale;
    update();
}

/**
 * @brief Freeze or resume real-time scrolling.
 * @param paused True to pause scrolling, false to resume real-time tracking.
 */
void PlotCanvasWidget::setPaused(bool paused)
{
    if (paused_ != paused)
    {
        paused_ = paused;
        if (paused_)
        {
            pauseTimeUs_ = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        }
        update();
    }
}

/**
 * @brief Refresh canvas theme colors.
 * @param dark True for dark theme, false for light theme.
 */
void PlotCanvasWidget::setDarkTheme(bool dark)
{
    dark_ = dark;
    update();
}

/**
 * @brief Return the active time window in seconds.
 * @return Duration in seconds.
 */
int PlotCanvasWidget::timeWindow() const
{
    return timeWindowSec_;
}

/**
 * @brief Return whether auto-scale is active.
 * @return True if auto-scaling is enabled.
 */
bool PlotCanvasWidget::isAutoScale() const
{
    return autoScale_;
}

/**
 * @brief Return whether plot rendering is paused.
 * @return True if canvas is frozen/paused.
 */
bool PlotCanvasWidget::isPaused() const
{
    return paused_;
}

/**
 * @brief Return currently calculated or locked Y-axis value bounds.
 * @return Pair of {min, max} values.
 */
std::pair<double, double> PlotCanvasWidget::currentYRange() const
{
    return {cachedYMin_, cachedYMax_};
}

/**
 * @brief Track cursor position for inspection hairline.
 * @param event Mouse movement event.
 */
void PlotCanvasWidget::mouseMoveEvent(QMouseEvent *event)
{
    const QRect plotRect = rect().adjusted(55, 12, -15, -24);
    if (plotRect.contains(event->pos()))
    {
        hoverPos_ = event->pos();
        isHovering_ = true;
    }
    else
    {
        isHovering_ = false;
    }
    update();
}

/**
 * @brief Clear hover inspection state when cursor leaves the canvas.
 * @param event Leave event.
 */
void PlotCanvasWidget::leaveEvent(QEvent *event)
{
    Q_UNUSED(event);
    isHovering_ = false;
    update();
}

/**
 * @brief Render background grid, axes, channel curves, and inspection tooltip.
 * @param event Paint event.
 */
void PlotCanvasWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRect totalRect = rect();
    const QRect plotRect = totalRect.adjusted(55, 12, -15, -24);

    if (plotRect.width() <= 20 || plotRect.height() <= 20)
    {
        return;
    }

    // 1. Canvas Background
    const QColor bgColor = dark_ ? QColor("#0d1422") : QColor("#ffffff");
    const QColor borderColor = dark_ ? QColor("#35445d") : QColor("#d1dbea");
    const QColor gridColor = dark_ ? QColor(40, 53, 77, 120) : QColor(226, 232, 240, 200);
    const QColor textColor = dark_ ? QColor("#aab4cd") : QColor("#626c84");

    painter.fillRect(totalRect, dark_ ? QColor("#1b2333") : QColor("#f8fafc"));
    painter.setPen(QPen(borderColor, 1));
    painter.setBrush(bgColor);
    painter.drawRoundedRect(plotRect, 4, 4);

    // 2. Empty State
    const bool hasActiveData = channels_ && !channels_->empty();
    if (!hasActiveData)
    {
        painter.setPen(textColor);
        QFont emptyFont = font();
        emptyFont.setPixelSize(12);
        painter.setFont(emptyFont);
        painter.drawText(plotRect, Qt::AlignCenter,
                         "No active telemetry channels.\nRight-click a numeric field in DECODED FIELDS and select 'Plot this field'.");
        return;
    }

    // 3. Time Calculations
    const int64_t nowUs = paused_ ? pauseTimeUs_ :
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    const int64_t windowUs = int64_t(timeWindowSec_) * 1000000LL;
    const int64_t startUs = nowUs - windowUs;

    // 4. Value Scaling (Y-Range)
    double yMin = std::numeric_limits<double>::infinity();
    double yMax = -std::numeric_limits<double>::infinity();
    bool foundSamples = false;

    if (autoScale_)
    {
        for (const auto &channel : *channels_)
        {
            if (!channel.visible)
            {
                continue;
            }
            const auto rangeOpt = channel.buffer.rangeSince(startUs);
            if (rangeOpt.has_value())
            {
                if (rangeOpt->first < yMin)
                {
                    yMin = rangeOpt->first;
                }
                if (rangeOpt->second > yMax)
                {
                    yMax = rangeOpt->second;
                }
                foundSamples = true;
            }
        }
        if (!foundSamples)
        {
            yMin = -1.0;
            yMax = 1.0;
        }
        else
        {
            double span = yMax - yMin;
            if (span < 1e-6)
            {
                span = (std::abs(yMin) > 1e-4) ? std::abs(yMin) * 0.2 : 1.0;
                yMin -= span;
                yMax += span;
            }
            else
            {
                yMin -= span * 0.1;
                yMax += span * 0.1;
            }
        }
        cachedYMin_ = yMin;
        cachedYMax_ = yMax;
    }
    else
    {
        yMin = cachedYMin_;
        yMax = cachedYMax_;
    }

    const double ySpan = (yMax - yMin) > 1e-9 ? (yMax - yMin) : 1.0;

    // 5. Grid Lines & Ticks
    painter.setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    QFont tickFont = painter.font();
    tickFont.setPixelSize(10);
    painter.setFont(tickFont);

    // Horizontal Grid (Y divisions: 4 segments -> 5 lines)
    const int yDivisions = 4;
    for (int i = 0; i <= yDivisions; ++i)
    {
        const double frac = double(i) / double(yDivisions);
        const int y = plotRect.bottom() - int(frac * plotRect.height());
        const double val = yMin + frac * ySpan;

        painter.setPen(QPen(gridColor, 1, Qt::DashLine));
        painter.drawLine(plotRect.left() + 1, y, plotRect.right() - 1, y);

        painter.setPen(textColor);
        const QString label = QString::number(val, 'f', (std::abs(val) < 10.0 && std::abs(val) > 0.0001) ? 2 : 1);
        const QRect textRect(0, y - 8, plotRect.left() - 6, 16);
        painter.drawText(textRect, Qt::AlignRight | Qt::AlignVCenter, label);
    }

    // Vertical Grid (Time divisions: 5 segments)
    const int timeDivisions = 5;
    for (int i = 0; i <= timeDivisions; ++i)
    {
        const double frac = double(i) / double(timeDivisions);
        const int x = plotRect.left() + int(frac * plotRect.width());
        const double timeOffset = -double(timeWindowSec_) * (1.0 - frac);

        painter.setPen(QPen(gridColor, 1, Qt::DashLine));
        painter.drawLine(x, plotRect.top() + 1, x, plotRect.bottom() - 1);

        painter.setPen(textColor);
        const QString timeLabel = (i == timeDivisions) ? "0s" : QString("%1s").arg(int(timeOffset));
        const QRect timeRect(x - 24, plotRect.bottom() + 4, 48, 16);
        painter.drawText(timeRect, Qt::AlignCenter, timeLabel);
    }

    // 6. Paint Channel Curves
    painter.setClipRect(plotRect.adjusted(1, 1, -1, -1));

    for (const auto &channel : *channels_)
    {
        if (!channel.visible || channel.buffer.empty())
        {
            continue;
        }

        const size_t sampleCount = channel.buffer.size();
        QPolygonF poly;
        poly.reserve(int(sampleCount));

        for (size_t i = 0; i < sampleCount; ++i)
        {
            const auto s = channel.buffer.at(i);
            if (s.timeUs < startUs)
            {
                continue;
            }
            const double xNorm = double(s.timeUs - startUs) / double(windowUs);
            const double yNorm = (s.value - yMin) / ySpan;

            const double px = plotRect.left() + xNorm * plotRect.width();
            const double py = plotRect.bottom() - yNorm * plotRect.height();

            poly.append(QPointF(px, py));
        }

        if (poly.size() >= 2)
        {
            painter.setPen(QPen(channel.color, 2.0));
            painter.drawPolyline(poly);
        }
        else if (poly.size() == 1)
        {
            painter.setPen(Qt::NoPen);
            painter.setBrush(channel.color);
            painter.drawEllipse(poly.first(), 3.0, 3.0);
        }
    }

    painter.setClipping(false);

    // 7. Hover Inspection Hairline & Readout Tooltip
    if (isHovering_ && plotRect.contains(hoverPos_))
    {
        painter.setPen(QPen(dark_ ? QColor("#94a3b8") : QColor("#475569"), 1, Qt::DashLine));
        painter.drawLine(hoverPos_.x(), plotRect.top(), hoverPos_.x(), plotRect.bottom());

        const double hoverFrac = double(hoverPos_.x() - plotRect.left()) / double(plotRect.width());
        const int64_t cursorTimeUs = startUs + int64_t(hoverFrac * double(windowUs));
        const double timeDeltaSec = double(cursorTimeUs - nowUs) / 1000000.0;

        struct CursorReadout
        {
            QString name;
            double value;
            QColor color;
            QPointF pt;
            bool found;
        };

        std::vector<CursorReadout> readouts;
        readouts.reserve(channels_->size());

        for (const auto &channel : *channels_)
        {
            if (!channel.visible || channel.buffer.empty())
            {
                continue;
            }

            const size_t sampleCount = channel.buffer.size();
            double bestDiff = std::numeric_limits<double>::infinity();
            Mav::PlotSample bestSample{};
            bool found = false;

            for (size_t i = 0; i < sampleCount; ++i)
            {
                const auto s = channel.buffer.at(i);
                const double diff = std::abs(double(s.timeUs - cursorTimeUs));
                if (diff < bestDiff)
                {
                    bestDiff = diff;
                    bestSample = s;
                    found = true;
                }
            }

            if (found)
            {
                const double xNorm = double(bestSample.timeUs - startUs) / double(windowUs);
                const double yNorm = (bestSample.value - yMin) / ySpan;
                const double px = plotRect.left() + xNorm * plotRect.width();
                const double py = plotRect.bottom() - yNorm * plotRect.height();

                readouts.push_back({channel.label, bestSample.value, channel.color, QPointF(px, py), true});

                painter.setPen(QPen(dark_ ? Qt::white : Qt::black, 1));
                painter.setBrush(channel.color);
                painter.drawEllipse(QPointF(px, py), 4.0, 4.0);
            }
        }

        if (!readouts.empty())
        {
            const int cardWidth = 140;
            const int cardHeight = 22 + int(readouts.size()) * 18;
            int cardX = hoverPos_.x() + 12;
            if (cardX + cardWidth > plotRect.right())
            {
                cardX = hoverPos_.x() - cardWidth - 12;
            }
            int cardY = hoverPos_.y() - cardHeight / 2;
            if (cardY < plotRect.top() + 4)
            {
                cardY = plotRect.top() + 4;
            }
            if (cardY + cardHeight > plotRect.bottom() - 4)
            {
                cardY = plotRect.bottom() - cardHeight - 4;
            }

            const QRect cardRect(cardX, cardY, cardWidth, cardHeight);
            painter.setPen(QPen(borderColor, 1));
            painter.setBrush(dark_ ? QColor(20, 29, 44, 230) : QColor(255, 255, 255, 230));
            painter.drawRoundedRect(cardRect, 5, 5);

            painter.setPen(textColor);
            QFont headerFont = tickFont;
            headerFont.setBold(true);
            painter.setFont(headerFont);
            painter.drawText(cardRect.adjusted(8, 4, -8, 0), Qt::AlignLeft | Qt::AlignTop,
                             QString("t: %1s").arg(timeDeltaSec, 0, 'f', 2));

            QFont valueFont = tickFont;
            valueFont.setBold(false);
            painter.setFont(valueFont);
            int lineY = cardRect.top() + 20;

            for (const auto &r : readouts)
            {
                painter.setPen(Qt::NoPen);
                painter.setBrush(r.color);
                painter.drawEllipse(QRect(cardRect.left() + 8, lineY + 3, 7, 7));

                painter.setPen(dark_ ? QColor("#e2e8f0") : QColor("#1e293b"));
                const QString valStr = QString::number(r.value, 'g', 4);
                painter.drawText(QRect(cardRect.left() + 20, lineY, cardRect.width() - 28, 16),
                                 Qt::AlignLeft | Qt::AlignVCenter,
                                 QString("%1: %2").arg(r.name, valStr));
                lineY += 18;
            }
        }
    }
}

/**
 * @brief Construct the telemetry plotter toolbar, canvas, and control container.
 * @param parent Owning QWidget container.
 */
TelemetryPlotWidget::TelemetryPlotWidget(QWidget *parent)
    : QFrame(parent)
{
    setObjectName("telemetryPlotter");
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    auto *headerFrame = new QFrame(this);
    headerFrame->setObjectName("plotterHeader");
    auto *headerLayout = new QHBoxLayout(headerFrame);
    headerLayout->setContentsMargins(10, 6, 10, 6);
    headerLayout->setSpacing(8);

    auto *title = new QLabel("PLOTTER", headerFrame);
    title->setObjectName("plotterTitle");
    QFont titleFont = title->font();
    titleFont.setPixelSize(12);
    titleFont.setBold(true);
    title->setFont(titleFont);
    headerLayout->addWidget(title);

    tagsContainer_ = new QWidget(headerFrame);
    tagsContainer_->setObjectName("plotterTagsContainer");
    tagsLayout_ = new QHBoxLayout(tagsContainer_);
    tagsLayout_->setContentsMargins(0, 0, 0, 0);
    tagsLayout_->setSpacing(6);
    headerLayout->addWidget(tagsContainer_);

    headerLayout->addStretch(1);

    timeRangeBox_ = new QComboBox(headerFrame);
    timeRangeBox_->setObjectName("plotTimeRange");
    timeRangeBox_->addItems({"10s", "30s", "60s", "120s"});
    timeRangeBox_->setCurrentIndex(1); // 30s
    timeRangeBox_->setFixedHeight(24);
    headerLayout->addWidget(timeRangeBox_);

    autoScaleBtn_ = new QPushButton("Auto Y", headerFrame);
    autoScaleBtn_->setObjectName("plotAutoScaleBtn");
    autoScaleBtn_->setCheckable(true);
    autoScaleBtn_->setChecked(true);
    autoScaleBtn_->setFixedHeight(24);
    headerLayout->addWidget(autoScaleBtn_);

    pauseBtn_ = new QPushButton("Pause", headerFrame);
    pauseBtn_->setObjectName("plotPauseBtn");
    pauseBtn_->setCheckable(true);
    pauseBtn_->setChecked(false);
    pauseBtn_->setFixedHeight(24);
    headerLayout->addWidget(pauseBtn_);

    clearBtn_ = new QPushButton("Clear", headerFrame);
    clearBtn_->setObjectName("plotClearBtn");
    clearBtn_->setFixedHeight(24);
    headerLayout->addWidget(clearBtn_);

    exportBtn_ = new QPushButton("CSV", headerFrame);
    exportBtn_->setObjectName("plotExportBtn");
    exportBtn_->setFixedHeight(24);
    headerLayout->addWidget(exportBtn_);

    popoutBtn_ = new QPushButton("⧉", headerFrame);
    popoutBtn_->setObjectName("plotPopoutBtn");
    popoutBtn_->setFixedSize(24, 24);
    popoutBtn_->setToolTip("Pop-out to separate window");
    headerLayout->addWidget(popoutBtn_);

    closeBtn_ = new QPushButton("✕", headerFrame);
    closeBtn_->setObjectName("plotCloseBtn");
    closeBtn_->setFixedSize(24, 24);
    closeBtn_->setToolTip("Close plotter");
    headerLayout->addWidget(closeBtn_);

    mainLayout->addWidget(headerFrame);

    canvas_ = new PlotCanvasWidget(this);
    canvas_->setChannels(&channels_);
    mainLayout->addWidget(canvas_, 1);

    connect(timeRangeBox_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx)
    {
        const std::array<int, 4> windows{10, 30, 60, 120};
        if (idx >= 0 && idx < int(windows.size()))
        {
            canvas_->setTimeWindow(windows[idx]);
        }
    });

    connect(autoScaleBtn_, &QPushButton::toggled, this, [this](bool checked)
    {
        canvas_->setAutoScale(checked);
    });

    connect(pauseBtn_, &QPushButton::toggled, this, [this](bool checked)
    {
        canvas_->setPaused(checked);
        pauseBtn_->setText(checked ? "Resume" : "Pause");
    });

    connect(clearBtn_, &QPushButton::clicked, this, [this]()
    {
        clearAllData();
    });

    connect(exportBtn_, &QPushButton::clicked, this, [this]()
    {
        exportCsv(QString());
    });

    connect(popoutBtn_, &QPushButton::clicked, this, [this]()
    {
        setFloating(!isFloating_);
    });

    connect(closeBtn_, &QPushButton::clicked, this, [this]()
    {
        if (isFloating_)
        {
            setFloating(false);
        }
        if (onClosed)
        {
            onClosed();
        }
    });
}

/**
 * @brief Add a telemetry field subscription to the plotter.
 * @param sysid MAVLink System ID.
 * @param compid MAVLink Component ID.
 * @param msgid MAVLink Message ID.
 * @param fieldName Symbolic name of the numeric field.
 * @param label Display label for tags and legend.
 * @return True if channel was successfully added, false if duplicate or limit reached.
 */
bool TelemetryPlotWidget::addChannel(uint32_t sysid, uint8_t compid, uint32_t msgid,
                                    const QString &fieldName, const QString &label)
{
    if (channels_.size() >= 6 || hasChannel(sysid, compid, msgid, fieldName))
    {
        return false;
    }
    PlotChannel ch;
    ch.sysid = sysid;
    ch.compid = compid;
    ch.msgid = msgid;
    ch.fieldName = fieldName;
    ch.label = label;
    ch.color = channelColor(channels_.size());
    channels_.push_back(std::move(ch));
    rebuildChannelTags();
    canvas_->update();
    if (onChannelsChanged)
    {
        onChannelsChanged();
    }
    return true;
}

/**
 * @brief Remove a telemetry field subscription by key.
 * @param sysid MAVLink System ID.
 * @param compid MAVLink Component ID.
 * @param msgid MAVLink Message ID.
 * @param fieldName Symbolic name of the field to remove.
 * @return True if channel was found and removed, false otherwise.
 */
bool TelemetryPlotWidget::removeChannel(uint32_t sysid, uint8_t compid, uint32_t msgid,
                                       const QString &fieldName)
{
    auto it = std::find_if(channels_.begin(), channels_.end(), [&](const PlotChannel &c)
    {
        return c.sysid == sysid && c.compid == compid && c.msgid == msgid && c.fieldName == fieldName;
    });
    if (it != channels_.end())
    {
        channels_.erase(it);
        for (size_t i = 0; i < channels_.size(); ++i)
        {
            channels_[i].color = channelColor(i);
        }
        rebuildChannelTags();
        canvas_->update();
        if (onChannelsChanged)
        {
            onChannelsChanged();
        }
        return true;
    }
    return false;
}

/**
 * @brief Remove channel at specified zero-based index.
 * @param index Index of channel to remove.
 */
void TelemetryPlotWidget::removeChannelAt(size_t index)
{
    if (index < channels_.size())
    {
        channels_.erase(channels_.begin() + index);
        for (size_t i = 0; i < channels_.size(); ++i)
        {
            channels_[i].color = channelColor(i);
        }
        rebuildChannelTags();
        canvas_->update();
        if (onChannelsChanged)
        {
            onChannelsChanged();
        }
    }
}

/**
 * @brief Check whether a specific channel key is already plotted.
 * @param sysid MAVLink System ID.
 * @param compid MAVLink Component ID.
 * @param msgid MAVLink Message ID.
 * @param fieldName Symbolic name of the field.
 * @return True if channel exists.
 */
bool TelemetryPlotWidget::hasChannel(uint32_t sysid, uint8_t compid, uint32_t msgid,
                                    const QString &fieldName) const
{
    return std::any_of(channels_.begin(), channels_.end(), [&](const PlotChannel &c)
    {
        return c.sysid == sysid && c.compid == compid && c.msgid == msgid && c.fieldName == fieldName;
    });
}

/**
 * @brief Return count of active plotted channels.
 * @return Channel count (0 to 6).
 */
size_t TelemetryPlotWidget::channelCount() const
{
    return channels_.size();
}

/**
 * @brief Return const reference to vector of channels.
 * @return Const reference to channels.
 */
const std::vector<PlotChannel> &TelemetryPlotWidget::channels() const
{
    return channels_;
}

/**
 * @brief Return mutable reference to vector of channels.
 * @return Mutable reference to channels.
 */
std::vector<PlotChannel> &TelemetryPlotWidget::channels()
{
    return channels_;
}

/**
 * @brief Ingest a snapshot of received messages and parameters into active plot channels.
 * @param snapshot Immutable MAVLink receiver snapshot.
 * @param nowUs Optional custom timestamp in microseconds (0 to use steady_clock::now).
 */
void TelemetryPlotWidget::processSnapshot(const Snapshot &snapshot, int64_t nowUs)
{
    if (channels_.empty())
    {
        return;
    }

    if (nowUs <= 0)
    {
        nowUs = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    for (auto &channel : channels_)
    {
        if (channel.msgid == 22)
        {
            if (snapshot.params)
            {
                const auto devKey = std::make_pair(channel.sysid, channel.compid);
                const auto it = snapshot.params->find(devKey);
                if (it != snapshot.params->end())
                {
                    const auto paramIt = it->second.byId.find(channel.fieldName.toStdString());
                    if (paramIt != it->second.byId.end())
                    {
                        const auto &p = paramIt->second;
                        double val = 0.0;
                        if (p.type == MAV_PARAM_TYPE_REAL32 || p.type == MAV_PARAM_TYPE_REAL64)
                        {
                            val = double(p.value);
                        }
                        else
                        {
                            mavlink_param_union_t u;
                            u.param_float = p.value;
                            val = double(u.param_int32);
                        }
                        channel.buffer.push(nowUs, val);
                        channel.latestValue = val;
                        channel.hasValue = true;
                    }
                }
            }
        }
        else
        {
            for (const auto &rxView : snapshot.rx)
            {
                const auto &msg = rxView.message;
                if (msg.sysid == channel.sysid && msg.compid == channel.compid && msg.msgid == channel.msgid)
                {
                    const auto *info = mavlink_get_message_info_by_id(channel.msgid);
                    if (info)
                    {
                        for (unsigned i = 0; i < info->num_fields; ++i)
                        {
                            if (channel.fieldName == QString::fromLatin1(info->fields[i].name))
                            {
                                const auto valOpt = Mav::readFieldNumeric(msg, info->fields[i]);
                                if (valOpt.has_value())
                                {
                                    channel.buffer.push(nowUs, *valOpt);
                                    channel.latestValue = *valOpt;
                                    channel.hasValue = true;
                                }
                                break;
                            }
                        }
                    }
                    break;
                }
            }
        }
    }
    updateCanvas();
}

/**
 * @brief Update tags and refresh canvas after adding samples.
 */
void TelemetryPlotWidget::updateCanvas()
{
    updateChannelTagValues();
    canvas_->update();
}

/**
 * @brief Clear data from all channel buffers.
 */
void TelemetryPlotWidget::clearAllData()
{
    for (auto &c : channels_)
    {
        c.buffer.clear();
        c.hasValue = false;
    }
    updateChannelTagValues();
    canvas_->update();
}

/**
 * @brief Apply active light or dark theme styling.
 * @param dark True for dark theme, false for light theme.
 */
void TelemetryPlotWidget::setDarkTheme(bool dark)
{
    dark_ = dark;
    if (canvas_)
    {
        canvas_->setDarkTheme(dark);
    }
    rebuildChannelTags();
}

/**
 * @brief Export all channel timeseries samples into a CSV file.
 * @param targetPath Path to destination file (or empty to prompt file dialog).
 * @return True if export succeeded, false on failure or cancellation.
 */
bool TelemetryPlotWidget::exportCsv(const QString &targetPath) const
{
    QString path = targetPath;
    if (path.isEmpty())
    {
        path = QFileDialog::getSaveFileName(const_cast<TelemetryPlotWidget *>(this),
                                            "Export Telemetry Plot to CSV",
                                            "telemetry_plot.csv",
                                            "CSV Files (*.csv)");
    }
    if (path.isEmpty())
    {
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        return false;
    }

    QTextStream out(&file);

    out << "Time_us,Relative_s";
    for (const auto &c : channels_)
    {
        out << "," << c.label;
    }
    out << "\n";

    std::vector<int64_t> allTimes;
    for (const auto &c : channels_)
    {
        for (const auto &s : c.buffer.samples())
        {
            allTimes.push_back(s.timeUs);
        }
    }
    std::sort(allTimes.begin(), allTimes.end());
    allTimes.erase(std::unique(allTimes.begin(), allTimes.end()), allTimes.end());

    if (allTimes.empty())
    {
        return true;
    }

    const int64_t t0 = allTimes.front();

    std::vector<std::map<int64_t, double>> channelMaps(channels_.size());
    for (size_t i = 0; i < channels_.size(); ++i)
    {
        for (const auto &s : channels_[i].buffer.samples())
        {
            channelMaps[i][s.timeUs] = s.value;
        }
    }

    for (int64_t t : allTimes)
    {
        const double relSec = double(t - t0) / 1000000.0;
        out << t << "," << QString::number(relSec, 'f', 4);
        for (size_t i = 0; i < channels_.size(); ++i)
        {
            out << ",";
            auto it = channelMaps[i].find(t);
            if (it != channelMaps[i].end())
            {
                out << QString::number(it->second, 'g', 6);
            }
        }
        out << "\n";
    }

    return true;
}

/**
 * @brief Return pointer to the underlying canvas widget.
 * @return Pointer to PlotCanvasWidget.
 */
PlotCanvasWidget *TelemetryPlotWidget::canvas() const
{
    return canvas_;
}

/**
 * @brief Return pointer to pause toggle button.
 * @return Pointer to pause button.
 */
QPushButton *TelemetryPlotWidget::pauseButton() const
{
    return pauseBtn_;
}

/**
 * @brief Return pointer to auto-scale toggle button.
 * @return Pointer to auto-scale button.
 */
QPushButton *TelemetryPlotWidget::autoScaleButton() const
{
    return autoScaleBtn_;
}

/**
 * @brief Return pointer to clear buffer button.
 * @return Pointer to clear button.
 */
QPushButton *TelemetryPlotWidget::clearButton() const
{
    return clearBtn_;
}

/**
 * @brief Return pointer to CSV export button.
 * @return Pointer to export button.
 */
QPushButton *TelemetryPlotWidget::exportButton() const
{
    return exportBtn_;
}

/**
 * @brief Return pointer to the pop-out / dock button.
 * @return Pointer to pop-out button.
 */
QPushButton *TelemetryPlotWidget::popoutButton() const
{
    return popoutBtn_;
}

/**
 * @brief Return pointer to close plotter button.
 * @return Pointer to close button.
 */
QPushButton *TelemetryPlotWidget::closeButton() const
{
    return closeBtn_;
}

/**
 * @brief Return pointer to time range combo box.
 * @return Pointer to time range combo box.
 */
QComboBox *TelemetryPlotWidget::timeRangeBox() const
{
    return timeRangeBox_;
}

/**
 * @brief Configure whether the plotter is floating as an independent window or docked.
 * @param floating True to pop-out as standalone window, false to dock into parent container.
 */
void TelemetryPlotWidget::setFloating(bool floating)
{
    if (isFloating_ == floating)
    {
        return;
    }

    isFloating_ = floating;

    if (isFloating_)
    {
        setWindowFlags(Qt::Window);
        setWindowTitle("MAVLinkLab — Telemetry Plotter");
        if (parentWidget())
        {
            setWindowIcon(parentWidget()->windowIcon());
        }
        resize(850, 550);
        if (popoutBtn_)
        {
            popoutBtn_->setText("⤓");
            popoutBtn_->setToolTip("Dock into main window");
        }
        if (onFloatingChanged)
        {
            onFloatingChanged(true);
        }
        show();
        raise();
        activateWindow();
    }
    else
    {
        setWindowFlags(Qt::Widget);
        if (popoutBtn_)
        {
            popoutBtn_->setText("⧉");
            popoutBtn_->setToolTip("Pop-out to separate window");
        }
        if (onFloatingChanged)
        {
            onFloatingChanged(false);
        }
    }
}

/**
 * @brief Return whether the plotter is currently in floating window mode.
 * @return True if floating, false if docked.
 */
bool TelemetryPlotWidget::isFloating() const
{
    return isFloating_;
}

/**
 * @brief Handle window close event when in floating mode, docking back and notifying owner.
 * @param event Close event.
 */
void TelemetryPlotWidget::closeEvent(QCloseEvent *event)
{
    if (isFloating_)
    {
        setFloating(false);
    }
    if (onClosed)
    {
        onClosed();
    }
    if (event)
    {
        event->accept();
    }
}

void TelemetryPlotWidget::rebuildChannelTags()
{
    tagWidgets_.clear();
    QLayoutItem *child = nullptr;
    while ((child = tagsLayout_->takeAt(0)) != nullptr)
    {
        delete child->widget();
        delete child;
    }

    for (size_t i = 0; i < channels_.size(); ++i)
    {
        const auto &c = channels_[i];
        auto *tag = new QFrame(tagsContainer_);
        auto *tagLayout = new QHBoxLayout(tag);
        tagLayout->setContentsMargins(5, 2, 4, 2);
        tagLayout->setSpacing(4);

        auto *dot = new QLabel("●", tag);
        dot->setStyleSheet(QString("color: %1; font-size: 10px;").arg(c.color.name()));
        tagLayout->addWidget(dot);

        const QString valStr = c.hasValue ? QString::number(c.latestValue, 'g', 4) : "--";
        auto *text = new QLabel(QString("%1: %2").arg(c.label, valStr), tag);
        text->setStyleSheet(QString("color: %1; font-size: 11px; font-weight: 500;")
                            .arg(dark_ ? "#edf0fa" : "#1e293b"));
        tagLayout->addWidget(text);

        auto *removeBtn = new QPushButton("×", tag);
        removeBtn->setFixedSize(14, 14);
        removeBtn->setStyleSheet("QPushButton { border: 0; background: transparent; color: #94a3b8; font-size: 12px; font-weight: bold; } "
                                 "QPushButton:hover { color: #ef4444; }");
        tagLayout->addWidget(removeBtn);

        const size_t chIdx = i;
        connect(removeBtn, &QPushButton::clicked, this, [this, chIdx]()
        {
            removeChannelAt(chIdx);
        });

        tag->setStyleSheet(QString("QFrame { background: %1; border: 1px solid %2; border-radius: 3px; }")
                           .arg(dark_ ? c.color.name() + "22" : c.color.name() + "18",
                                c.color.name() + "88"));
        tagsLayout_->addWidget(tag);
        tagWidgets_.push_back({tag, text});
    }
}

/**
 * @brief Update numeric value strings in existing channel tags without rebuilding widgets.
 */
void TelemetryPlotWidget::updateChannelTagValues()
{
    if (tagWidgets_.size() != channels_.size())
    {
        rebuildChannelTags();
        return;
    }
    for (size_t i = 0; i < channels_.size(); ++i)
    {
        const auto &c = channels_[i];
        if (tagWidgets_[i].valueLabel)
        {
            const QString valStr = c.hasValue ? QString::number(c.latestValue, 'g', 4) : "--";
            tagWidgets_[i].valueLabel->setText(QString("%1: %2").arg(c.label, valStr));
        }
    }
}

QColor TelemetryPlotWidget::channelColor(size_t index) const
{
    static const std::array<const char *, 6> colors = {
        "#06b6d4", // Cyan
        "#f59e0b", // Amber
        "#10b981", // Emerald
        "#8b5cf6", // Violet
        "#f43f5e", // Rose
        "#3b82f6"  // Blue
    };
    return QColor(colors[index % colors.size()]);
}
