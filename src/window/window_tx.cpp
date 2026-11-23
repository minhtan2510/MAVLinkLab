/**
 * @file window_tx.cpp
 * @brief MainWindow TX message dashboard, drag-and-drop, animations, and undo system.
 */

#include "window.h"
#include "window_private.h"
#include <QPainter>
#include <QToolTip>
#include <QClipboard>
#include <QApplication>
#include <QKeyEvent>
#include <QSignalBlocker>
#include <QMessageBox>
#include <QScrollBar>

/** @brief Display a popup menu with preset frequencies and custom rate input. */
void MainWindow::showQuickRateMenu(quint64 key, double currentHz, const QPoint &globalPos)
{
    auto *menu = new WorkbenchMenu(this);
    menu->setObjectName("quickRateMenu");
    menu->setAttribute(Qt::WA_DeleteOnClose);
    configureTranslucentMenu(menu);
    menu->installEventFilter(this);
    const std::vector<double> presets = {0.5, 1.0, 2.0, 5.0, 10.0, 20.0, 50.0, 100.0};
    for (double hz : presets)
    {
        const QString label = QString("%1 Hz").arg(hz, 0, 'f', (hz < 1.0) ? 1 : 0);
        auto *action = menu->addAction(label, this, [this, key, hz]()
        {
            QMetaObject::invokeMethod(worker, "setRate", Qt::QueuedConnection,
                                      Q_ARG(quint64, key), Q_ARG(double, hz));
            saveTxMessages();
        });
        if (std::abs(hz - currentHz) < 0.05)
        {
            action->setCheckable(true);
            action->setChecked(true);
        }
    }
    menu->addSeparator();
    menu->addAction("Custom…", this, [this, key, currentHz]()
    {
        QDialog dialog(this);
        dialog.setObjectName("customRateDialog");
        dialog.setWindowTitle("Custom Rate");
        dialog.setMinimumWidth(320);
        auto *dialogLayout = new QVBoxLayout(&dialog);
        dialogLayout->setContentsMargins(20, 16, 20, 16);
        dialogLayout->setSpacing(14);
        dialogLayout = addDialogTitleBar(&dialog, dialogLayout);

        auto *label = new QLabel("Target transmission rate (Hz):", &dialog);
        label->setObjectName("secondaryText");
        dialogLayout->addWidget(label);

        auto *spin = new WorkbenchSpinBox<QSpinBox>(&dialog);
        spin->setObjectName("customRateSpin");
        spin->setRange(1, 1000);
        spin->setValue(std::max(1, static_cast<int>(std::round(currentHz))));
        spin->setMinimumHeight(36);
        spin->selectAll();
        dialogLayout->addWidget(spin);

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        buttons->button(QDialogButtonBox::Ok)->setText("Apply");
        buttons->button(QDialogButtonBox::Ok)->setProperty("role", "primary");
        buttons->button(QDialogButtonBox::Ok)->setMinimumHeight(36);
        buttons->button(QDialogButtonBox::Cancel)->setMinimumHeight(36);
        dialogLayout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        if (dialog.exec() == QDialog::Accepted)
        {
            const double newHz = static_cast<double>(spin->value());
            QMetaObject::invokeMethod(worker, "setRate", Qt::QueuedConnection,
                                      Q_ARG(quint64, key), Q_ARG(double, newHz));
            saveTxMessages();
        }
    });
    menu->popup(globalPos);
}

/** @brief Display transient confirmation badge and cell flash when copying a field value. */
void MainWindow::showCopiedFeedback(int row)
{
    if (!inspector || row < 0 || row >= inspector->rowCount())
    {
        return;
    }
    auto *valItem = inspector->item(row, 2);
    if (!valItem)
    {
        return;
    }

    if (copiedInspectorRow >= 0 && copiedInspectorRow < inspector->rowCount() && copiedInspectorRow != row)
    {
        if (auto *prev = inspector->item(copiedInspectorRow, 2))
        {
            prev->setData(Qt::UserRole + 6, false);
        }
    }

    copiedInspectorRow = row;
    valItem->setData(Qt::UserRole + 6, true);

    if (!copyFeedbackBadge)
    {
        copyFeedbackBadge = new CopyFeedbackBadge(inspector->viewport());
    }
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    static_cast<CopyFeedbackBadge *>(copyFeedbackBadge)->refreshTheme(dark);
    copyFeedbackBadge->setText("✓ Copied");
    copyFeedbackBadge->adjustSize();

    const QRect cellRect = inspector->visualItemRect(valItem);
    const QRect input(cellRect.left() + 8, cellRect.top() + 6, cellRect.width() - 16, 28);
    const int badgeWidth = copyFeedbackBadge->width();
    const int badgeHeight = 22;
    const int x = input.right() - badgeWidth - 4;
    const int y = input.top() + (input.height() - badgeHeight) / 2;
    copyFeedbackBadge->setGeometry(x, y, badgeWidth, badgeHeight);
    copyFeedbackBadge->show();
    copyFeedbackBadge->raise();

    inspector->viewport()->update();

    if (!copyFeedbackTimer)
    {
        copyFeedbackTimer = new QTimer(this);
        copyFeedbackTimer->setSingleShot(true);
        connect(copyFeedbackTimer, &QTimer::timeout, this, [this]()
        {
            if (copiedInspectorRow >= 0 && copiedInspectorRow < inspector->rowCount())
            {
                if (auto *item = inspector->item(copiedInspectorRow, 2))
                {
                    item->setData(Qt::UserRole + 6, false);
                }
            }
            copiedInspectorRow = -1;
            if (copyFeedbackBadge)
            {
                copyFeedbackBadge->hide();
            }
            if (inspector && inspector->viewport())
            {
                inspector->viewport()->update();
            }
        });
    }
    copyFeedbackTimer->start(800);
}

/**
 * @brief Start interactive drag operation for a TX message card.
 * @param row Index of the row being dragged.
 * @param globalPos Initial mouse position in global coordinates.
 */
void MainWindow::startTxDrag(int row, const QPoint &globalPos)
{
    if (!txTable || row < 0 || row >= txTable->rowCount())
    {
        return;
    }
    const QModelIndex idx = txTable->model()->index(row, 1);
    const QRect cellRect = txTable->visualRect(idx);
    const QRect cardRect = txCardRect(cellRect);

    // Grab the fully-rendered card content BEFORE setting dragging state
    txTable->setProperty("isDragging", false);
    QPixmap grabPix = txTable->viewport()->grab(cardRect);

    isDraggingTx = true;
    dragSourceRow = row;
    dragTargetRow = row;
    lastDragGlobalPos = globalPos;
    isHoveringTrash = false;

    finalizeTxDeleteAnimation();
    if (dragDeleteShrinkTimer && dragDeleteShrinkTimer->isActive())
    {
        dragDeleteShrinkTimer->stop();
    }
    if (txDuplicateAnimTimer && txDuplicateAnimTimer->isActive())
    {
        txDuplicateAnimTimer->stop();
        txTable->setProperty("duplicateAnimRow", -1);
        txTable->setProperty("duplicateAnimOffset", 0.0);
        txTable->setProperty("duplicateAnimProgress", 1.0);
    }
    pendingDeleteKey = 0;

    if (trashDropCard && deleteAllButton)
    {
        QWidget *parent = deleteAllButton->parentWidget();
        const QPoint pos = parent ? deleteAllButton->mapTo(parent, QPoint(0, 0)) : deleteAllButton->pos();
        trashDropCard->setParent(parent);

        auto *stopBtn = findChild<QPushButton *>("stopAll");
        auto *addBtn = findChild<QPushButton *>("addMessage");
        if (stopBtn && addBtn)
        {
            const QPoint stopPos = parent ? stopBtn->mapTo(parent, QPoint(0, 0)) : stopBtn->pos();
            const QPoint addPos = parent ? addBtn->mapTo(parent, QPoint(0, 0)) : addBtn->pos();
            const int initialGap = std::max(pos.x() - (stopPos.x() + stopBtn->width()), 6);
            const int shiftLeft = initialGap / 2;
            const int finalGap = initialGap - shiftLeft;
            const int targetLeft = pos.x() - shiftLeft;
            const int targetRight = addPos.x() - finalGap;
            const int targetWidth = std::max(targetRight - targetLeft, 120);
            const int centerY = pos.y() + deleteAllButton->height() / 2;
            trashDropCard->startExpandFromLeft(pos, centerY, targetLeft, targetWidth);
        }
        else
        {
            trashDropCard->startExpand(pos);
        }
        deleteAllButton->hide();
    }

    txTable->setProperty("isDragging", true);
    txTable->setProperty("dragSourceRow", dragSourceRow);
    txTable->setProperty("dragTargetRow", dragTargetRow);

    const int shadowMargin = 16;
    const QSize ghostSize(cardRect.width() + shadowMargin * 2, cardRect.height() + shadowMargin * 2);
    dragGhostNormal = QPixmap(ghostSize);
    dragGhostNormal.fill(Qt::transparent);
    {
        QPainter p(&dragGhostNormal);
        p.setRenderHint(QPainter::Antialiasing);

        // 1. Soft multi-layer elevation shadow (blur radius ~14px, offset Y+6px)
        const QRect shadowBase(shadowMargin, shadowMargin + 6, cardRect.width(), cardRect.height());
        for (int step = shadowMargin; step >= 2; step -= 2)
        {
            const int alpha = int(18.0 * (1.0 - double(step) / shadowMargin));
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, alpha));
            p.drawRoundedRect(shadowBase.adjusted(-step, -step, step, step), 8 + step, 8 + step);
        }

        // 2. Draw card with rounded clip to keep corners transparent
        p.save();
        QPainterPath clipPath;
        clipPath.addRoundedRect(QRectF(shadowMargin, shadowMargin, cardRect.width(), cardRect.height()), 8, 8);
        p.setClipPath(clipPath);
        p.setOpacity(0.96);
        p.drawPixmap(shadowMargin, shadowMargin, grabPix);
        p.restore();

        // 3. Crisp accent border highlight (Modern 3D lift)
        const bool dark = txTable->property("darkTheme").toBool();
        p.setOpacity(1.0);
        p.setPen(QPen(QColor(dark ? "#60a5fa" : "#2563eb"), 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRect(shadowMargin, shadowMargin, cardRect.width(), cardRect.height()), 8, 8);
    }

    dragGhostDanger = QPixmap(ghostSize);
    dragGhostDanger.fill(Qt::transparent);
    {
        QPainter p(&dragGhostDanger);
        p.setRenderHint(QPainter::Antialiasing);

        // 1. Soft shadow with warm red danger glow
        const QRect shadowBase(shadowMargin, shadowMargin + 6, cardRect.width(), cardRect.height());
        for (int step = shadowMargin; step >= 2; step -= 2)
        {
            const int alpha = int(18.0 * (1.0 - double(step) / shadowMargin));
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(180, 20, 40, alpha));
            p.drawRoundedRect(shadowBase.adjusted(-step, -step, step, step), 8 + step, 8 + step);
        }

        // 2. Draw card with rounded clip, subtle optical soft-blur, and milky frosted glass overlay (~51% opacity)
        const bool dark = txTable->property("darkTheme").toBool();
        const QRectF cardTargetRect(shadowMargin, shadowMargin, cardRect.width(), cardRect.height());

        // Subtle optical soft-blur (~1.4x factor) softens text and icon edges while preserving letterform legibility
        QImage softenedImg = grabPix.toImage();
        if (!softenedImg.isNull() && softenedImg.width() > 16 && softenedImg.height() > 16)
        {
            const int softW = std::max(16, int(softenedImg.width() * 0.70));
            const int softH = std::max(16, int(softenedImg.height() * 0.70));
            softenedImg = softenedImg.scaled(softW, softH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                     .scaled(cardRect.width(), cardRect.height(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
        const QPixmap softenedPix = QPixmap::fromImage(softenedImg);

        p.save();
        QPainterPath clipPath;
        clipPath.addRoundedRect(cardTargetRect, 8, 8);
        p.setClipPath(clipPath);

        // Draw subtly softened card content with balanced opacity (88%)
        p.setOpacity(0.88);
        p.drawPixmap(shadowMargin, shadowMargin, softenedPix);

        // Overlay dense milky frosted glass haze (~51% opacity: alpha 130-135/255) for a cloudy translucent look
        p.setOpacity(1.0);
        p.setPen(Qt::NoPen);
        const QColor frostedMask = dark ? QColor(56, 28, 34, 135)     // Dark dense smoky red frosted haze
                                        : QColor(255, 245, 245, 130); // Light milky frosted haze
        p.setBrush(frostedMask);
        p.drawRoundedRect(cardTargetRect, 8, 8);
        p.restore();

        // 3. Crisp danger red border (thin 1.2px)
        p.setOpacity(0.85);
        p.setPen(QPen(QColor("#ef4444"), 1.2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRect(shadowMargin, shadowMargin, cardRect.width(), cardRect.height()), 8, 8);
    }

    if (!dragGhostLabel)
    {
        dragGhostLabel = new QLabel(nullptr);
        dragGhostLabel->setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
        dragGhostLabel->setAttribute(Qt::WA_ShowWithoutActivating);
        dragGhostLabel->setAttribute(Qt::WA_TranslucentBackground);
        dragGhostLabel->setStyleSheet("background: transparent;");
    }
    dragGhostLabel->setPixmap(dragGhostNormal);
    dragGhostLabel->resize(dragGhostNormal.size());

    const QPoint vpPos = txTable->viewport()->mapFromGlobal(globalPos);
    lastDragVpPos = vpPos;
    ghostOffset = (vpPos - cardRect.topLeft()) + QPoint(shadowMargin, shadowMargin);
    dragGhostLabel->move(globalPos - ghostOffset);
    dragGhostLabel->show();

    txTable->viewport()->setCursor(Qt::ClosedHandCursor);
    txTable->viewport()->update();
}

/**
 * @brief Update ghost preview and target drop row while dragging.
 * @param globalPos Current mouse position in global coordinates.
 * @param viewportPos Current mouse position in txTable viewport coordinates.
 */
void MainWindow::updateTxDrag(const QPoint &globalPos, const QPoint &viewportPos)
{
    if (!isDraggingTx || !txTable)
    {
        return;
    }
    lastDragGlobalPos = globalPos;
    lastDragVpPos = viewportPos;

    if (dragGhostLabel)
    {
        dragGhostLabel->move(globalPos - ghostOffset);
    }

    // Check if hovering over the trash drop zone
    bool hoveringTrash = false;
    if (trashDropCard && trashDropCard->isVisible())
    {
        const int effectiveW = std::max(trashDropCard->width(), trashDropCard->fullWidth());
        const int effectiveH = std::max(trashDropCard->height(), trashDropCard->fullHeight());
        const QPoint globalTopLeft = trashDropCard->mapToGlobal(QPoint(0, (trashDropCard->height() - effectiveH) / 2));
        const QRect trashGlobal(globalTopLeft, QSize(effectiveW, effectiveH));
        hoveringTrash = trashGlobal.contains(globalPos);
    }

    if (hoveringTrash != isHoveringTrash)
    {
        isHoveringTrash = hoveringTrash;
        if (trashDropCard)
        {
            trashDropCard->setHovered(isHoveringTrash);
        }
        if (dragGhostLabel)
        {
            dragGhostLabel->setPixmap(isHoveringTrash ? dragGhostDanger : dragGhostNormal);
        }
    }

    if (isHoveringTrash)
    {
        // When hovering over trash, clear placeholder slot in table
        if (dragTargetRow != -1)
        {
            dragTargetRow = -1;
            txTable->setProperty("dragTargetRow", -1);
            txTable->viewport()->update();
        }
        if (dragAutoScrollTimer && dragAutoScrollTimer->isActive())
        {
            dragAutoScrollTimer->stop();
        }
        return;
    }

    int newTargetRow = -1;
    const QModelIndex idx = txTable->indexAt(viewportPos);
    if (idx.isValid() && idx.row() < txTable->rowCount())
    {
        newTargetRow = idx.row();
    }
    else if (viewportPos.y() < 0)
    {
        newTargetRow = 0;
    }
    else if (viewportPos.y() >= txTable->viewport()->height() && txTable->rowCount() > 0)
    {
        newTargetRow = txTable->rowCount() - 1;
    }

    if (newTargetRow >= 0 && newTargetRow != dragTargetRow)
    {
        const int prevTarget = dragTargetRow;
        dragTargetRow = newTargetRow;
        txTable->setProperty("dragTargetRow", dragTargetRow);

        // Trigger snappy sliding animation
        txTable->setProperty("dragAnimFrom", prevTarget);
        txTable->setProperty("dragAnimTo", dragTargetRow);
        const double initialOffset = (prevTarget < dragTargetRow) ? 35.0 : -35.0;
        txTable->setProperty("dragAnimOffset", initialOffset);
        if (dragAnimTimer && !dragAnimTimer->isActive())
        {
            dragAnimTimer->start(8);
        }
        txTable->viewport()->update();
    }

    const int scrollMargin = 30;
    if (viewportPos.y() < scrollMargin)
    {
        dragAutoScrollSpeed = -1;
        if (dragAutoScrollTimer && !dragAutoScrollTimer->isActive())
        {
            dragAutoScrollTimer->start(40);
        }
    }
    else if (viewportPos.y() > txTable->viewport()->height() - scrollMargin)
    {
        dragAutoScrollSpeed = 1;
        if (dragAutoScrollTimer && !dragAutoScrollTimer->isActive())
        {
            dragAutoScrollTimer->start(40);
        }
    }
    else
    {
        dragAutoScrollSpeed = 0;
        if (dragAutoScrollTimer && dragAutoScrollTimer->isActive())
        {
            dragAutoScrollTimer->stop();
        }
    }
}

/**
 * @brief Finalize or cancel the active drag operation.
 * @param commit True to apply the new row order to the worker; false to revert.
 */
void MainWindow::finishTxDrag(bool commit)
{
    if (dragAutoScrollTimer)
    {
        dragAutoScrollTimer->stop();
    }
    if (dragAnimTimer)
    {
        dragAnimTimer->stop();
    }
    if (txTable)
    {
        txTable->viewport()->setCursor(Qt::ArrowCursor);
    }

    const int from = dragSourceRow;
    const int to = dragTargetRow;
    const bool deleteRequested = isHoveringTrash;

    isDraggingTx = false;
    dragHandlePressed = false;
    dragSourceRow = -1;
    dragTargetRow = -1;
    isHoveringTrash = false;

    if (txTable)
    {
        txTable->setProperty("isDragging", false);
        txTable->setProperty("dragSourceRow", -1);
        txTable->setProperty("dragTargetRow", -1);
        txTable->setProperty("dragAnimOffset", 0.0);
        txTable->setProperty("dragAnimFrom", -1);
        txTable->setProperty("dragAnimTo", -1);
        txTable->clearSelection();
        txTable->viewport()->update();
    }

    if (commit && deleteRequested && from >= 0 && txTable)
    {
        const uint64_t key = txTable->item(from, 0) ? txTable->item(from, 0)->data(Qt::UserRole).toULongLong() : 0;
        if (key != 0 && dragGhostLabel && dragGhostLabel->isVisible() && trashDropCard && trashDropCard->isVisible())
        {
            pendingDeleteKey = key;
            dragDeleteStartRect = dragGhostLabel->geometry();
            dragDeleteTargetCenter = trashDropCard->mapToGlobal(QPoint(14, trashDropCard->height() / 2));
            dragDeleteShrinkStep = 0;
            dragDeleteShrinkTimerElapsed.restart();
            if (!dragDeleteShrinkTimer)
            {
                dragDeleteShrinkTimer = new QTimer(this);
                dragDeleteShrinkTimer->setTimerType(Qt::PreciseTimer);
                connect(dragDeleteShrinkTimer, &QTimer::timeout, this, [this]()
                {
                    stepTxDeleteShrink();
                });
            }
            dragDeleteShrinkTimer->start(8);
            return;
        }

        if (key != 0 && worker)
        {
            Mav::TxEntry entryToRestore;
            bool found = false;
            if (latest)
            {
                for (const auto &txView : latest->tx)
                {
                    if (txView.entry.key == key)
                    {
                        entryToRestore = txView.entry;
                        found = true;
                        break;
                    }
                }
            }
            QMetaObject::invokeMethod(worker, "removeEntry", Qt::QueuedConnection, Q_ARG(quint64, key));
            saveTxMessages();
            if (found)
            {
                showTxUndoToast(entryToRestore, dragSourceRow);
            }
        }
        if (dragGhostLabel)
        {
            dragGhostLabel->hide();
        }
        if (trashDropCard)
        {
            trashDropCard->startCollapse([this]()
            {
                if (deleteAllButton)
                {
                    deleteAllButton->show();
                }
            });
        }
        else if (deleteAllButton)
        {
            deleteAllButton->show();
        }
    }
    else
    {
        if (dragGhostLabel)
        {
            dragGhostLabel->hide();
        }
        if (trashDropCard)
        {
            trashDropCard->startCollapse([this]()
            {
                if (deleteAllButton)
                {
                    deleteAllButton->show();
                }
            });
        }
        else if (deleteAllButton)
        {
            deleteAllButton->show();
        }

        if (commit && from >= 0 && to >= 0 && from != to && worker)
        {
            resetSortIndicator();
            QMetaObject::invokeMethod(worker, "moveEntry", Qt::QueuedConnection,
                                      Q_ARG(int, from), Q_ARG(int, to));
            saveTxMessages();
        }
    }
}

/**
 * @brief Advance the shrink-and-fade animation sucking the dragged card into the trash can.
 */
void MainWindow::stepTxDeleteShrink()
{
    const qint64 elapsed = dragDeleteShrinkTimerElapsed.elapsed();
    const double duration = 220.0;
    const double t = std::clamp(double(elapsed) / duration, 0.0, 1.0);
    if (t >= 1.0)
    {
        if (dragDeleteShrinkTimer)
        {
            dragDeleteShrinkTimer->stop();
        }
        if (dragGhostLabel)
        {
            dragGhostLabel->hide();
        }
        if (pendingDeleteKey != 0 && worker)
        {
            Mav::TxEntry entryToRestore;
            bool found = false;
            if (latest)
            {
                for (const auto &txView : latest->tx)
                {
                    if (txView.entry.key == pendingDeleteKey)
                    {
                        entryToRestore = txView.entry;
                        found = true;
                        break;
                    }
                }
            }
            QMetaObject::invokeMethod(worker, "removeEntry", Qt::QueuedConnection, Q_ARG(quint64, pendingDeleteKey));
            saveTxMessages();
            if (found)
            {
                showTxUndoToast(entryToRestore, dragSourceRow);
            }
            pendingDeleteKey = 0;
        }
        if (trashDropCard)
        {
            trashDropCard->startCollapse([this]()
            {
                if (deleteAllButton)
                {
                    deleteAllButton->show();
                }
            });
        }
        return;
    }

    if (!dragGhostLabel)
    {
        return;
    }

    const double ease = t * t;
    const double scale = 1.0 - ease * 0.85;
    const int newW = std::max(20, int(dragDeleteStartRect.width() * scale));
    const int newH = std::max(10, int(dragDeleteStartRect.height() * scale));
    const int curCenterX = int(dragDeleteStartRect.center().x() + (dragDeleteTargetCenter.x() - dragDeleteStartRect.center().x()) * ease);
    const int curCenterY = int(dragDeleteStartRect.center().y() + (dragDeleteTargetCenter.y() - dragDeleteStartRect.center().y()) * ease);
    const QRect curRect(curCenterX - newW / 2, curCenterY - newH / 2, newW, newH);
    dragGhostLabel->setGeometry(curRect);

    QPixmap scaled = dragGhostDanger.scaled(newW, newH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QPixmap faded(scaled.size());
    faded.fill(Qt::transparent);
    {
        QPainter p(&faded);
        p.setOpacity(std::clamp(1.0 - t, 0.0, 1.0));
        p.drawPixmap(0, 0, scaled);
    }
    dragGhostLabel->setPixmap(faded);
}

/** @brief Cancel the active drag operation and revert card positions. */
void MainWindow::cancelTxDrag()
{
    finishTxDrag(false);
}


/**
 * @brief Display a transient undo toast at the bottom-left corner when an entry is deleted.
 * @param entry The full TxEntry that was removed.
 * @param rowIndex The zero-based row index before removal.
 */
void MainWindow::showTxUndoToast(const Mav::TxEntry &entry, int rowIndex)
{
    lastDeletedTxEntry = entry;
    lastDeletedTxRowIndex = rowIndex;
    txUndoStack.push_back({entry, rowIndex});
    if (txUndoStack.size() > 64)
    {
        txUndoStack.erase(txUndoStack.begin());
    }

    if (!txUndoToast)
    {
        txUndoToast = new WorkbenchToast(this);
        txUndoToast->setOnUndoClicked([this]()
        {
            executeTxUndo();
        });
    }
    const bool dark = themeBox && (themeBox->currentIndex() == 2 ||
                      (themeBox->currentIndex() == 0 && qApp->palette().color(QPalette::Window).lightness() < 128));
    txUndoToast->updateThemeStyle(dark);
    repositionTxUndoToast();

    if (txUndoToast->isVisible())
    {
        txUndoToast->resetCountdown();
    }
    else
    {
        txUndoToast->showToast(QString("Deleted [%1]").arg(entry.id));
    }
}

/**
 * @brief Restore the last deleted TX entry at its exact original row position.
 */
void MainWindow::executeTxUndo()
{
    if (txDeleteAnimKey != 0)
    {
        if (!txUndoStack.empty() && txDeleteAnimKey == txUndoStack.back().entry.key)
        {
            if (txDeleteAnimTimer && txDeleteAnimTimer->isActive())
            {
                txDeleteAnimTimer->stop();
            }
            if (txTable)
            {
                txTable->setProperty("deleteAnimRow", -1);
                txTable->setProperty("deleteAnimOffset", 0.0);
                txTable->setProperty("deleteAnimProgress", 0.0);
                txTable->viewport()->update();
            }
            txDeleteAnimKey = 0;
            txDeleteAnimRow = -1;
            txUndoStack.pop_back();
            lastDeletedTxEntry = txUndoStack.empty() ? Mav::TxEntry{} : txUndoStack.back().entry;
            lastDeletedTxRowIndex = txUndoStack.empty() ? -1 : txUndoStack.back().rowIndex;
            if (txUndoToast)
            {
                txUndoToast->dismiss();
            }
            return;
        }
        finalizeTxDeleteAnimation();
    }

    if (txUndoStack.empty() || !worker)
    {
        return;
    }

    const DeletedTxItem item = txUndoStack.back();
    txUndoStack.pop_back();
    lastDeletedTxEntry = txUndoStack.empty() ? Mav::TxEntry{} : txUndoStack.back().entry;
    lastDeletedTxRowIndex = txUndoStack.empty() ? -1 : txUndoStack.back().rowIndex;

    if (item.entry.id == 0 || item.rowIndex < 0)
    {
        return;
    }

    const int row = item.rowIndex;
    pendingDeleteKey = 0;
    pendingSelectionKey = item.entry.key;
    QMetaObject::invokeMethod(worker, "restoreEntry", Qt::QueuedConnection,
                              Q_ARG(int, row), Q_ARG(Mav::TxEntry, item.entry));
    saveTxMessages();

    if (txUndoToast)
    {
        txUndoToast->dismiss();
    }
}

/**
 * @brief Reposition the undo toast anchored to the bottom-left of the window.
 */
void MainWindow::repositionTxUndoToast()
{
    if (!txUndoToast)
    {
        return;
    }
    const int targetX = 36;
    const int statusHeight = statusBar() && statusBar()->isVisible() ? statusBar()->height() : 24;
    const int targetY = height() - statusHeight - txUndoToast->height() - 24;
    txUndoToast->setTargetPosition(QPoint(targetX, targetY));
}

/**
 * @brief Delete a TX message card at the specified row and trigger the undo toast.
 * @param row Index of the row to delete.
 */
void MainWindow::deleteTxCardAt(int row)
{
    if (!txTable || row < 0 || row >= txTable->rowCount() || !worker)
    {
        return;
    }
    const QTableWidgetItem *item = txTable->item(row, 0);
    if (!item)
    {
        return;
    }
    const uint64_t key = item->data(Qt::UserRole).toULongLong();
    if (key == 0)
    {
        return;
    }

    Mav::TxEntry entryToRestore;
    bool found = false;
    if (latest)
    {
        for (const auto &txView : latest->tx)
        {
            if (txView.entry.key == key)
            {
                entryToRestore = txView.entry;
                found = true;
                break;
            }
        }
    }

    if (found)
    {
        showTxUndoToast(entryToRestore, row);
    }

    startTxDeleteAnimation(row, key);
}

/**
 * @brief Finalize any active row deletion animation immediately.
 */
void MainWindow::finalizeTxDeleteAnimation()
{
    if (txDeleteAnimTimer && txDeleteAnimTimer->isActive())
    {
        txDeleteAnimTimer->stop();
    }
    if (txTable)
    {
        txTable->setProperty("deleteAnimRow", -1);
        txTable->setProperty("deleteAnimOffset", 0.0);
        txTable->setProperty("deleteAnimProgress", 0.0);
    }
    if (txDeleteAnimKey != 0 && worker)
    {
        const uint64_t keyToRemove = txDeleteAnimKey;
        const int deletedRow = txDeleteAnimRow;
        txDeleteAnimKey = 0;
        txDeleteAnimRow = -1;
        pendingDeleteKey = keyToRemove;

        if (txTable && deletedRow >= 0 && deletedRow < txTable->rowCount())
        {
            txTable->removeRow(deletedRow);
            updateTxHeights();
        }

        QMetaObject::invokeMethod(worker, "removeEntry", Qt::QueuedConnection, Q_ARG(quint64, keyToRemove));
        saveTxMessages();

        if (txTable && txTable->rowCount() > 0)
        {
            const int nextRow = (deletedRow >= 0 && deletedRow < txTable->rowCount()) ? deletedRow : (txTable->rowCount() - 1);
            txTable->selectRow(nextRow);
            txTable->setCurrentCell(nextRow, 1);
        }
        else if (txTable)
        {
            txTable->clearSelection();
            txTable->setCurrentItem(nullptr);
        }
        updateActions();
    }
    if (txTable)
    {
        txTable->viewport()->update();
    }
}

/**
 * @brief Start smooth 220ms tuck-up collapse animation for row deletion (reverse duplicate).
 * @param row Index of the card row being deleted.
 * @param key Identifier of the message entry being deleted.
 */
void MainWindow::startTxDeleteAnimation(int row, uint64_t key)
{
    if (!txTable || row < 0 || row >= txTable->rowCount() || key == 0)
    {
        return;
    }
    if (txDeleteAnimKey != 0)
    {
        finalizeTxDeleteAnimation();
    }
    if (txDuplicateAnimTimer && txDuplicateAnimTimer->isActive())
    {
        txDuplicateAnimTimer->stop();
        txTable->setProperty("duplicateAnimRow", -1);
        txTable->setProperty("duplicateAnimOffset", 0.0);
        txTable->setProperty("duplicateAnimProgress", 1.0);
    }
    if (txSwapAnimTimer && txSwapAnimTimer->isActive())
    {
        txSwapAnimTimer->stop();
        txTable->setProperty("swapRowA", -1);
        txTable->setProperty("swapRowB", -1);
        txTable->setProperty("swapOffset", 0.0);
    }

    txDeleteAnimRow = row;
    txDeleteAnimKey = key;
    txDeleteAnimRowHeight = double(txTable->rowHeight(row) > 0 ? txTable->rowHeight(row) : 74);

    txTable->setProperty("deleteAnimRow", row);
    txTable->setProperty("deleteAnimOffset", 0.0);
    txTable->setProperty("deleteAnimProgress", 0.0);

    txDeleteAnimElapsed.restart();
    if (!txDeleteAnimTimer)
    {
        txDeleteAnimTimer = new QTimer(this);
        txDeleteAnimTimer->setTimerType(Qt::PreciseTimer);
        connect(txDeleteAnimTimer, &QTimer::timeout, this, [this]()
        {
            const qint64 elapsed = txDeleteAnimElapsed.elapsed();
            const double duration = 220.0;
            const double t = std::clamp(double(elapsed) / duration, 0.0, 1.0);
            const double ease = 1.0 - std::pow(1.0 - t, 2.0);
            const double curOffset = -ease * txDeleteAnimRowHeight;

            txTable->setProperty("deleteAnimOffset", curOffset);
            txTable->setProperty("deleteAnimProgress", ease);
            txTable->viewport()->update();

            if (t >= 1.0)
            {
                finalizeTxDeleteAnimation();
            }
        });
    }
    txDeleteAnimTimer->start(8);
    txTable->viewport()->update();
}

/**
 * @brief Handle keyboard navigation and manipulation shortcuts for TX cards.
 * @param event The key event to process.
 * @return True if the key was handled and consumed; false otherwise.
 */
bool MainWindow::handleTxKeyPress(QKeyEvent *event)
{
    if (!event)
    {
        return false;
    }

    // Never intercept keyboard events while user is typing in text inputs or spinboxes
    QWidget *focused = QApplication::focusWidget();
    const bool inInput = (qobject_cast<QLineEdit *>(focused) != nullptr ||
                          qobject_cast<QTextEdit *>(focused) != nullptr ||
                          qobject_cast<QPlainTextEdit *>(focused) != nullptr ||
                          qobject_cast<QAbstractSpinBox *>(focused) != nullptr);
    if (inInput)
    {
        return false;
    }

    // Never intercept when a modal dialog is open (e.g. Message Editor)
    if (QApplication::activeModalWidget() != nullptr)
    {
        return false;
    }

    // F1: Open Keyboard Shortcuts reference dialog
    if (event->key() == Qt::Key_F1 && event->modifiers() == Qt::NoModifier)
    {
        showKeyboardShortcuts();
        return true;
    }

    // Ctrl+Z: Undo last deleted card if undo stack has items
    if (event->modifiers() == Qt::ControlModifier && event->key() == Qt::Key_Z)
    {
        if (!txUndoStack.empty() || txDeleteAnimKey != 0)
        {
            executeTxUndo();
            return true;
        }
    }

    const quint64 key = selectedTx();
    if (key == 0 || !txTable)
    {
        return false;
    }
    const int row = txTable->currentRow();
    if (row < 0 || row >= txTable->rowCount())
    {
        return false;
    }

    // Delete / Backspace / Ctrl+X: delete selected card immediately with Undo toast
    const bool isDeleteKey = (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) &&
                             (event->modifiers() == Qt::NoModifier);
    const bool isCtrlX = (event->modifiers() & Qt::ControlModifier) && (event->key() == Qt::Key_X);
    if (isDeleteKey || isCtrlX)
    {
        deleteTxCardAt(row);
        return true;
    }

    // Ctrl+D: duplicate selected card
    if ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_D)
    {
        const uint64_t newKey = nextKey++;
        pendingSelectionKey = newKey;
        pendingDuplicateKey = newKey;
        QMetaObject::invokeMethod(worker, "duplicateEntry", Qt::QueuedConnection,
                                  Q_ARG(quint64, key), Q_ARG(quint64, newKey));
        saveTxMessages();
        return true;
    }

    // Ctrl+Up: move card up
    if ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_Up)
    {
        if (row > 0 && worker)
        {
            const int targetRow = row - 1;
            pendingSelectionKey = key;
            resetSortIndicator();
            QMetaObject::invokeMethod(worker, "moveEntry", Qt::QueuedConnection,
                                      Q_ARG(int, row), Q_ARG(int, targetRow));
            txTable->selectRow(targetRow);
            txTable->setCurrentCell(targetRow, 1);
            saveTxMessages();
            startTxSwapAnimation(targetRow, row);
            return true;
        }
    }

    // Ctrl+Down: move card down
    if ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_Down)
    {
        if (row < txTable->rowCount() - 1 && worker)
        {
            const int targetRow = row + 1;
            pendingSelectionKey = key;
            resetSortIndicator();
            QMetaObject::invokeMethod(worker, "moveEntry", Qt::QueuedConnection,
                                      Q_ARG(int, row), Q_ARG(int, targetRow));
            txTable->selectRow(targetRow);
            txTable->setCurrentCell(targetRow, 1);
            saveTxMessages();
            startTxSwapAnimation(targetRow, row);
            return true;
        }
    }

    // Space: toggle active/stopped state
    if (event->key() == Qt::Key_Space && event->modifiers() == Qt::NoModifier)
    {
        if (!connected)
        {
            return true; // No-op when disconnected, do not trigger error
        }
        if (latest && worker)
        {
            for (const auto &view : latest->tx)
            {
                if (view.entry.key == key)
                {
                    QMetaObject::invokeMethod(worker, "setActive", Qt::QueuedConnection,
                                              Q_ARG(quint64, key), Q_ARG(bool, !view.entry.active));
                    return true;
                }
            }
        }
        return true;
    }

    return false;
}

/**
 * @brief Trigger smooth vertical swap slide animation between two rows.
 * @param newRow Destination row index of the moved card.
 * @param oldRow Origin row index of the moved card.
 */
void MainWindow::startTxSwapAnimation(int newRow, int oldRow)
{
    if (!txTable || newRow < 0 || oldRow < 0)
    {
        return;
    }
    finalizeTxDeleteAnimation();
    if (txDuplicateAnimTimer && txDuplicateAnimTimer->isActive())
    {
        txDuplicateAnimTimer->stop();
        txTable->setProperty("duplicateAnimRow", -1);
        txTable->setProperty("duplicateAnimOffset", 0.0);
        txTable->setProperty("duplicateAnimProgress", 1.0);
    }
    const double rowH = double(txTable->rowHeight(newRow) > 0 ? txTable->rowHeight(newRow) : 74);
    txSwapAnimStartOffset = (newRow < oldRow) ? +rowH : -rowH;
    txTable->setProperty("swapRowA", newRow);
    txTable->setProperty("swapRowB", oldRow);
    txTable->setProperty("swapOffset", txSwapAnimStartOffset);
    txSwapAnimElapsed.restart();
    if (!txSwapAnimTimer)
    {
        txSwapAnimTimer = new QTimer(this);
        txSwapAnimTimer->setTimerType(Qt::PreciseTimer);
        connect(txSwapAnimTimer, &QTimer::timeout, this, [this]()
        {
            const qint64 elapsed = txSwapAnimElapsed.elapsed();
            const double duration = 180.0;
            const double t = std::clamp(double(elapsed) / duration, 0.0, 1.0);
            const double ease = 1.0 - std::pow(1.0 - t, 2.0);
            const double curOffset = (1.0 - ease) * txSwapAnimStartOffset;
            txTable->setProperty("swapOffset", curOffset);
            txTable->viewport()->update();
            if (t >= 1.0)
            {
                txSwapAnimTimer->stop();
                txTable->setProperty("swapRowA", -1);
                txTable->setProperty("swapRowB", -1);
                txTable->setProperty("swapOffset", 0.0);
                txTable->viewport()->update();
            }
        });
    }
    txSwapAnimTimer->start(8);
    txTable->viewport()->update();
}

/**
 * @brief Trigger smooth slide-out and push-down animation for a duplicated TX card.
 * @param insertRow Row index of the newly inserted cloned card.
 */
void MainWindow::startTxDuplicateAnimation(int insertRow)
{
    if (!txTable || insertRow < 0 || insertRow >= txTable->rowCount())
    {
        return;
    }
    finalizeTxDeleteAnimation();
    if (txDuplicateAnimTimer && txDuplicateAnimTimer->isActive())
    {
        txDuplicateAnimTimer->stop();
    }
    if (txSwapAnimTimer && txSwapAnimTimer->isActive())
    {
        txSwapAnimTimer->stop();
        txTable->setProperty("swapRowA", -1);
        txTable->setProperty("swapRowB", -1);
        txTable->setProperty("swapOffset", 0.0);
    }
    const double rowH = double(txTable->rowHeight(insertRow) > 0 ? txTable->rowHeight(insertRow) : 74);
    txDuplicateAnimStartOffset = -rowH;
    txTable->setProperty("duplicateAnimRow", insertRow);
    txTable->setProperty("duplicateAnimOffset", txDuplicateAnimStartOffset);
    txTable->setProperty("duplicateAnimProgress", 0.0);
    txDuplicateAnimElapsed.restart();
    if (!txDuplicateAnimTimer)
    {
        txDuplicateAnimTimer = new QTimer(this);
        txDuplicateAnimTimer->setTimerType(Qt::PreciseTimer);
        connect(txDuplicateAnimTimer, &QTimer::timeout, this, [this]()
        {
            const qint64 elapsed = txDuplicateAnimElapsed.elapsed();
            const double duration = 220.0;
            const double t = std::clamp(double(elapsed) / duration, 0.0, 1.0);
            const double ease = 1.0 - std::pow(1.0 - t, 2.0);
            const double curOffset = (1.0 - ease) * txDuplicateAnimStartOffset;
            txTable->setProperty("duplicateAnimOffset", curOffset);
            txTable->setProperty("duplicateAnimProgress", ease);
            txTable->viewport()->update();
            if (t >= 1.0)
            {
                txDuplicateAnimTimer->stop();
                txTable->setProperty("duplicateAnimRow", -1);
                txTable->setProperty("duplicateAnimOffset", 0.0);
                txTable->setProperty("duplicateAnimProgress", 1.0);
                txTable->viewport()->update();
            }
        });
    }
    txDuplicateAnimTimer->start(8);
    txTable->viewport()->update();
}



uint64_t MainWindow::selectedTx() const
{
    const auto row = txTable->currentRow();
    return row < 0 || !txTable->item(row, 0) ? 0 : txTable->item(row, 0)->data(Qt::UserRole).toULongLong();
}


void MainWindow::setTxSummaryMode(int mode)
{
    Q_UNUSED(mode);
}


void MainWindow::setTxSummaryStats(int total, int running, double estimatedBps, double utilization)
{
    if (txSummary)
    {
        txSummary->setStats(total, running, estimatedBps, utilization);
    }
}

/**
 * @brief Initialize the sort popup menu with criteria actions.
 */
void MainWindow::setupSortMenu()
{
    if (!sortMenu)
    {
        sortMenu = new WorkbenchMenu(this);
        sortMenu->setObjectName("sortMenu");
        configureTranslucentMenu(sortMenu);
        sortMenu->installEventFilter(this);
    }
    sortMenu->clear();
    sortActions.clear();

    static const struct
    {
        const char *name;
        const char *label;
    } kSortDefs[] = {
        {"sortById", "ID"},
        {"sortByStatus", "Status"},
        {"sortByPayloadSize", "Payload Size"},
        {"sortByTargetHz", "Target Rate"},
        {"sortBySysComp", "Sys / Comp ID"}
    };

    for (int criteria = 0; criteria < 5; ++criteria)
    {
        auto *action = new QAction(kSortDefs[criteria].label, sortMenu);
        action->setObjectName(kSortDefs[criteria].name);
        action->setCheckable(true);
        connect(action, &QAction::triggered, this, [this, criteria]()
        {
            applySort(criteria);
        });
        sortMenu->addAction(action);
        sortActions.push_back(action);
    }

    updateSortMenuActions();

    if (sortButton)
    {
        connect(sortButton, &QPushButton::clicked, this, [this]()
        {
            if (!sortMenu || (txTable && txTable->rowCount() == 0))
            {
                return;
            }
            updateSortMenuActions();
            const QPoint pos = sortButton->mapToGlobal(QPoint(0, sortButton->height() + 2));
            sortMenu->popup(pos);
        });
    }
}

/**
 * @brief Update vector arrow icons and checked states across sort menu actions.
 */
void MainWindow::updateSortMenuActions()
{
    const bool isDark = sortMenu ? (sortMenu->palette().color(QPalette::Window).lightness() < 128) : true;
    const QColor arrowColor = isDark ? QColor("#60a5fa") : QColor("#2563eb");

    for (size_t i = 0; i < sortActions.size(); ++i)
    {
        auto *action = sortActions[i];
        if (!action)
        {
            continue;
        }
        const int criteria = static_cast<int>(i);
        const bool active = (currentSortCriteria == criteria);

        action->setChecked(active);
        if (active)
        {
            const QString iconName = currentSortAscending ? "sortArrowUp" : "sortArrowDown";
            action->setIcon(actionIcon(iconName, arrowColor));
        }
        else
        {
            action->setIcon(actionIcon("transparent16", Qt::transparent));
        }
    }
}

/**
 * @brief Apply sorting on TX message cards by criteria and toggle sort order.
 * @param criteria Target sort criteria (0: Id, 1: Status, 2: PayloadSize, 3: TargetHz, 4: SysComp).
 */
void MainWindow::applySort(int criteria)
{
    if (currentSortCriteria == criteria)
    {
        currentSortAscending = !currentSortAscending;
    }
    else
    {
        currentSortCriteria = criteria;
        currentSortAscending = true;
    }

    if (worker)
    {
        QMetaObject::invokeMethod(worker, "sortEntries", Qt::QueuedConnection,
                                  Q_ARG(int, currentSortCriteria),
                                  Q_ARG(bool, currentSortAscending));
    }
    updateSortMenuActions();
    saveTxMessages();
}

/**
 * @brief Reset sort state indicator to custom when cards are manually rearranged.
 */
void MainWindow::resetSortIndicator()
{
    if (currentSortCriteria != -1)
    {
        currentSortCriteria = -1;
        currentSortAscending = true;
        updateSortMenuActions();
    }
}

/**
 * @brief Update the sort button icon based on theme, hover, and enabled states.
 * @param hovered True if the mouse pointer is currently hovering over the button.
 */
void MainWindow::updateSortButtonIcon(bool hovered)
{
    if (!sortButton)
    {
        return;
    }
    const bool dark = property("darkTheme").toBool();
    const bool isHovered = hovered && sortButton->isEnabled();
    const QColor normalColor = dark ? QColor("#8895ad") : QColor("#64748b");
    const QColor hoverColor = dark ? QColor("#edf0fa") : QColor("#20263b");
    const QColor disabledColor = dark ? QColor("#526079") : QColor("#94a3b8");

    QIcon icon = actionIcon("sortMessages", isHovered ? hoverColor : normalColor);
    icon.addPixmap(actionIcon("sortMessages", disabledColor).pixmap(16, 16), QIcon::Disabled);
    sortButton->setIcon(icon);
    sortButton->setIconSize(QSize(16, 16));
}


