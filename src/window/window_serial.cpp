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
    connectButton->setEnabled(false);
    connectingByUser = true;
    connectingPort = config.port;
    connectingBaud = config.baud;
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
        }
    }
}

/**
 * @brief Display a compact alert dialog when serial port connection fails.
 * @param title Headline summarizing the failure.
 * @param message Detailed reason or actionable instruction.
 */
void MainWindow::showConnectionError(const QString &title, const QString &message)
{
    if (connectButton)
    {
        connectButton->setEnabled(true);
        connectButton->setText("Connect");
    }
    ConnectionErrorDialog dialog(title, message, this);
    dialog.exec();
    if (connectButton)
    {
        connectButton->setEnabled(true);
        connectButton->setText("Connect");
    }
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

