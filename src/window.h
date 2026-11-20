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
#include <QElapsedTimer>
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
class TrashDropCard;
class WorkbenchToast;

#include "plotter.h"

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
    /**
     * @brief Update the visual state, text label, role, and tooltip of the Connect button.
     * @param state Status string ("Disconnected", "Connecting", "Connected", "Reconnecting").
     * @param open True if serial port is open.
     */
    void updateConnectButtonVisuals(const QString &state, bool open);
    /**
     * @brief Start smooth breathing pulse animation for connecting or reconnecting state.
     * @param role Target animation role ("connecting" or "reconnecting").
     */
    void startConnectPulse(const QString &role);
    /** @brief Stop the breathing pulse animation and reset button inline styling. */
    void stopConnectPulse();
    /** @brief Update the status bar metrics directly (e.g. for inspection/testing). */
    void setStatusMetrics(double rxBps, double txBps, uint64_t crc, uint64_t pending, uint64_t overflow,
                          double rxUtil = 0.0, double txUtil = 0.0, int baud = 0);
    /** @brief Apply one snapshot directly (e.g. for inspection/testing). */
    void applySnapshot(SnapshotPtr snapshot);
    /** @brief Update the RX throughput indicator pill directly (e.g. for inspection/testing). */
    void setRxThroughputStats(double bps, double utilization, int baud);
    /** @brief Stop the background snapshot polling timer (e.g. for deterministic unit testing). */
    void stopRefreshTimer();
    /** @brief Trigger smooth vertical swap slide animation between two rows. */
    void startTxSwapAnimation(int newRow, int oldRow);
    /** @brief Trigger smooth slide-out and push-down animation for a duplicated TX card. */
    void startTxDuplicateAnimation(int insertRow);
    /** @brief Return the currently configured serial port settings. */
    PortConfig currentPortConfig() const;
    /** @brief Return pointer to the telemetry plotter widget. */
    TelemetryPlotWidget *plotterWidget() const;
    /** @brief Add or toggle a field subscription in the telemetry plotter. */
    void togglePlotField(uint32_t sysid, uint8_t compid, uint32_t msgid, const QString &fieldName, const QString &label);
    /** @brief Return the application icon containing multi-resolution raster assets. */
    static QIcon appIcon();
    /**
     * @brief Compute the visual bounding rectangle for a TX message card inside a table cell.
     * @param cellRect The raw geometry of the table cell (column 1).
     * @param animDy Optional vertical animation translation offset in pixels.
     * @return Padded card rectangle with 6px left margin and 8px right breathing room.
     */
    static QRect txCardRect(const QRect &cellRect, int animDy = 0);
protected:
    /** @brief Persist presentation layout when the window closes normally. */
    void closeEvent(QCloseEvent *event) override;
    /** @brief Track theme/input changes and clip the UART popup to its rounded boundary. */
    bool eventFilter(QObject *watched, QEvent *event) override;
    /** @brief Cancel drag-and-drop operations on Escape and dispatch smart TX shortcuts. */
    void keyPressEvent(QKeyEvent *event) override;
    /** @brief Keep overlays such as the undo toast properly positioned on resize. */
    void resizeEvent(QResizeEvent *event) override;
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
    /** @brief Toggle expansion state of all RX device nodes. */
    void toggleExpandCollapseRx();
    /** @brief Update the toggle expand/collapse icon and tooltip based on RX tree state. */
    void updateRxExpandIcon();
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
    /** @brief Display reference dialog showing available keyboard shortcuts. */
    void showKeyboardShortcuts();
    /** @brief Display a popup menu with preset frequencies and custom rate input. */
    void showQuickRateMenu(quint64 key, double currentHz, const QPoint &globalPos);
    /** @brief Display transient confirmation badge and cell flash when copying a field value. */
    void showCopiedFeedback(int row);
    /** @brief Update the color styling of the RX throughput pill based on channel utilization. */
    void updateRxThroughputStyle(double util);
    /**
     * @brief Start interactive drag operation for a TX message card.
     * @param row Index of the row being dragged.
     * @param globalPos Initial mouse position in global coordinates.
     */
    void startTxDrag(int row, const QPoint &globalPos);
    /**
     * @brief Update ghost preview and target drop row while dragging.
     * @param globalPos Current mouse position in global coordinates.
     * @param viewportPos Current mouse position in txTable viewport coordinates.
     */
    void updateTxDrag(const QPoint &globalPos, const QPoint &viewportPos);
    /**
     * @brief Finalize or cancel the active drag operation.
     * @param commit True to apply the new row order to the worker; false to revert.
     */
    void finishTxDrag(bool commit);
    /** @brief Cancel the active drag operation and revert card positions. */
    void cancelTxDrag();
    /** @brief Execute one frame of the shrink-and-fade ghost deletion animation. */
    void stepTxDeleteShrink();
    /** @brief Display a transient undo toast at the bottom-left corner when an entry is deleted. */
    void showTxUndoToast(const Mav::TxEntry &entry, int rowIndex);
    /** @brief Restore the last deleted TX entry at its original row position. */
    void executeTxUndo();
    /** @brief Reposition the undo toast anchored to the bottom-left of the window. */
    void repositionTxUndoToast();
    /** @brief Initialize the sort popup menu and context submenu with criteria actions. */
    void setupSortMenu();
    /** @brief Update checkmarks, indicators, and text labels across sort menu actions. */
    void updateSortMenuActions();
    /**
     * @brief Apply sorting on TX message cards by criteria and toggle sort order.
     * @param criteria Target sort criteria (0: Id, 1: Name, 2: Status, 3: PayloadSize, 4: TargetHz, 5: SysComp).
     */
    void applySort(int criteria);
    /** @brief Reset sort state indicator to custom when cards are manually rearranged. */
    void resetSortIndicator();
    /**
     * @brief Update the sort button icon based on theme, hover, and enabled states.
     * @param hovered True if the mouse pointer is currently hovering over the button.
     */
    void updateSortButtonIcon(bool hovered = false);
    /** @brief Delete a TX message card at the specified row and trigger the undo toast. */
    void deleteTxCardAt(int row);
    /**
     * @brief Start smooth 220ms tuck-up collapse animation for row deletion (reverse duplicate).
     * @param row Index of the card row being deleted.
     * @param key Identifier of the message entry being deleted.
     */
    void startTxDeleteAnimation(int row, uint64_t key);
    /** @brief Finalize any active row deletion animation immediately. */
    void finalizeTxDeleteAnimation();
    /** @brief Handle keyboard navigation and manipulation shortcuts for TX cards. */
    bool handleTxKeyPress(QKeyEvent *event);
    bool profileBusy = false;
    QString profileExportPath;
    QAction *importProfileAction = nullptr;
    QAction *exportProfileAction = nullptr;
    QAction *checkUpdatesAction = nullptr;
    QAction *keyboardShortcutsAction = nullptr;
    SerialWorker *worker = nullptr;
    QThread thread;
    QTimer refresh;
    QTimer *connectPulseTimer = nullptr;
    double connectPulsePhase = 0.0;
    QString connectPulseRole;
    bool snapshotPending = false;
    bool connected = false;
    bool connecting = false;
    bool reconnecting = false;
    bool connectingByUser = false;
    QString connectingPort;
    int connectingBaud = 0;
    bool paused = false;
    uint64_t nextKey = 1;
    uint64_t pendingSelectionKey = 0;
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
    QCheckBox *singleActiveStreamBox = nullptr;
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
    bool isDraggingTx = false;
    bool dragHandlePressed = false;
    int dragSourceRow = -1;
    int dragTargetRow = -1;
    QPoint dragStartPos;
    QPoint lastDragGlobalPos;
    QPoint lastDragVpPos;
    QPoint ghostOffset;
    QLabel *dragGhostLabel = nullptr;
    QTimer *dragAutoScrollTimer = nullptr;
    int dragAutoScrollSpeed = 0;
    QTimer *dragAnimTimer = nullptr;
    std::map<int, double> dragRowOffsets;
    TrashDropCard *trashDropCard = nullptr;
    bool isHoveringTrash = false;
    QTimer *dragDeleteShrinkTimer = nullptr;
    QElapsedTimer dragDeleteShrinkTimerElapsed;
    int dragDeleteShrinkStep = 0;
    QRect dragDeleteStartRect;
    QPoint dragDeleteTargetCenter;
    uint64_t pendingDeleteKey = 0;
    QPixmap dragGhostNormal;
    QPixmap dragGhostDanger;
    WorkbenchToast *txUndoToast = nullptr;
    /** @brief Record for an entry deleted from the transmit queue. */
    struct DeletedTxItem
    {
        Mav::TxEntry entry;
        int rowIndex = -1;
    };
    std::vector<DeletedTxItem> txUndoStack;
    Mav::TxEntry lastDeletedTxEntry;
    int lastDeletedTxRowIndex = -1;
    QTimer *txSwapAnimTimer = nullptr;
    QElapsedTimer txSwapAnimElapsed;
    double txSwapAnimStartOffset = 0.0;
    uint64_t pendingDuplicateKey = 0;
    QTimer *txDuplicateAnimTimer = nullptr;
    QElapsedTimer txDuplicateAnimElapsed;
    double txDuplicateAnimStartOffset = 0.0;
    QTimer *txDeleteAnimTimer = nullptr;
    QElapsedTimer txDeleteAnimElapsed;
    int txDeleteAnimRow = -1;
    uint64_t txDeleteAnimKey = 0;
    double txDeleteAnimRowHeight = 74.0;
    QMenu *txMenu = nullptr;
    QTreeWidget *rxTree = nullptr;
    QTableWidget *inspector = nullptr;
    QLineEdit *rxFilter = nullptr;
    QPushButton *toggleExpandRx = nullptr;
    QAction *toggleExpandRxAction = nullptr;
    QLineEdit *paramFilter = nullptr;
    bool isParamView = false;
    uint32_t activeParamSysId = 0;
    uint8_t activeParamCompId = 0;
    QAction *editAction = nullptr;
    QAction *duplicateAction = nullptr;
    QAction *removeAction = nullptr;
    QAction *onceAction = nullptr;
    QAction *startAction = nullptr;
    QAction *stopAction = nullptr;
    QPushButton *runAllButton = nullptr;
    QPushButton *stopAllButton = nullptr;
    QPushButton *deleteAllButton = nullptr;
    QPushButton *sortButton = nullptr;
    QMenu *sortMenu = nullptr;
    std::vector<QAction *> sortActions;
    int currentSortCriteria = -1;
    bool currentSortAscending = true;
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
