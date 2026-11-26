/**
 * @file window_rx.cpp
 * @brief MainWindow RX stream tree, snapshot processing, inspector, and parameter editor.
 */

#include "window.h"
#include "window_private.h"
#include "mavlink_enums.h"
#include "plotter.h"
#include <QScrollBar>
#include <QHeaderView>
#include <QApplication>
#include <QPainter>


void MainWindow::applySnapshot(SnapshotPtr snapshot)
{
    snapshotPending = false;
    latest = std::move(snapshot);

    std::vector<const TxView *> visibleTx;
    visibleTx.reserve(latest->tx.size());
    bool foundPendingDelete = false;
    size_t running = 0;
    for (const auto &view : latest->tx)
    {
        if (pendingDeleteKey != 0 && view.entry.key == pendingDeleteKey)
        {
            foundPendingDelete = true;
            continue;
        }
        visibleTx.push_back(&view);
        if (view.entry.active)
        {
            ++running;
        }
    }
    if (!foundPendingDelete)
    {
        pendingDeleteKey = 0;
    }

    txSummary->setStats(int(visibleTx.size()), int(running),
                        latest->estimatedTxBps, latest->estimatedTxUtilization);
    setRxThroughputStats(latest->rxBytesPerSecond, latest->rxUtilizationPercent, latest->baud);
    const uint64_t selected = pendingSelectionKey ? pendingSelectionKey : selectedTx();
    pendingSelectionKey = 0;
    const uint64_t dupKey = pendingDuplicateKey;
    pendingDuplicateKey = 0;
    int dupRow = -1;
    if (!isDraggingTx)
    {
        const bool rowsChanged = (txTable->rowCount() != int(visibleTx.size()));
        bool titleChanged = rowsChanged;
        txTable->setRowCount(int(visibleTx.size()));
        for (size_t i = 0; i < visibleTx.size(); ++i)
        {
            const auto &view = *visibleTx[i];
            const auto &entry = view.entry;
            const int row = int(i);
            if (dupKey != 0 && entry.key == dupKey)
            {
                dupRow = row;
            }
            if (cell(txTable, row, 0, QString::number(entry.id)))
            {
                titleChanged = true;
            }
            txTable->item(row, 0)->setData(Qt::UserRole, qulonglong(entry.key));
            if (cell(txTable, row, 1, messageName(entry.id)))
            {
                titleChanged = true;
            }
            // Cards use the interface font rather than the numeric cell font.
            txTable->item(row, 1)->setFont(txTable->font());
            cell(txTable, row, 2, QString("%1 / %2").arg(entry.sysid).arg(entry.compid));
            cell(txTable, row, 3, QString::number(entry.hz, 'f', (entry.hz > 0.0 && entry.hz < 10.0 && entry.hz != std::floor(entry.hz)) ? 1 : 0));
            cell(txTable, row, 4, QString::number(view.measuredHz, 'f', 0));
            cell(txTable, row, 5, QString::number(qulonglong(entry.count)));
            cell(txTable, row, 6, entry.error.empty() ? (entry.active ? "Running" : "Stopped") : QString::fromStdString(entry.error));
            const QString description = QString("%1 · ID %2\nSystem / Component %3\nTarget %4 Hz · Actual %5 Hz\nEst. wire load: ~%6\n%7 frames handed to Qt output\n%8")
                                        .arg(messageName(entry.id), QString::number(entry.id), txTable->item(row, 2)->text(),
                                             txTable->item(row, 3)->text(), txTable->item(row, 4)->text(),
                                             QString::fromStdString(Mav::formatByteRate(view.estimatedBps)),
                                             txTable->item(row, 5)->text(), txTable->item(row, 6)->text());
            txTable->item(row, 1)->setData(Qt::AccessibleTextRole, description);
            if (entry.key == selected && txTable->currentRow() != row)
            {
                const int scroll = txTable->verticalScrollBar()->value();
                txTable->setCurrentCell(row, 1);
                txTable->verticalScrollBar()->setValue(scroll);
            }
        }
        if (titleChanged)
        {
            updateTxHeights();
        }
        if (dupRow >= 0)
        {
            startTxDuplicateAnimation(dupRow);
            if (txTable->item(dupRow, 1))
            {
                txTable->scrollToItem(txTable->item(dupRow, 1), QAbstractItemView::EnsureVisible);
            }
        }
    }
    const bool dark = themeBox->currentIndex() == 2 ||
                      (themeBox->currentIndex() == 0 && qApp->palette().color(QPalette::Window).lightness() < 128);
    if (metricsWidget)
    {
        metricsWidget->updateMetrics(latest->rxBytesPerSecond,
                                     latest->txBytesPerSecond,
                                     latest->crcErrors,
                                     latest->pending,
                                     latest->overflow,
                                     dark,
                                     latest->rxUtilizationPercent,
                                     latest->txUtilizationPercent,
                                     latest->baud);
    }
    if (!paused)
    {
        const auto &fixedFont = cachedFixedFont();
        if (rxTree->topLevelItemCount() == 0 && (!rxDeviceItems_.empty() || !rxStreamItems_.empty()))
        {
            rxDeviceItems_.clear();
            rxStreamItems_.clear();
        }

        for (const auto &entry : latest->rx)
        {
            const auto &message = entry.message;
            const uint32_t sysid = message.sysid;
            const uint8_t compid = message.compid;

            const uint64_t devKey = (uint64_t(sysid) << 8) | uint64_t(compid);
            auto devIt = rxDeviceItems_.find(devKey);
            QTreeWidgetItem *device = (devIt != rxDeviceItems_.end()) ? devIt->second : nullptr;

            if (!device)
            {
                device = new QTreeWidgetItem(rxTree);
                device->setData(0, Qt::UserRole, sysid);
                device->setData(0, Qt::UserRole + 1, compid);
                device->setExpanded(true);
                device->setFirstColumnSpanned(true);

                QFont groupFont = rxTree->font();
                groupFont.setPixelSize(13);
                groupFont.setWeight(QFont::Bold);
                device->setFont(0, groupFont);

                device->setTextAlignment(0, Qt::AlignLeft | Qt::AlignVCenter);
                device->setText(0, QString("System %1 · Component %2").arg(sysid).arg(compid));
                rxDeviceItems_[devKey] = device;
            }

            const uint64_t streamKey = (uint64_t(sysid) << 40) | (uint64_t(compid) << 32) | uint64_t(message.msgid);
            auto streamIt = rxStreamItems_.find(streamKey);
            QTreeWidgetItem *item = (streamIt != rxStreamItems_.end()) ? streamIt->second : nullptr;

            if (!item)
            {
                item = new QTreeWidgetItem(device);
                item->setData(0, Qt::UserRole, message.msgid);

                QFont messageFont = rxTree->font();
                messageFont.setPixelSize(13);
                item->setFont(0, messageFont);
                item->setFont(1, fixedFont);
                item->setFont(2, fixedFont);
                item->setFont(3, fixedFont);

                item->setTextAlignment(0, Qt::AlignLeft | Qt::AlignVCenter);
                item->setTextAlignment(1, Qt::AlignCenter);
                item->setTextAlignment(2, Qt::AlignCenter);
                item->setTextAlignment(3, Qt::AlignCenter);

                const auto *entryInfo = mavlink_get_msg_entry(message.msgid);
                const int payloadLen = entryInfo ? int(entryInfo->max_msg_len) : int(message.len);

                item->setText(0, QString("[%1] %2").arg(message.msgid).arg(messageName(message.msgid)));
                item->setText(1, QString("%1 B").arg(payloadLen));
                item->setData(1, Qt::UserRole, payloadLen);
                rxStreamItems_[streamKey] = item;
            }

            // Dirty-check Rate (Hz)
            const double prevHz = item->data(2, Qt::UserRole).toDouble();
            if (std::abs(entry.hz - prevHz) >= 0.05 || (prevHz == 0.0 && entry.hz > 0.0) || (entry.hz == 0.0 && prevHz > 0.0))
            {
                item->setText(2, QString::number(entry.hz, 'f', (entry.hz > 0.0 && entry.hz < 10.0) ? 1 : 0));
                item->setData(2, Qt::UserRole, entry.hz);
            }

            // Dirty-check Count
            const qulonglong prevCount = item->data(3, Qt::UserRole).toULongLong();
            if (qulonglong(entry.count) != prevCount)
            {
                item->setText(3, QString::number(qulonglong(entry.count)));
                item->setData(3, Qt::UserRole, qulonglong(entry.count));
            }

            item->setData(0, Qt::UserRole + 2, entry.age);

            const QString currentStatus = rxStatus(entry.age);
            if (message.msgid == 22 && latest && latest->params)
            {
                const auto pDevKey = std::make_pair(sysid, compid);
                const auto paramIt = latest->params->find(pDevKey);
                if (paramIt != latest->params->end() && paramIt->second.totalCount > 0)
                {
                    item->setToolTip(0, QString("Parameters: %1 / %2 loaded · %3")
                                            .arg(paramIt->second.byId.size())
                                            .arg(paramIt->second.totalCount)
                                            .arg(currentStatus));
                }
                else
                {
                    item->setToolTip(0, currentStatus);
                }
            }
            else
            {
                item->setToolTip(0, currentStatus);
            }
            item->setData(0, Qt::AccessibleTextRole, item->text(0) + ", " + currentStatus);
        }
        sortRxStreams();
        filterRx();
        updateInspector();
        updateRxExpandIcon();
    }
    if (telemetryPlotter && telemetryPlotter->channelCount() > 0 && !paused && latest)
    {
        telemetryPlotter->processSnapshot(*latest);
    }
    updateActions();
}

/**
 * @brief Sort child stream items within each top-level System/Component group.
 */
void MainWindow::sortRxStreams()
{
    if (!rxTree || rxSortColumn < 0)
    {
        return;
    }

    QTreeWidgetItem *current = rxTree->currentItem();
    const int scroll = rxTree->verticalScrollBar() ? rxTree->verticalScrollBar()->value() : 0;

    for (int i = 0; i < rxTree->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem *device = rxTree->topLevelItem(i);
        if (!device || device->childCount() < 2)
        {
            continue;
        }

        QList<QTreeWidgetItem *> children = device->takeChildren();
        const int col = rxSortColumn;
        const bool asc = (rxSortOrder == Qt::AscendingOrder);

        std::stable_sort(children.begin(), children.end(), [col, asc](const QTreeWidgetItem *a, const QTreeWidgetItem *b)
        {
            const uint32_t idA = a->data(0, Qt::UserRole).toUInt();
            const uint32_t idB = b->data(0, Qt::UserRole).toUInt();
            switch (col)
            {
                case 0: // Message ID
                    return asc ? (idA < idB) : (idA > idB);
                case 1: // Payload size (bytes)
                {
                    const int va = a->data(1, Qt::UserRole).toInt();
                    const int vb = b->data(1, Qt::UserRole).toInt();
                    if (va != vb)
                    {
                        return asc ? (va < vb) : (va > vb);
                    }
                    return idA < idB;
                }
                case 2: // Rate (Hz)
                {
                    const double va = a->data(2, Qt::UserRole).toDouble();
                    const double vb = b->data(2, Qt::UserRole).toDouble();
                    if (va != vb)
                    {
                        return asc ? (va < vb) : (va > vb);
                    }
                    return idA < idB;
                }
                case 3: // Count
                {
                    const qulonglong va = a->data(3, Qt::UserRole).toULongLong();
                    const qulonglong vb = b->data(3, Qt::UserRole).toULongLong();
                    if (va != vb)
                    {
                        return asc ? (va < vb) : (va > vb);
                    }
                    return idA < idB;
                }
                default:
                    return false;
            }
        });

        device->addChildren(children);
    }

    if (current)
    {
        const QSignalBlocker blocker(rxTree);
        rxTree->setCurrentItem(current);
    }
    if (rxTree->verticalScrollBar())
    {
        rxTree->verticalScrollBar()->setValue(scroll);
    }
}

TelemetryPlotWidget *MainWindow::plotterWidget() const
{
    return telemetryPlotter;
}

void MainWindow::togglePlotField(uint32_t sysid, uint8_t compid, uint32_t msgid, const QString &fieldName, const QString &label)
{
    if (!telemetryPlotter)
    {
        return;
    }
    if (telemetryPlotter->hasChannel(sysid, compid, msgid, fieldName))
    {
        telemetryPlotter->removeChannel(sysid, compid, msgid, fieldName);
        if (telemetryPlotter->channelCount() == 0)
        {
            telemetryPlotter->hide();
            if (plotToggleBtn)
            {
                plotToggleBtn->setChecked(false);
            }
        }
    }
    else
    {
        if (telemetryPlotter->addChannel(sysid, compid, msgid, fieldName, label))
        {
            telemetryPlotter->show();
            if (telemetryPlotter->isFloating())
            {
                telemetryPlotter->raise();
                telemetryPlotter->activateWindow();
            }
            else if (inspectorContentSplitter)
            {
                inspectorContentSplitter->setSizes({350, 550});
            }
            if (plotToggleBtn)
            {
                plotToggleBtn->setChecked(true);
            }
        }
    }
}


void MainWindow::setRxThroughputStats(double bps, double utilization, int baud)
{
    if (!rxThroughput)
    {
        return;
    }
    if (!connected)
    {
        rxThroughput->hide();
        return;
    }
    const QString text = (baud > 0) ?
        QString("%1 (%2%)")
            .arg(QString::fromStdString(Mav::formatByteRate(bps)))
            .arg(utilization, 0, 'f', 1) :
        QString::fromStdString(Mav::formatByteRate(bps));
    rxThroughput->setText(text);
    updateRxThroughputStyle(utilization);
    rxThroughput->show();
}

void MainWindow::updateRxThroughputStyle(double util)
{
    lastRxUtil = util;
    if (!rxThroughput)
    {
        return;
    }
    const bool dark = themeBox && (themeBox->currentIndex() == 2 ||
                      (themeBox->currentIndex() == 0 && qApp->palette().color(QPalette::Window).lightness() < 128));
    if (util > 90.0)
    {
        rxThroughput->setStyleSheet(dark ?
            "color: #f87171; font-size: 11px; font-weight: 600; padding: 2px 10px; background: #2e1215; border: 1px solid #7f1d1d; border-radius: 6px;" :
            "color: #dc2626; font-size: 11px; font-weight: 600; padding: 2px 10px; background: #fef2f2; border: 1px solid #fecaca; border-radius: 6px;");
    }
    else if (util > 70.0)
    {
        rxThroughput->setStyleSheet(dark ?
            "color: #fbbf24; font-size: 11px; font-weight: 600; padding: 2px 10px; background: #2b1a07; border: 1px solid #78350f; border-radius: 6px;" :
            "color: #d97706; font-size: 11px; font-weight: 600; padding: 2px 10px; background: #fffbeb; border: 1px solid #fde68a; border-radius: 6px;");
    }
    else
    {
        rxThroughput->setStyleSheet(dark ?
            "color: #60a5fa; font-size: 11px; font-weight: 600; padding: 2px 10px; background: #0f233a; border: 1px solid #1e3a5f; border-radius: 6px;" :
            "color: #2563eb; font-size: 11px; font-weight: 600; padding: 2px 10px; background: #eff6ff; border: 1px solid #bfdbfe; border-radius: 6px;");
    }
}

void MainWindow::stopRefreshTimer()
{
    refresh.stop();
}

void MainWindow::setStatusMetrics(double rxBps, double txBps, uint64_t crc, uint64_t pending, uint64_t overflow,
                                  double rxUtil, double txUtil, int baud)
{
    if (metricsWidget)
    {
        const bool dark = property("darkTheme").isValid() ?
                          property("darkTheme").toBool() :
                          (themeBox && themeBox->currentIndex() == 2);
        metricsWidget->updateMetrics(rxBps, txBps, crc, pending, overflow, dark, rxUtil, txUtil, baud);
    }
}

/**
 * @brief Open parameter value modification dialog and dispatch PARAM_SET upon confirmation.
 * @param row Index of the parameter row in the inspector table.
 */
void MainWindow::editParameter(int row)
{
    if (!isParamView || row < 0 || row >= inspector->rowCount())
    {
        return;
    }
    auto *nameItem = inspector->item(row, 0);
    auto *typeItem = inspector->item(row, 1);
    auto *valItem = inspector->item(row, 2);
    if (!nameItem || !typeItem || !valItem)
    {
        return;
    }

    const QString paramId = nameItem->text();
    const QString paramTypeStr = typeItem->text();
    const QString currentVal = valItem->text();
    const uint8_t paramType = static_cast<uint8_t>(valItem->data(Qt::UserRole + 10).toUInt());

    QDialog dialog(this);
    dialog.setObjectName("editParamDialog");
    dialog.setWindowTitle(QString("Edit %1").arg(paramId));
    dialog.setMinimumWidth(340);

    auto *dialogLayout = new QVBoxLayout(&dialog);
    dialogLayout->setContentsMargins(20, 16, 20, 16);
    dialogLayout->setSpacing(14);
    dialogLayout = addDialogTitleBar(&dialog, dialogLayout);

    auto *descLabel = new QLabel(QString("Set new value for <b>%1</b> (%2):").arg(paramId, paramTypeStr), &dialog);
    descLabel->setObjectName("secondaryText");
    dialogLayout->addWidget(descLabel);

    auto *inputEdit = new QLineEdit(&dialog);
    inputEdit->setObjectName("paramValueInput");
    inputEdit->setAttribute(Qt::WA_InputMethodEnabled, false);
    inputEdit->setInputMethodHints(Qt::ImhLatinOnly | Qt::ImhNoPredictiveText);
    inputEdit->setText(currentVal);
    inputEdit->selectAll();
    inputEdit->setFixedHeight(34);
    dialogLayout->addWidget(inputEdit);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttonBox->button(QDialogButtonBox::Ok)->setText("Apply");
    buttonBox->button(QDialogButtonBox::Ok)->setProperty("role", "primary");
    buttonBox->button(QDialogButtonBox::Ok)->setFixedHeight(34);
    buttonBox->button(QDialogButtonBox::Cancel)->setFixedHeight(34);
    dialogLayout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted)
    {
        const QString newText = inputEdit->text().trimmed();
        float outWireValue = 0.0f;
        if (!Mav::encodeParamValue(newText.toStdString(), paramType, outWireValue))
        {
            showConnectionError("Invalid Value", QString("Value '%1' is not valid for type %2.").arg(newText, paramTypeStr));
            return;
        }

        QMetaObject::invokeMethod(worker, "setParameter", Qt::QueuedConnection,
                                  Q_ARG(uint8_t, static_cast<uint8_t>(activeParamSysId)),
                                  Q_ARG(uint8_t, activeParamCompId),
                                  Q_ARG(QString, paramId),
                                  Q_ARG(float, outWireValue),
                                  Q_ARG(uint8_t, paramType));

        cell(inspector, row, 2, QString::fromStdString(Mav::formatParamValue(outWireValue, paramType)));
    }
}

void MainWindow::updateInspector()
{
    if (paused || !latest)
    {
        return;
    }
    inspectorTitle->setText("DECODED FIELDS");
    inspectorSource->clear();
    inspectorSource->hide();
    auto *item = rxTree->currentItem();
    if (!item || !item->parent())
    {
        inspector->setRowCount(0);
        copiedInspectorRow = -1;
        if (copyFeedbackBadge)
        {
            copyFeedbackBadge->hide();
        }
        if (paramFilter)
        {
            paramFilter->hide();
        }
        isParamView = false;
        return;
    }
    const uint32_t id = item->data(0, Qt::UserRole).toUInt();
    const uint32_t system = item->parent()->data(0, Qt::UserRole).toUInt();
    const uint8_t component = uint8_t(item->parent()->data(0, Qt::UserRole + 1).toUInt());

    if (id == 22)
    {
        isParamView = true;
        activeParamSysId = system;
        activeParamCompId = component;
        if (paramFilter)
        {
            paramFilter->show();
        }

        const auto devKey = std::make_pair(system, component);
        const auto it = (latest && latest->params) ? latest->params->find(devKey) : Snapshot::ParamMap::const_iterator{};
        const auto &collection = (latest && latest->params && it != latest->params->end()) ? it->second : Mav::ParamCollection{};
        const uint16_t loaded = static_cast<uint16_t>(collection.byId.size());
        const uint16_t total = collection.totalCount;

        if (total > 0)
        {
            inspectorTitle->setText(QString("PARAMETERS (%1 / %2)").arg(loaded).arg(total));
        }
        else
        {
            inspectorTitle->setText(QString("PARAMETERS (%1)").arg(loaded));
        }
        inspectorSource->setText(QString("System %1 · Component %2").arg(system).arg(component));
        inspectorSource->show();

        const QString filterText = paramFilter ? paramFilter->text().trimmed().toLower() : QString();
        std::vector<const Mav::ParamItem *> visibleParams;
        visibleParams.reserve(collection.byId.size());
        for (const auto &[name, param] : collection.byId)
        {
            if (filterText.isEmpty() || QString::fromStdString(name).toLower().contains(filterText))
            {
                visibleParams.push_back(&param);
            }
        }

        inspector->setRowCount(static_cast<int>(visibleParams.size()));
        const bool dark = themeBox->currentIndex() == 2 ||
                          (themeBox->currentIndex() == 0 && qApp->palette().color(QPalette::Window).lightness() < 128);

        for (int i = 0; i < static_cast<int>(visibleParams.size()); ++i)
        {
            const auto *p = visibleParams[i];
            const QString nameStr = QString::fromStdString(p->id);
            const QString typeStr = QString::fromStdString(Mav::paramTypeName(p->type));
            const QString valStr = QString::fromStdString(Mav::formatParamValue(p->value, p->type));

            cell(inspector, i, 0, nameStr);
            cell(inspector, i, 1, typeStr);
            cell(inspector, i, 2, valStr);
            cell(inspector, i, 3, QString("#%1").arg(p->index));

            QFont identityFont = cachedFixedFont();
            identityFont.setPixelSize(13);
            inspector->item(i, 0)->setFont(identityFont);
            identityFont.setPixelSize(12);
            inspector->item(i, 1)->setFont(identityFont);

            auto *valItem = inspector->item(i, 2);
            valItem->setData(Qt::UserRole + 10, p->type);
            valItem->setData(Qt::UserRole + 11, p->value);
            valItem->setForeground(QColor(dark ? "#b2c8e2" : "#3f628b"));
            valItem->setToolTip(QString("%1: %2 (%3) · Double click to edit").arg(nameStr, valStr, typeStr));
        }
        return;
    }

    isParamView = false;
    if (paramFilter)
    {
        paramFilter->hide();
    }

    for (const auto &view : latest->rx)
    {
        const auto &message = view.message;
        if (message.sysid != system || message.compid != component || message.msgid != id)
        {
            continue;
        }
        const auto *info = mavlink_get_message_info_by_id(id);
        if (!info)
        {
            inspector->setRowCount(0);
            copiedInspectorRow = -1;
            if (copyFeedbackBadge)
            {
                copyFeedbackBadge->hide();
            }
            return;
        }
        inspector->setRowCount(int(info->num_fields));
        if (copiedInspectorRow >= int(info->num_fields))
        {
            copiedInspectorRow = -1;
            if (copyFeedbackBadge)
            {
                copyFeedbackBadge->hide();
            }
        }
        inspectorTitle->setText(QString("[%1] %2").arg(id).arg(messageName(id)));
        inspectorSource->setText(QString("System %1 · Component %2").arg(system).arg(component));
        inspectorSource->show();
        const bool dark = themeBox->currentIndex() == 2 ||
                          (themeBox->currentIndex() == 0 && qApp->palette().color(QPalette::Window).lightness() < 128);
        for (unsigned i = 0; i < info->num_fields; ++i)
        {
            const auto &field = info->fields[i];
            cell(inspector, int(i), 0, field.name);
            QString type = QString::fromStdString(Mav::typeName(field.type));
            if (field.array_length)
            {
                type += QString("[%1]").arg(field.array_length);
            }
            cell(inspector, int(i), 1, type);
            QFont identityFont = cachedFixedFont();
            identityFont.setPixelSize(13);
            inspector->item(int(i), 0)->setFont(identityFont);
            identityFont.setPixelSize(12);
            inspector->item(int(i), 1)->setFont(identityFont);
            const QString valStr = QString::fromStdString(Mav::readField(message, field));
            const QString fieldKey = QString("%1.%2.%3.%4").arg(system).arg(component).arg(id).arg(field.name);
            const auto prevIt = lastFieldValues.find(fieldKey);
            const bool changed = (prevIt != lastFieldValues.end()) && (prevIt->second != valStr);
            lastFieldValues[fieldKey] = valStr;
            cell(inspector, int(i), 2, valStr);
            auto *valItem = inspector->item(int(i), 2);
            valItem->setData(Qt::UserRole + 5, changed);
            if (int(i) == copiedInspectorRow)
            {
                valItem->setData(Qt::UserRole + 6, true);
            }
            if (changed)
            {
                valItem->setForeground(QColor(dark ? "#58a6ff" : "#0969da"));
            }
            else
            {
                valItem->setForeground(QColor(dark ? "#b2c8e2" : "#3f628b"));
            }
            const QString hint = mavlinkEnumHint(id, QString::fromLatin1(field.name), valStr);
            valItem->setToolTip(hint.isEmpty() ? valStr : valStr + " · " + hint);
            cell(inspector, int(i), 3, "Unavailable");
        }
        return;
    }
}

void MainWindow::filterRx()
{
    const auto query = rxFilter->text().trimmed();
    int total = 0;
    int matched = 0;
    for (int i = 0; i < rxTree->topLevelItemCount(); ++i)
    {
        auto *device = rxTree->topLevelItem(i);
        bool deviceVisible = false;
        for (int j = 0; j < device->childCount(); ++j)
        {
            auto *item = device->child(j);
            const QString identity = device->text(0) + " " + item->text(0);
            const bool visible = identity.contains(query, Qt::CaseInsensitive);
            if (item->isHidden() == visible)
            {
                item->setHidden(!visible);
            }
            ++total;
            matched += visible;
            deviceVisible |= visible;
        }
        if (device->isHidden() == deviceVisible)
        {
            device->setHidden(!deviceVisible);
        }
    }
    rxSummary->setText(query.isEmpty() ? QString("%1 %2").arg(total).arg(total == 1 ? "stream" : "streams") :
                       QString("%1 / %2 streams").arg(matched).arg(total));
}

/**
 * @brief Toggle expansion state of all RX device nodes.
 *
 * If any top-level item is currently expanded, collapses all items.
 * If all items are collapsed, expands all items.
 */
void MainWindow::toggleExpandCollapseRx()
{
    if (!rxTree || rxTree->topLevelItemCount() == 0)
    {
        return;
    }
    bool anyExpanded = false;
    for (int i = 0; i < rxTree->topLevelItemCount(); ++i)
    {
        if (rxTree->topLevelItem(i)->isExpanded())
        {
            anyExpanded = true;
            break;
        }
    }
    if (anyExpanded)
    {
        rxTree->collapseAll();
    }
    else
    {
        rxTree->expandAll();
    }
    updateRxExpandIcon();
}

/**
 * @brief Update the toggle expand/collapse button icon and tooltip based on RX tree state.
 */
void MainWindow::updateRxExpandIcon()
{
    if (!toggleExpandRx || !rxTree)
    {
        return;
    }
    const bool hasMessages = rxTree->topLevelItemCount() > 0;
    bool anyExpanded = false;
    for (int i = 0; i < rxTree->topLevelItemCount(); ++i)
    {
        if (rxTree->topLevelItem(i)->isExpanded())
        {
            anyExpanded = true;
            break;
        }
    }
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    const QColor iconColor = dark ? QColor("#e2e8f0") : QColor("#334155");
    const QColor disabledColor = dark ? QColor("#526079") : QColor("#94a3b8");
    const QString iconName = anyExpanded ? "collapseAll" : "expandAll";
    const QString toolTip = anyExpanded ? "Collapse all (Ctrl+E)" : "Expand all (Ctrl+E)";

    QIcon icon = actionIcon(iconName, iconColor);
    icon.addPixmap(actionIcon(iconName, disabledColor).pixmap(18, 18), QIcon::Disabled);
    toggleExpandRx->setIcon(icon);
    toggleExpandRx->setIconSize(QSize(18, 18));
    toggleExpandRx->setToolTip(toolTip);
    toggleExpandRx->setEnabled(hasMessages);
    if (toggleExpandRxAction)
    {
        toggleExpandRxAction->setText(anyExpanded ? "Collapse All Messages" : "Expand All Messages");
        toggleExpandRxAction->setEnabled(hasMessages);
    }
}



