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
#include <map>

class TxSummaryWidget;
class ConnectionStatusWidget;
class StatusBarMetricsWidget;
class StatusBarMessageWidget;

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
    /** @brief Update the TX summary counts directly (e.g. for inspection/testing). */
    void setTxSummaryStats(int total, int running);
    /** @brief Update the connection status indicator directly (e.g. for inspection/testing). */
    void setConnectionStatus(const QString &state, bool open);
    /** @brief Update the status bar metrics directly (e.g. for inspection/testing). */
    void setStatusMetrics(double rxBps, double txBps, uint64_t crc, uint64_t pending, uint64_t overflow);
    /** @brief Apply one snapshot directly (e.g. for inspection/testing). */
    void applySnapshot(SnapshotPtr snapshot);
    /** @brief Stop the background snapshot polling timer (e.g. for deterministic unit testing). */
    void stopRefreshTimer();
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
    /** @brief Display a popup menu with preset frequencies and custom rate input. */
    void showQuickRateMenu(quint64 key, double currentHz, const QPoint &globalPos);
    /** @brief Display transient confirmation badge and cell flash when copying a field value. */
    void showCopiedFeedback(int row);
    bool profileBusy = false;
    QString profileExportPath;
    QAction *importProfileAction = nullptr;
    QAction *exportProfileAction = nullptr;
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
    QLabel *inspectorTitle = nullptr;
    QLabel *inspectorSource = nullptr;
    QLabel *copyFeedbackBadge = nullptr;
    QTimer *copyFeedbackTimer = nullptr;
    int copiedInspectorRow = -1;
    QLabel *pausedLabel = nullptr;
    std::map<QString, QString> lastFieldValues;
};
