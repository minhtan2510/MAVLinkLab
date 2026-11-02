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
#include <map>

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    /** @brief Build the desktop UI and start an isolated serial worker. */
    explicit MainWindow(QWidget *parent = nullptr);
    /** @brief Close the serial port in its owning thread and join the worker. */
    ~MainWindow() override;
protected:
    /** @brief Track system palette and button input modality for keyboard-only focus indicators. */
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    /** @brief Re-enumerate ports while retaining an explicitly typed PTY/device path. */
    void refreshPorts();
    /** @brief Send the selected serial configuration to the worker. */
    void toggleConnection();
    /** @brief Create or edit one complete TX entry through generated metadata. */
    void editMessage(bool edit);
    /** @brief Apply one snapshot without rebuilding existing RX rows. */
    void applySnapshot(SnapshotPtr snapshot);
    /** @brief Decode the selected stream into the field inspector. */
    void updateInspector();
    /** @brief Filter RX rows by sender and message identity. */
    void filterRx();
    /** @brief Apply semantic light/dark tokens or the current system palette. */
    void applyTheme();
    /** @brief Display a persistent actionable error in the status bar. */
    void showError(const QString &message);
    /** @brief Return the stable identity of the selected TX row, or zero. */
    uint64_t selectedTx() const;
    /** @brief Enable actions according to connection and selection state. */
    void updateActions();
    /** @brief Refresh the compact serial framing summary without changing I/O. */
    void updateSerialSummary();
    /**
     * @brief Update the hardware connection status LED indicator.
     * @param state Current connection state string.
     */
    void updateStatusDot(const QString &state);
    SerialWorker *worker = nullptr;
    QThread thread;
    QTimer refresh;
    bool snapshotPending = false;
    bool connected = false;
    bool connecting = false;
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
    QDialog *settingsDialog = nullptr;
    QPushButton *connectButton = nullptr;
    QLabel *connectionLabel = nullptr;
    QLabel *statusDot = nullptr;
    QLabel *metrics = nullptr;
    QLabel *lastError = nullptr;
    QTableWidget *txTable = nullptr;
    QTreeWidget *rxTree = nullptr;
    QTableWidget *inspector = nullptr;
    QLineEdit *rxFilter = nullptr;
    QPushButton *editButton = nullptr;
    QPushButton *removeButton = nullptr;
    QPushButton *onceButton = nullptr;
    QPushButton *startButton = nullptr;
    QPushButton *stopButton = nullptr;
    QPushButton *stopAllButton = nullptr;
    QLabel *serialSummary = nullptr;
    QLabel *txSummary = nullptr;
    QLabel *rxSummary = nullptr;
    QLabel *inspectorTitle = nullptr;
    std::map<QString, QString> lastFieldValues;
};
