#pragma once
#include "worker.h"
#include <QMainWindow>
#include <QDialog>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QThread>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTreeWidget>
#include <QLabel>
#include <QSpinBox>
#include <QCheckBox>
#include <QSplitter>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLayoutItem>
#include <QPainter>
#include <QMouseEvent>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QFontDatabase>
#include <functional>
#include <limits>
#include <vector>
#include <array>
#include <chrono>
#include <cmath>
#include <map>

class TxSummaryWidget;
class ConnectionStatusWidget;
class StatusBarMetricsWidget;
class StatusBarMessageWidget;

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
    explicit PlotCanvasWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName("plotCanvas");
        setAttribute(Qt::WA_OpaquePaintEvent, false);
        setMouseTracking(true);
        setMinimumSize(200, 120);
    }

    /** @brief Set reference to the active channel list. */
    void setChannels(const std::vector<PlotChannel> *channels)
    {
        channels_ = channels;
        update();
    }

    /** @brief Configure display time window in seconds (e.g. 10, 30, 60, 120). */
    void setTimeWindow(int seconds)
    {
        timeWindowSec_ = seconds > 0 ? seconds : 30;
        update();
    }

    /** @brief Enable or disable vertical auto-scaling. */
    void setAutoScale(bool autoScale)
    {
        autoScale_ = autoScale;
        update();
    }

    /** @brief Freeze or resume real-time scrolling. */
    void setPaused(bool paused)
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

    /** @brief Refresh canvas theme colors. */
    void setDarkTheme(bool dark)
    {
        dark_ = dark;
        update();
    }

    /** @brief Return the active time window in seconds. */
    int timeWindow() const
    {
        return timeWindowSec_;
    }

    /** @brief Return whether auto-scale is active. */
    bool isAutoScale() const
    {
        return autoScale_;
    }

    /** @brief Return whether plot rendering is paused. */
    bool isPaused() const
    {
        return paused_;
    }

    /** @brief Return currently calculated or locked Y-axis value bounds. */
    std::pair<double, double> currentYRange() const
    {
        return {cachedYMin_, cachedYMax_};
    }

protected:
    void mouseMoveEvent(QMouseEvent *event) override
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

    void leaveEvent(QEvent *event) override
    {
        Q_UNUSED(event);
        isHovering_ = false;
        update();
    }

    void paintEvent(QPaintEvent *event) override
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
                const auto samples = channel.buffer.samples();
                for (const auto &s : samples)
                {
                    if (s.timeUs >= startUs)
                    {
                        if (s.value < yMin)
                        {
                            yMin = s.value;
                        }
                        if (s.value > yMax)
                        {
                            yMax = s.value;
                        }
                        foundSamples = true;
                    }
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
            if (!channel.visible)
            {
                continue;
            }
            const auto samples = channel.buffer.samples();
            if (samples.empty())
            {
                continue;
            }

            QPolygonF poly;
            poly.reserve(int(samples.size()));

            for (const auto &s : samples)
            {
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
                if (!channel.visible)
                {
                    continue;
                }
                const auto samples = channel.buffer.samples();
                if (samples.empty())
                {
                    continue;
                }

                double bestDiff = std::numeric_limits<double>::infinity();
                Mav::PlotSample bestSample{};
                bool found = false;

                for (const auto &s : samples)
                {
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

    explicit TelemetryPlotWidget(QWidget *parent = nullptr)
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

        connect(closeBtn_, &QPushButton::clicked, this, [this]()
        {
            if (onClosed)
            {
                onClosed();
            }
        });
    }

    /** @brief Add a telemetry field subscription to the plotter. */
    bool addChannel(uint32_t sysid, uint8_t compid, uint32_t msgid,
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

    /** @brief Remove a telemetry field subscription by key. */
    bool removeChannel(uint32_t sysid, uint8_t compid, uint32_t msgid,
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

    /** @brief Remove channel at specified zero-based index. */
    void removeChannelAt(size_t index)
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

    /** @brief Check whether a specific channel key is already plotted. */
    bool hasChannel(uint32_t sysid, uint8_t compid, uint32_t msgid,
                    const QString &fieldName) const
    {
        return std::any_of(channels_.begin(), channels_.end(), [&](const PlotChannel &c)
        {
            return c.sysid == sysid && c.compid == compid && c.msgid == msgid && c.fieldName == fieldName;
        });
    }

    /** @brief Return count of active plotted channels. */
    size_t channelCount() const
    {
        return channels_.size();
    }

    /** @brief Return const reference to vector of channels. */
    const std::vector<PlotChannel> &channels() const
    {
        return channels_;
    }

    /** @brief Return mutable reference to vector of channels. */
    std::vector<PlotChannel> &channels()
    {
        return channels_;
    }

    /** @brief Update tags and refresh canvas after adding samples. */
    void updateCanvas()
    {
        rebuildChannelTags();
        canvas_->update();
    }

    /** @brief Clear data from all channel buffers. */
    void clearAllData()
    {
        for (auto &c : channels_)
        {
            c.buffer.clear();
            c.hasValue = false;
        }
        rebuildChannelTags();
        canvas_->update();
    }

    /** @brief Apply active light or dark theme styling. */
    void setDarkTheme(bool dark)
    {
        dark_ = dark;
        if (canvas_)
        {
            canvas_->setDarkTheme(dark);
        }
        rebuildChannelTags();
    }

    /** @brief Export all channel timeseries samples into a CSV file. */
    bool exportCsv(const QString &targetPath) const
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

    PlotCanvasWidget *canvas() const
    {
        return canvas_;
    }

    QPushButton *pauseButton() const
    {
        return pauseBtn_;
    }

    QPushButton *autoScaleButton() const
    {
        return autoScaleBtn_;
    }

    QPushButton *clearButton() const
    {
        return clearBtn_;
    }

    QPushButton *exportButton() const
    {
        return exportBtn_;
    }

    QPushButton *closeButton() const
    {
        return closeBtn_;
    }

    QComboBox *timeRangeBox() const
    {
        return timeRangeBox_;
    }

private:
    void rebuildChannelTags()
    {
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
        }
    }

    QColor channelColor(size_t index) const
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

    std::vector<PlotChannel> channels_;
    PlotCanvasWidget *canvas_ = nullptr;
    QWidget *tagsContainer_ = nullptr;
    QHBoxLayout *tagsLayout_ = nullptr;
    QComboBox *timeRangeBox_ = nullptr;
    QPushButton *autoScaleBtn_ = nullptr;
    QPushButton *pauseBtn_ = nullptr;
    QPushButton *clearBtn_ = nullptr;
    QPushButton *exportBtn_ = nullptr;
    QPushButton *closeBtn_ = nullptr;
    bool dark_ = false;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    /** @brief Build the desktop UI and start an isolated serial worker. */
    explicit MainWindow(QWidget *parent = nullptr);
    /** @brief Close the serial port in its owning thread and join the worker. */
    ~MainWindow() override;
    /** @brief Select display mode for the TX status indicator (0: DualPills, 1: SegmentedChip). */
    void setTxSummaryMode(int mode);
    /** @brief Select display rate unit for the status bar metrics (0: B/s, 1: KB/s, 2: MB/s). */
    void setStatusBarRateUnit(int unit);
    /** @brief Update the TX summary counts directly (e.g. for inspection/testing). */
    void setTxSummaryStats(int total, int running, double estimatedBps = 0.0, double utilization = 0.0);
    /** @brief Update the connection status indicator directly (e.g. for inspection/testing). */
    void setConnectionStatus(const QString &state, bool open);
    /** @brief Update the status bar metrics directly (e.g. for inspection/testing). */
    void setStatusMetrics(double rxBps, double txBps, uint64_t crc, uint64_t pending, uint64_t overflow,
                          double rxUtil = 0.0, double txUtil = 0.0, int baud = 0);
    /** @brief Apply one snapshot directly (e.g. for inspection/testing). */
    void applySnapshot(SnapshotPtr snapshot);
    /** @brief Update the RX throughput indicator pill directly (e.g. for inspection/testing). */
    void setRxThroughputStats(double bps, double utilization, int baud);
    /** @brief Stop the background snapshot polling timer (e.g. for deterministic unit testing). */
    void stopRefreshTimer();
    /** @brief Return the currently configured serial port settings. */
    PortConfig currentPortConfig() const;
    /** @brief Return pointer to the telemetry plotter widget. */
    TelemetryPlotWidget *plotterWidget() const;
    /** @brief Add or toggle a field subscription in the telemetry plotter. */
    void togglePlotField(uint32_t sysid, uint8_t compid, uint32_t msgid, const QString &fieldName, const QString &label);
    /** @brief Return the application icon containing multi-resolution raster assets. */
    static QIcon appIcon();
protected:
    /** @brief Persist presentation layout when the window closes normally. */
    void closeEvent(QCloseEvent *event) override;
    /** @brief Track theme/input changes and clip the UART popup to its rounded boundary. */
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    /** @brief Re-enumerate ports while retaining an explicitly typed PTY/device path. */
    void refreshPorts();
    /** @brief Send the selected serial configuration to the worker. */
    void toggleConnection();
    /** @brief Create or edit a TX entry; start newly added entries when connected. */
    void editMessage(bool edit);
    /** @brief Decode the selected stream into the field inspector. */
    void updateInspector();
    /** @brief Filter RX rows by sender and message identity. */
    void filterRx();
    /** @brief Apply semantic light/dark tokens or the current system palette. */
    void applyTheme();
    /** @brief Display a persistent actionable error in the status bar. */
    void showError(const QString &message);
    /** @brief Display a compact alert dialog when serial port connection fails. */
    void showConnectionError(const QString &title, const QString &message);
    /**
     * @brief Open parameter value modification dialog and dispatch PARAM_SET upon confirmation.
     * @param row Index of the parameter row in the inspector table.
     */
    void editParameter(int row);
    /** @brief Return the stable identity of the selected TX row, or zero. */
    uint64_t selectedTx() const;
    /** @brief Enable actions according to connection and selection state. */
    void updateActions();
    /** @brief Refresh the compact serial framing summary without changing I/O. */
    void updateSerialSummary();
    /** @brief Resize TX rows to fit at most two title lines without moving inline controls. */
    void updateTxHeights();
    /** @brief Restore only presentation geometry and column widths from application settings. */
    void restoreLayout();
    /** @brief Reset window geometry, splitters and column presentations to defaults. */
    void resetLayout();
    /** @brief Reset application preferences and configurations to default values. */
    void restoreDefaultSettings();
    /** @brief Restore application preferences such as theme and MAVLink defaults. */
    void restoreSettings();
    /** @brief Save application preferences such as theme and MAVLink defaults. */
    void saveSettings();
    /** @brief Restore configured TX messages from workspace storage. */
    void restoreTxMessages();
    /** @brief Save configured TX messages to workspace storage. */
    void saveTxMessages();
    /** @brief Import a validated TX profile with an explicit append option. */
    void importProfile();
    /** @brief Choose a destination and export the worker's current TX definitions. */
    void exportProfile();
    /** @brief Display the Check for Updates dialog and query GitHub latest release. */
    void checkForUpdates();
    /** @brief Display a popup menu with preset frequencies and custom rate input. */
    void showQuickRateMenu(quint64 key, double currentHz, const QPoint &globalPos);
    /** @brief Display transient confirmation badge and cell flash when copying a field value. */
    void showCopiedFeedback(int row);
    /** @brief Update the color styling of the RX throughput pill based on channel utilization. */
    void updateRxThroughputStyle(double util);
    bool profileBusy = false;
    QString profileExportPath;
    QAction *importProfileAction = nullptr;
    QAction *exportProfileAction = nullptr;
    QAction *checkUpdatesAction = nullptr;
    SerialWorker *worker = nullptr;
    QThread thread;
    QTimer refresh;
    bool snapshotPending = false;
    bool connected = false;
    bool connecting = false;
    bool reconnecting = false;
    bool connectingByUser = false;
    QString connectingPort;
    int connectingBaud = 0;
    bool paused = false;
    uint64_t nextKey = 1;
    SnapshotPtr latest;
    QComboBox *portBox = nullptr;
    QComboBox *baudBox = nullptr;
    QComboBox *dataBox = nullptr;
    QComboBox *parityBox = nullptr;
    QComboBox *stopBox = nullptr;
    QComboBox *flowBox = nullptr;
    QComboBox *themeBox = nullptr;
    QSpinBox *defaultSysIdBox = nullptr;
    QSpinBox *defaultCompIdBox = nullptr;
    QComboBox *defaultVersionBox = nullptr;
    QCheckBox *autoReconnectBox = nullptr;
    QCheckBox *deassertControlLinesBox = nullptr;
    QCheckBox *restoreTxBox = nullptr;
    QCheckBox *showDebugBarBox = nullptr;
    QComboBox *statsRateUnitBox = nullptr;
    QLabel *statsRateUnitLabel = nullptr;
    QDialog *settingsDialog = nullptr;
    ConnectionStatusWidget *connectionStatusPill = nullptr;
    QPushButton *connectButton = nullptr;
    QFrame *statusBarCard = nullptr;
    StatusBarMetricsWidget *metricsWidget = nullptr;
    StatusBarMessageWidget *lastError = nullptr;
    QTableWidget *txTable = nullptr;
    QMenu *txMenu = nullptr;
    QTreeWidget *rxTree = nullptr;
    QTableWidget *inspector = nullptr;
    QLineEdit *rxFilter = nullptr;
    QLineEdit *paramFilter = nullptr;
    bool isParamView = false;
    uint32_t activeParamSysId = 0;
    uint8_t activeParamCompId = 0;
    QAction *editAction = nullptr;
    QAction *removeAction = nullptr;
    QAction *onceAction = nullptr;
    QAction *startAction = nullptr;
    QAction *stopAction = nullptr;
    QPushButton *runAllButton = nullptr;
    QPushButton *stopAllButton = nullptr;
    QPushButton *deleteAllButton = nullptr;
    QLabel *serialSummary = nullptr;
    TxSummaryWidget *txSummary = nullptr;
    QLabel *rxSummary = nullptr;
    QLabel *rxThroughput = nullptr;
    double lastRxUtil = 0.0;
    QLabel *inspectorTitle = nullptr;
    QLabel *inspectorSource = nullptr;
    QLabel *copyFeedbackBadge = nullptr;
    QTimer *copyFeedbackTimer = nullptr;
    int copiedInspectorRow = -1;
    QLabel *pausedLabel = nullptr;
    QPushButton *plotToggleBtn = nullptr;
    QAction *plotFieldAction = nullptr;
    QSplitter *inspectorContentSplitter = nullptr;
    TelemetryPlotWidget *telemetryPlotter = nullptr;
    std::map<QString, QString> lastFieldValues;
};
