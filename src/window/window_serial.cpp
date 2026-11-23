/**
 * @file window_serial.cpp
 * @brief MainWindow serial port detection, connection lifecycle, and hardware alert dialogs.
 */

#include "window.h"
#include "window_private.h"
#include <QSerialPortInfo>
#include <QMessageBox>
#include <QStatusBar>
#include <QTimer>
#include <cmath>

/** @brief Refresh the port dropdown and retain its selection only if still enumerated. */
void MainWindow::refreshPorts()
{
    if (connected || connecting || reconnecting)
    {
        return;
    }
    const QString previous = portBox->currentText();
    portBox->clear();
    for (const auto &info : QSerialPortInfo::availablePorts())
    {
        if (isPluggedPort(info))
        {
            portBox->addItem(info.systemLocation());
            const QString desc = info.description();
            const QString mfg = info.manufacturer();
            const QString tip = (!desc.isEmpty() || !mfg.isEmpty()) ?
                                (desc + " · " + mfg) : info.systemLocation();
            portBox->setItemData(portBox->count() - 1, tip, Qt::ToolTipRole);
        }
    }
    if (!previous.isEmpty() && portBox->findText(previous) >= 0)
    {
        portBox->setCurrentText(previous);
    }
}

PortConfig MainWindow::currentPortConfig() const
{
    PortConfig config;
    if (portBox)
    {
        config.port = portBox->currentText().trimmed();
    }
    if (baudBox)
    {
        bool ok = false;
        const int baud = baudBox->currentText().toInt(&ok);
        config.baud = (ok && baud > 0) ? baud : 0;
    }
    if (dataBox)
    {
        config.data = static_cast<QSerialPort::DataBits>(dataBox->currentText().toInt());
    }
    if (parityBox && parityBox->currentIndex() >= 0 && parityBox->currentIndex() < 5)
    {
        const QSerialPort::Parity parities[] = {QSerialPort::NoParity, QSerialPort::EvenParity,
                                                QSerialPort::OddParity, QSerialPort::MarkParity, QSerialPort::SpaceParity};
        config.parity = parities[parityBox->currentIndex()];
    }
    if (stopBox)
    {
        config.stop = stopBox->currentIndex() == 0 ? QSerialPort::OneStop :
                      stopBox->currentIndex() == 1 ? QSerialPort::TwoStop : QSerialPort::OneAndHalfStop;
    }
    if (flowBox)
    {
        config.flow = static_cast<QSerialPort::FlowControl>(flowBox->currentIndex());
    }
    if (autoReconnectBox)
    {
        config.autoReconnect = autoReconnectBox->isChecked();
    }
    if (deassertControlLinesBox)
    {
        config.deassertControlLines = deassertControlLinesBox->isChecked();
    }
    return config;
}

void MainWindow::toggleConnection()
{
    if (connected || reconnecting)
    {
        connectingByUser = false;
        QMetaObject::invokeMethod(worker, "closePort", Qt::QueuedConnection);
        return;
    }
    if (connecting)
    {
        return;
    }
    PortConfig config = currentPortConfig();
    if (config.port.isEmpty())
    {
        showConnectionError("No Port Selected", "Please select an available serial port before connecting.");
        return;
    }
    if (config.baud <= 0 || config.baud > 12000000)
    {
        showConnectionError("Invalid Baud Rate",
                            QString("Baud rate '%1' is invalid. Please select or enter a rate from 1 to 12,000,000.")
                                .arg(baudBox->currentText()));
        return;
    }
    connectingByUser = true;
    connectingPort = config.port;
    connectingBaud = config.baud;
    updateConnectButtonVisuals("Connecting", false);
    QMetaObject::invokeMethod(worker, "openPort", Qt::QueuedConnection, Q_ARG(PortConfig, config));
}


void MainWindow::updateActions()
{
    importProfileAction->setEnabled(!profileBusy);
    exportProfileAction->setEnabled(!profileBusy);
    findChild<QPushButton *>("addMessage")->setEnabled(!profileBusy);
    txTable->setEnabled(!profileBusy);
    const bool selected = !profileBusy && selectedTx() != 0;
    editAction->setEnabled(selected);
    duplicateAction->setEnabled(selected);
    removeAction->setEnabled(selected);
    onceAction->setEnabled(selected && connected);
    startAction->setEnabled(selected && connected);
    stopAction->setEnabled(selected && connected);
    const bool hasEntries = !profileBusy && txTable->rowCount() > 0;
    runAllButton->setEnabled(connected && hasEntries);
    stopAllButton->setEnabled(connected && hasEntries);
    deleteAllButton->setEnabled(hasEntries);
    if (sortButton)
    {
        sortButton->setEnabled(hasEntries);
        updateSortButtonIcon(sortButton->underMouse());
    }
    const int row = txTable->currentRow();
    const bool running = selected && txTable->item(row, 6) && txTable->item(row, 6)->text() == "Running";
    startAction->setVisible(!running);
    stopAction->setVisible(running);

}

void MainWindow::updateSerialSummary()
{
    const QString parity = parityBox->currentIndex() == 0 ? "N" : parityBox->currentText().left(1);
    serialSummary->setText(dataBox->currentText() + parity + stopBox->currentText() + " · No flow");
    serialSummary->setToolTip(QString("%1 data bits · %2 parity · %3 stop bit(s) · %4 flow control")
                              .arg(dataBox->currentText(), parityBox->currentText(),
                                   stopBox->currentText(), flowBox->currentText()));
    if (flowBox->currentIndex() != 0)
    {
        serialSummary->setText(dataBox->currentText() + parity + stopBox->currentText() + " · " +
                              (flowBox->currentIndex() == 1 ? "RTS/CTS" : "XON/XOFF"));
    }
}



void MainWindow::setConnectionStatus(const QString &state, bool open)
{
    connected = open;
    connecting = (state == "Connecting");
    reconnecting = (state == "Reconnecting");
    if (auto *tree = dynamic_cast<RxTreeWidget *>(rxTree))
    {
        tree->setConnectionInfo(open,
                                portBox ? portBox->currentText() : QString(),
                                baudBox ? baudBox->currentText() : QString());
    }
    if (connectionStatusPill)
    {
        connectionStatusPill->setStatus(state, open);
    }
    if (rxThroughput)
    {
        if (!open)
        {
            rxThroughput->hide();
        }
        else
        {
            rxThroughput->show();
            if (latest)
            {
                setRxThroughputStats(latest->rxBytesPerSecond, latest->rxUtilizationPercent, latest->baud);
            }
            else
            {
                setRxThroughputStats(0.0, 0.0, baudBox ? baudBox->currentText().toInt() : 115200);
            }
        }
    }
    updateConnectButtonVisuals(state, open);
}

/**
 * @brief Start smooth breathing pulse animation for connecting or reconnecting state.
 * @param role Target animation role ("connecting" or "reconnecting").
 */
void MainWindow::startConnectPulse(const QString &role)
{
    connectPulseRole = role;
    if (connectPulseTimer && connectPulseTimer->isActive())
    {
        connectPulseTimer->stop();
    }
    connectPulsePhase = 0.0;
    if (connectButton)
    {
        connectButton->setStyleSheet(QString());
        connectButton->style()->unpolish(connectButton);
        connectButton->style()->polish(connectButton);
    }
}

/** @brief Stop the breathing pulse animation and reset button inline styling. */
void MainWindow::stopConnectPulse()
{
    if (connectPulseTimer && connectPulseTimer->isActive())
    {
        connectPulseTimer->stop();
    }
    connectPulsePhase = 0.0;
    connectPulseRole.clear();
    if (connectButton)
    {
        connectButton->setStyleSheet(QString());
        connectButton->style()->unpolish(connectButton);
        connectButton->style()->polish(connectButton);
    }
}

/**
 * @brief Update the visual state, text label, role, and tooltip of the Connect button.
 * @param state Status string ("Disconnected", "Connecting", "Connected", "Reconnecting").
 * @param open True if serial port is open.
 */
void MainWindow::updateConnectButtonVisuals(const QString &state, bool open)
{
    if (!connectButton)
    {
        return;
    }

    const QString port = portBox ? portBox->currentText().trimmed() : QString();
    const QString baud = baudBox ? baudBox->currentText().trimmed() : QString();

    if (state == "Connecting")
    {
        connectButton->setText(QString::fromUtf8("Connecting…"));
        connectButton->setProperty("role", "connecting");
        connectButton->setProperty("connected", false);
        connectButton->setProperty("reconnecting", false);
        connectButton->setEnabled(false);
        if (!port.isEmpty() && !baud.isEmpty())
        {
            connectButton->setToolTip(QString("Opening serial connection to %1 (%2 baud)...").arg(port, baud));
        }
        else
        {
            connectButton->setToolTip("Opening serial connection...");
        }
        startConnectPulse("connecting");
    }
    else if (state == "Reconnecting")
    {
        connectButton->setText(QString::fromUtf8("Cancel (Reconnecting…)"));
        connectButton->setProperty("role", "reconnecting");
        connectButton->setProperty("connected", false);
        connectButton->setProperty("reconnecting", true);
        connectButton->setEnabled(true);
        connectButton->setToolTip("Click to cancel auto-reconnection loop");
        startConnectPulse("reconnecting");
    }
    else if (open)
    {
        stopConnectPulse();
        connectButton->setText("Disconnect");
        connectButton->setProperty("role", "danger");
        connectButton->setProperty("connected", true);
        connectButton->setProperty("reconnecting", false);
        connectButton->setEnabled(true);
        if (!port.isEmpty() && !baud.isEmpty())
        {
            connectButton->setToolTip(QString("Close serial connection to %1 (%2 baud)").arg(port, baud));
        }
        else
        {
            connectButton->setToolTip("Close serial connection");
        }
    }
    else
    {
        stopConnectPulse();
        connectButton->setText("Connect");
        connectButton->setProperty("role", "primary");
        connectButton->setProperty("connected", false);
        connectButton->setProperty("reconnecting", false);
        connectButton->setEnabled(true);
        connectButton->setToolTip("Open serial connection");
    }

    connectButton->style()->unpolish(connectButton);
    connectButton->style()->polish(connectButton);
}

/**
 * @brief Display a compact alert dialog when serial port connection fails.
 * @param title Headline summarizing the failure.
 * @param message Detailed reason or actionable instruction.
 */
void MainWindow::showConnectionError(const QString &title, const QString &message)
{
    updateConnectButtonVisuals("Disconnected", false);
    ConnectionErrorDialog dialog(title, message, this);
    dialog.exec();
    updateConnectButtonVisuals("Disconnected", false);
}

void MainWindow::showError(const QString &message)
{
    if (connectingByUser)
    {
        connectingByUser = false;
        const QString text = QString("Could not open %1 (%2 baud): %3")
                                 .arg(connectingPort)
                                 .arg(connectingBaud)
                                 .arg(message.isEmpty() ? "Unknown I/O error." : message);
        showConnectionError("Connection Failed", text);
        return;
    }
    if (lastError)
    {
        lastError->setText("Error: " + message);
        lastError->setToolTip(message);
    }
}

