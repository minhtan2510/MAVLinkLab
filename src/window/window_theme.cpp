/**
 * @file window_theme.cpp
 * @brief MainWindow Dark/Light semantic theme application and QSS stylesheet engine.
 */

#include "window.h"
#include "window_private.h"
#include "plotter.h"
#include <QApplication>
#include <QPalette>
#include <QTableWidget>
#include <QTreeWidget>


void MainWindow::applyTheme()
{
    static bool applying = false;
    if (applying)
    {
        return;
    }
    applying = true;

    const bool dark = themeBox->currentIndex() == 2 ||
                      (themeBox->currentIndex() == 0 && qApp->palette().color(QPalette::Window).lightness() < 128);
    qApp->setProperty("darkTheme", dark);
    setProperty("darkTheme", dark);
    if (settingsDialog)
    {
        settingsDialog->setProperty("darkTheme", dark);
    }
    if (txTable)
    {
        txTable->setProperty("darkTheme", dark);
        txTable->viewport()->update();
    }
    if (rxTree)
    {
        rxTree->setProperty("darkTheme", dark);
        rxTree->viewport()->update();
    }
    if (inspector)
    {
        inspector->setProperty("darkTheme", dark);
    }
    if (telemetryPlotter)
    {
        telemetryPlotter->setDarkTheme(dark);
    }
    for (auto *box : {restoreTxBox, singleActiveStreamBox, autoReconnectBox, deassertControlLinesBox, showDebugBarBox})
    {
        if (box)
        {
            box->setProperty("darkTheme", dark);
            box->update();
        }
    }
    const QString background = dark ? "#0d1422" : "#edf1f8";
    const QString surface = dark ? "#1b2333" : "#ffffff";
    const QString foreground = dark ? "#edf0fa" : "#20263b";
    const QString muted = dark ? "#aab4cd" : "#626c84";
    const QString hintForeground = dark ? "#606d88" : "#8893a7";
    const QString border = dark ? "#40506a" : "#d0d9e7";
    const QString alternate = dark ? "#212c40" : "#f7f8fc";
    const QString controlBorder = dark ? "#667595" : "#929db3";
    const QString scrollNormal = dark ? "rgba(255, 255, 255, 0.18)" : "rgba(0, 0, 0, 0.18)";
    const QString scrollHover = dark ? "rgba(255, 255, 255, 0.36)" : "rgba(0, 0, 0, 0.36)";
    const QString scrollPressed = dark ? "rgba(255, 255, 255, 0.52)" : "rgba(0, 0, 0, 0.52)";
    QPalette colors = qApp->palette();
    colors.setColor(QPalette::Window, QColor(background));
    colors.setColor(QPalette::WindowText, QColor(foreground));
    colors.setColor(QPalette::Base, QColor(surface));
    colors.setColor(QPalette::AlternateBase, QColor(alternate));
    colors.setColor(QPalette::Text, QColor(foreground));
    colors.setColor(QPalette::Button, QColor(surface));
    colors.setColor(QPalette::ButtonText, QColor(foreground));
    colors.setColor(QPalette::Highlight, QColor(dark ? "#30355d" : "#eeedff"));
    colors.setColor(QPalette::HighlightedText, QColor(foreground));
    colors.setColor(QPalette::Disabled, QPalette::Text, QColor(muted));
    colors.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(muted));
    colors.setColor(QPalette::PlaceholderText, QColor(hintForeground));
    setPalette(colors);
    qApp->setPalette(colors);
    const QString baseStyle = QString(
        "QMainWindow,QDialog { background:%1; color:%3; }"
        "QDialog { border:1px solid %4; }"
        "QFrame#dialogTitleBar { background:%1; border:0; }"
        "QFrame#dialogTitleDivider { background:%4; border:0; }"
        "QLabel#dialogWindowTitle { background:transparent; color:%3; font-size:18px; font-weight:600; }"
        "QPushButton#closeDialog { background:transparent; border:1px solid transparent; padding:0; font-size:20px; font-weight:400; }"
        "QPushButton#closeDialog:hover { background:%6; }"
        "QPushButton#closeDialog:pressed { background:%4; }"
        "QPushButton#closeDialog[keyboardFocus=true] { border-color:#818cf8; }"
        "QMenuBar { background:%1; color:%3; border-bottom:1px solid %4; padding:2px 8px; font-size:13px; outline:none; }"
        "QMenuBar::item { background:transparent; padding:4px 8px; border-radius:4px; outline:none; }"
        "QMenuBar::item:selected { background:%6; color:%3; }"
        "QMenuBar::item:pressed { background:%6; }"
        "QWidget { font-size:14px; } QLabel { color:%3; }"
        "QLabel#appMark { font-size:24px; font-weight:700; color:white; background:#4f46e5; border-radius:12px; }"
        "QLabel#heading { font-size:22px; font-weight:700; }"
        "QLabel#panelTitle { font-size:18px; font-weight:700; }"
        "QLabel#selectedMessageTitle { font-size:16px; font-weight:600; color:%3; }"
        "QLabel#catalogResultCount,QLabel#payloadHint,QLabel#catalogEmpty { color:%5; font-size:12px; }"
        "QFrame#messageFooterDivider { background:%4; border:0; }"
        "QSplitter#messageSplitter::handle { background:transparent; border-left:1px solid %4; margin:0 7px; }"
        "QLabel#sectionTitle { font-size:11px; font-weight:700; color:%5; }"
        "QLabel#secondaryText { color:%5; font-size:12px; }"
        "QLabel#profileHint,QLabel#profileSafetyHint { color:%9; font-size:12px; }"
        "QFrame#profileFileCard,QFrame#profileModeCard { background:%2; border:1px solid %4; border-radius:10px; }"
        "QLabel#profileSectionTitle { color:%3; font-size:13px; font-weight:600; background:transparent; }"
        "QLabel#profileFileType { color:%5; background:%6; border-radius:5px; padding:4px 7px; font-size:11px; }"
        "QLabel#profileModeBadge { color:%5; background:%6; border-radius:5px; padding:4px 9px; font-size:11px; font-weight:600; }"
        "QFrame#profileFooterDivider { background:%4; border:0; }"
        "QDialog#importProfileDialog QLabel#profileError { background:%6; border:1px solid %4; border-radius:6px; padding:10px 12px; font-size:12px; }"
        "QDialog#connectionErrorDialog { background:transparent; border:0; }"
        "QFrame#connectionErrorCard { background:%2; border:1px solid %4; border-radius:10px; }"
        "QLabel#dialogHeadline { color:%3; font-size:13px; font-weight:600; background:transparent; }"
        "QLabel#connectionErrorMessage { color:%5; background:transparent; font-size:12px; }"
        "QPushButton#dismissConnectionError { background:#4f46e5; color:white; border:1px solid #4338ca; border-radius:5px; font-size:12px; font-weight:500; }"
        "QPushButton#dismissConnectionError:hover { background:#4338ca; }"
        "QPushButton#dismissConnectionError:pressed { background:#3730a3; }"
        "QPushButton#dismissConnectionError:focus { outline:none; border-color:#818cf8; }"
        "QLabel#summaryBadge { color:%5; background:%6; border:1px solid %4; border-radius:6px; padding:0 10px; font-size:11px; font-weight:500; }"
        "QFrame#connectionBar { background:transparent; border:0; }"
        "QFrame#connectionCard { background:transparent; border:0; }"
        "QFrame#connectionCardDivider { background:%4; border:0; }"
        "QLabel#serialSummary { color:%5; background:%6; border:1px solid %4; border-radius:7px; padding:0 10px; font-size:12px; font-weight:500; }"
        "QLabel#serialFieldLabel { color:%5; font-size:12px; }"
        "QFrame#serialDivider,QFrame#txDivider { background:%4; border:0; }"
        "QMenu#serialMenu { background:%6; border:1px solid %4; border-radius:7px; padding:0; }"
        "QFrame#serialAdvanced { background:transparent; border:0; }"
        "QFrame#dashboardPanel { background:%2; border:1px solid %4; border-radius:14px; }"
        "QFrame#settingsCard,QFrame#shortcutsCard { background:%2; border:1px solid %4; border-radius:8px; }"
        "QFrame#shortcutRowDivider { background:%4; border:0; }"
        "QLabel#keycapBadge { background:%6; color:%3; border:1px solid %4; border-radius:4px; padding:2px 7px; font-weight:600; font-size:11px; min-width:18px; }"
        "QLabel#shortcutDesc { color:%3; font-size:13px; }"
        "QLabel#shortcutSymbol { color:%5; font-size:12px; font-weight:600; }"
        "QLabel#settingsGroupTitle { font-size:13px; font-weight:700; color:%3; }"
        "QCheckBox { background:transparent; color:%3; font-size:13px; outline:none; }"
        "QDialogButtonBox QPushButton { qproperty-icon: none; }"
        "QPushButton { background:%2; color:%3; border:1px solid %4; border-radius:7px; padding:7px 10px; font-weight:500; outline:none; }"
        "QPushButton#runAll,QPushButton#stopAll,QPushButton#deleteAll,QPushButton#pauseDisplay,QPushButton#clearStatistics,QPushButton#toggleExpandRx { padding:0; border-radius:6px; }"
        "QPushButton#sortMessages,QPushButton#sortMessages:hover,QPushButton#sortMessages:pressed,QPushButton#sortMessages:disabled { background:transparent; border:none; padding:0; border-radius:6px; }"
        "QPushButton:hover { background:%6; border-color:#818cf8; }"
        "QPushButton:pressed,QPushButton:checked { background:%6; border-color:%8; }"
        "QPushButton[role=primary] { background:#4f46e5; color:white; border-color:#4f46e5; }"
        "QPushButton[role=primary]:hover { background:#4338ca; border-color:#4338ca; }"
        "QPushButton[role=primary]:pressed { background:#3730a3; border-color:#3730a3; }"
        "QPushButton[role=success] { background:#087f6e; color:white; border-color:#087f6e; }"
        "QPushButton[role=success]:hover { background:#066758; border-color:#066758; }"
        "QPushButton[role=success]:pressed { background:#065446; border-color:#065446; }"
        "QPushButton[role=danger] { color:%7; }"
        "QPushButton:disabled,QPushButton[role=primary]:disabled,QPushButton[role=success]:disabled,QPushButton[role=danger]:disabled { color:%5; background:%6; border-color:%4; }"
        "QLineEdit:focus,QSpinBox:focus,QDoubleSpinBox:focus { border:1px solid #4f46e5; outline:none; }"
        "QPushButton[keyboardFocus=true],QPushButton[role=primary][keyboardFocus=true],QPushButton[role=success][keyboardFocus=true] { border:2px solid #818cf8; padding:6px 9px; outline:none; }"
        "QLineEdit,QSpinBox,QDoubleSpinBox { background:%2; color:%3; border:1px solid %8; border-radius:7px; padding:7px; }"
        "QLineEdit#rxFilter { padding:4px 8px; }"
        "QComboBox { background:%2; color:%3; border:1px solid %8; border-radius:7px; padding:4px 28px 4px 10px; min-height:22px; outline:none; }"
        "QComboBox:editable { padding:4px 28px 4px 7px; }"
        "QComboBox:hover { background:%6; border-color:#818cf8; }"
        "QComboBox:focus,QComboBox:on { border:1px solid #4f46e5; outline:none; }"
        "QComboBox:disabled { color:%5; background:%6; border-color:%4; }"
        "QComboBox::drop-down { subcontrol-origin:padding; subcontrol-position:top right; width:26px; border:0; }"
        "QComboBox::down-arrow { image:none; }"
        "QComboBox QLineEdit,QComboBox QLineEdit:focus,QComboBox QLineEdit:hover { padding:0; margin:0; border:0; outline:none; background:transparent; }"
        "QComboBox QFrame { background:transparent; border:0; }"
        "QPushButton#refreshPorts { padding:0; }"
        "QPushButton#refreshPorts[keyboardFocus=true],QPushButton#pauseDisplay[keyboardFocus=true],QPushButton#clearStatistics[keyboardFocus=true],QPushButton#toggleExpandRx[keyboardFocus=true] { padding:0; }"
        "QSpinBox::up-button,QDoubleSpinBox::up-button { subcontrol-origin:border; subcontrol-position:top right; width:20px; border-left:1px solid %4; border-bottom:1px solid %4; }"
        "QSpinBox::down-button,QDoubleSpinBox::down-button { subcontrol-origin:border; subcontrol-position:bottom right; width:20px; border-left:1px solid %4; }"
        "QSpinBox::up-arrow,QSpinBox::down-arrow,QDoubleSpinBox::up-arrow,QDoubleSpinBox::down-arrow { image:none; }"
        "QComboBox QAbstractItemView { background:%2; color:%3; border:1px solid %4; border-radius:8px; padding:4px; outline:none; selection-background-color:transparent; }"
        "QComboBox QAbstractItemView::item { min-height:28px; border-radius:5px; margin:1px 2px; }"
        "QComboBox QAbstractItemView::item:hover,QComboBox QAbstractItemView::item:selected { background:transparent; }"
        "QListWidget { background:%2; color:%3; selection-background-color:#4f46e5; selection-color:white; padding:3px; }"
        "QListWidget::item { padding:5px 6px; border-radius:3px; }"
        "QListWidget::item:selected { background:#4f46e5; color:white; }"
        "QListWidget::item:hover:!selected { background:%6; }"
        "QAbstractItemView { background:%2; color:%3; alternate-background-color:%6; border:0; border-radius:0; gridline-color:%4; }"
        "QTableView::item { padding:0 6px; border:0; }"
        "QTableWidget#txTable::item,QTableWidget#txTable::item:selected,QTableWidget#txTable::item:hover { padding:0; background:transparent; border:0; }"
        "QTreeView::item { padding:5px 4px; border:0; }"
        "QHeaderView::section { background:%6; color:%5; padding:9px 8px; border:0; border-bottom:1px solid %4; font-weight:600; font-size:12px; }"
        "QTableWidget#payloadFields,QTableWidget#inspector { border:0; border-radius:0; }"
        "QTableWidget#payloadFields::item,QTableWidget#inspector::item { padding:0 10px; border-bottom:1px solid %4; }"
        "QTableWidget#payloadFields QHeaderView::section,QTableWidget#inspector QHeaderView::section { background:%6; color:%3; padding:8px 10px; border-bottom:1px solid %4; font-weight:600; font-size:12px; }"
        "QLineEdit#payloadValueEditor { padding:0 8px; border-radius:5px; font-size:13px; selection-background-color:#4f46e5; selection-color:white; }"
        "QComboBox QLineEdit#payloadValueEditor { padding:0; margin:0; border:0; outline:none; background:transparent; }"
        "QSplitter::handle { background:transparent; }"
        "QFrame#statusBarCard { background:transparent; border:0; min-height:26px; }"
        "QScrollBar:vertical { background:transparent; width:6px; margin:4px 0px 4px 0px; }"
        "QScrollBar:horizontal { background:transparent; height:6px; margin:0px 4px 0px 4px; }"
        "QScrollBar::handle { background:" + scrollNormal + "; border-radius:3px; }"
        "QScrollBar::handle:vertical { min-height:24px; }"
        "QScrollBar::handle:horizontal { min-width:24px; }"
        "QScrollBar::handle:hover { background:" + scrollHover + "; }"
        "QScrollBar::handle:pressed { background:" + scrollPressed + "; }"
        "QScrollBar::add-line,QScrollBar::sub-line { width:0px; height:0px; background:transparent; border:none; }"
        "QScrollBar::add-page,QScrollBar::sub-page { background:transparent; border:none; }"
        "QLabel#validationError, QLabel#profileError { color:%7; }"
    ).arg(background, surface, foreground, border, muted, alternate,
          dark ? "#fca5a5" : "#b91c1c", controlBorder, hintForeground);
    const QString menuStyle = QString(
        "QMenu { background:%1; color:%2; border:1px solid %3; border-radius:8px; padding:6px; font-size:13px; outline:none; }"
        "QMenu::item { min-width:190px; padding:6px 28px 6px 10px; border-radius:5px; }"
        "QMenu#sortMenu { min-width:130px; padding:4px; }"
        "QMenu#sortMenu::item { min-width:125px; padding:5px 12px 5px 6px; border-radius:5px; }"
        "QMenu#sortMenu::indicator { width:0px; height:0px; }"
        "QMenu#sortMenu::item:checked { background:%7; font-weight:500; }"
        "QMenu#sortMenu::item:checked:selected { background:#4f46e5; color:white; }"
        "QMenu::icon { padding-left:8px; }"
        "QMenu::item:selected { background:#4f46e5; color:white; }"
        "QMenu::item:default { color:%4; }"
        "QMenu::item:default:selected { background:%5; color:%4; }"
        "QMenu::item:disabled { color:%6; background:transparent; }"
        "QMenu::item:disabled:selected { color:%6; background:transparent; }"
        "QMenu::separator { height:1px; background:%3; margin:5px 6px; }"
    ).arg(surface, foreground, border,
          dark ? "#fca5a5" : "#b91c1c",
          dark ? "#381e26" : "#fee2e2",
          hintForeground,
          dark ? "rgba(255, 255, 255, 0.09)" : "rgba(0, 0, 0, 0.06)");
    const QString dangerStyle = QString(
        "QPushButton#connect[role=danger],QPushButton#connect[connected=true] { background:%1; color:%2; border:1px solid %3; font-weight:500; }"
        "QPushButton#connect[role=danger]:hover,QPushButton#connect[connected=true]:hover { background:%4; border-color:#ef4444; color:%5; }"
        "QPushButton#connect[role=danger]:pressed,QPushButton#connect[connected=true]:pressed { background:%6; }"
    ).arg(dark ? "rgba(239, 68, 68, 0.12)" : "#fef2f2",
          dark ? "#fca5a5" : "#b91c1c",
          dark ? "rgba(239, 68, 68, 0.28)" : "#fecaca",
          dark ? "rgba(239, 68, 68, 0.22)" : "#fee2e2",
          dark ? "#ffffff" : "#991b1b",
          dark ? "rgba(239, 68, 68, 0.32)" : "#fecaca");
    const QString connectingStyle = QString(
        "QPushButton#connect[role=connecting],QPushButton#connect[role=connecting]:disabled { background:%1; color:#ffffff; border:1px solid %2; font-weight:500; }"
        "QPushButton#connect[role=connecting]:hover { background:%3; border-color:#d97706; color:#ffffff; }"
    ).arg(dark ? "#d97706" : "#d97706",
          dark ? "#f59e0b" : "#d97706",
          dark ? "#b45309" : "#b45309");
    const QString reconnectingStyle = QString(
        "QPushButton#connect[role=reconnecting],QPushButton#connect[reconnecting=true] { background:%1; color:#ffffff; border:1px solid %2; font-weight:500; }"
        "QPushButton#connect[role=reconnecting]:hover,QPushButton#connect[reconnecting=true]:hover { background:%3; border-color:#f97316; color:#ffffff; }"
        "QPushButton#connect[role=reconnecting]:pressed,QPushButton#connect[reconnecting=true]:pressed { background:%4; }"
    ).arg(dark ? "#ea580c" : "#ea580c",
          dark ? "#f97316" : "#ea580c",
          dark ? "#c2410c" : "#c2410c",
          dark ? "#9a3412" : "#9a3412");
    const QString connectionStyle =
        "QPushButton#connect[role=primary] { background:#4f46e5; color:white; border:1px solid #4f46e5; }"
        "QPushButton#connect[role=primary]:hover { background:#4338ca; border-color:#4338ca; }"
        "QPushButton#connect[role=primary]:pressed { background:#3730a3; border-color:#3730a3; }"
        + dangerStyle + connectingStyle + reconnectingStyle;
    const QString selectionStyle = QString(
        "QTableView::item:selected,QTreeView::item:selected { background:%1; color:%2; }"
        "QTreeView::branch:selected { background:%1; }"
        "QTableView::item:hover:!selected,QTreeView::item:hover:!selected { background:%3; }"
    ).arg(dark ? "#2c3550" : "#eeedfa", foreground, alternate);
    const QString dataStyle = QString(
        "QFrame#dataPane { background:%1; border:1px solid %2; border-radius:5px; }"
        "QLabel#dataPaneTitle { background:%3; color:%4; border-bottom:1px solid %2; padding:8px 10px; font-size:12px; font-weight:600; }"
        "QFrame#inspectorHeader { background:%3; border-bottom:1px solid %2; }"
        "QLabel#inspectorMessageTitle { background:transparent; color:%4; font-size:12px; font-weight:600; }"
        "QLabel#inspectorSource { color:%4; font-size:12px; }"
        "QLineEdit#paramFilter { padding:0 8px; border:1px solid %2; border-radius:5px; font-size:12px; background:%1; color:%4; }"
        "QLabel#displayPaused { color:%7; background:%1; padding:2px 6px; border-radius:4px; font-size:11px; }"
        "QTreeWidget#rxTree { background:%1; alternate-background-color:%5; outline:none; }"
        "QTableWidget#inspector { background:%1; border:0; }"
        "QTableWidget#txTable { background:transparent; }"
        "QTableView#txTable::item:selected,QTableView#txTable::item:hover { background:transparent; }"
        "QTreeWidget#rxTree::item { border-bottom:1px solid %5; height:34px; padding:0 8px; outline:none; }"
        "QTreeWidget#rxTree::item:focus { outline:none; border:none; }"
        "QTreeWidget#rxTree::item:selected:focus { outline:none; border:none; }"
        "QHeaderView::section { background:%6; color:%4; border-bottom:1px solid %2; border-right:1px solid %5; padding:4px 10px; }"
        "QSplitter#rxSplit::handle { background:transparent; border:0; }"
        "QSplitter#inspectorContentSplitter::handle { background:%2; margin:0; }"
        "QFrame#telemetryPlotter { background:%1; border:0; }"
        "QFrame#plotterHeader { background:%3; border-bottom:1px solid %2; }"
        "QLabel#plotterTitle { color:%4; font-size:12px; font-weight:600; }"
        "QPushButton#plotToggleBtn { background:%3; color:%4; border:1px solid %2; border-radius:4px; font-size:11px; padding:0 8px; }"
        "QPushButton#plotToggleBtn:checked { background:#4f46e5; color:#ffffff; border-color:#4338ca; }"
        "QPushButton#plotAutoScaleBtn,QPushButton#plotPauseBtn,QPushButton#plotClearBtn,QPushButton#plotExportBtn,QPushButton#plotCloseBtn,QPushButton#plotPopoutBtn {"
        "  background:%3; color:%4; border:1px solid %2; border-radius:3px; padding:2px 7px; font-size:11px; }"
        "QPushButton#plotAutoScaleBtn:checked,QPushButton#plotPauseBtn:checked {"
        "  background:#4f46e5; color:#ffffff; border-color:#4338ca; }"
        "QPushButton#plotCloseBtn,QPushButton#plotPopoutBtn { padding:0; }"
        "QComboBox#plotTimeRange { background:%1; color:%4; border:1px solid %2; border-radius:3px; font-size:11px; padding:0 4px; }"
    ).arg(dark ? "#162134" : "#ffffff", dark ? "#35445d" : "#d1dbea",
          dark ? "#202d42" : "#eef2f9", dark ? "#d6dfee" : "#40536e",
          dark ? "#1d2b41" : "#f4f7fb", dark ? "#1d293c" : "#f7f9fc", dark ? "#f3d698" : "#805c17");
    const QString rxButtonStyle = QString(
        "QPushButton#pauseDisplay[paused=true] { background:%1; border:1px solid %2; }"
        "QPushButton#pauseDisplay[paused=true]:hover { background:%3; border-color:%2; }"
        "QPushButton#clearStatistics:hover { background:%4; border-color:%5; }"
        "QPushButton#clearStatistics:pressed { background:%6; border-color:%7; }"
    ).arg(dark ? "rgba(245, 158, 11, 0.22)" : "rgba(245, 158, 11, 0.15)",
          dark ? "#f59e0b" : "#d97706",
          dark ? "rgba(245, 158, 11, 0.32)" : "rgba(245, 158, 11, 0.25)",
          dark ? "rgba(239, 68, 68, 0.16)" : "rgba(239, 68, 68, 0.12)",
          dark ? "#ef4444" : "#dc2626",
          dark ? "rgba(239, 68, 68, 0.28)" : "rgba(239, 68, 68, 0.20)",
          dark ? "#dc2626" : "#b91c1c");
    setStyleSheet(baseStyle + menuStyle + connectionStyle + selectionStyle + dataStyle + rxButtonStyle);
    for (const auto &name : {"addMessage", "startTx", "sendOnce", "stopTx", "refreshPorts", "serialSettings", "editMessage", "duplicateMessage", "removeMessage", "runAll", "stopAll", "deleteAll", "sortMessages", "pauseDisplay", "clearStatistics", "toggleExpandRx"})
    {
        auto *control = findChild<QPushButton *>(name);
        if (!control)
        {
            continue;
        }
        if (QString(name) == "toggleExpandRx")
        {
            updateRxExpandIcon();
            continue;
        }
        if (QString(name) == "pauseDisplay")
        {
            const bool isPaused = control->property("paused").toBool();
            const QColor pauseColor = isPaused
                ? (dark ? QColor("#fbbf24") : QColor("#d97706"))
                : foreground;
            QIcon icon = actionIcon(isPaused ? "resumeDisplay" : "pauseDisplay", pauseColor);
            icon.addPixmap(actionIcon(isPaused ? "resumeDisplay" : "pauseDisplay", QColor(muted)).pixmap(18, 18), QIcon::Disabled);
            control->setIcon(icon);
            control->setIconSize(QSize(18, 18));
            continue;
        }
        if (QString(name) == "sortMessages")
        {
            updateSortButtonIcon(sortButton && sortButton->underMouse());
            continue;
        }
        const QString role = control->property("role").toString();
        const QColor iconColor(role == "primary" || role == "success" ? "#ffffff" :
                               role == "danger" ? (dark ? "#fca5a5" : "#b91c1c") : foreground);
        QIcon icon = actionIcon(name, iconColor);
        const QSize iconSize(QString(name) == "clearStatistics" ? QSize(18, 18) : QSize(16, 16));
        icon.addPixmap(actionIcon(name, QColor(muted)).pixmap(iconSize.width(), iconSize.height()), QIcon::Disabled);
        control->setIcon(icon);
        control->setIconSize(iconSize);
    }
    const QColor disabledIconColor(dark ? "#526079" : "#94a3b8");
    const QColor dangerColor(dark ? "#fca5a5" : "#b91c1c");
    for (auto *action : {startAction, stopAction, onceAction, editAction, duplicateAction, removeAction})
    {
        if (action)
        {
            const QString name = action->objectName();
            const QColor actionFg = (name == "removeMessage") ? dangerColor : foreground;
            QIcon icon = actionIcon(name, actionFg);
            icon.addPixmap(actionIcon(name, Qt::white).pixmap(32, 32), QIcon::Selected);
            icon.addPixmap(actionIcon(name, Qt::white).pixmap(16, 16), QIcon::Selected);
            icon.addPixmap(actionIcon(name, disabledIconColor).pixmap(32, 32), QIcon::Disabled);
            icon.addPixmap(actionIcon(name, disabledIconColor).pixmap(16, 16), QIcon::Disabled);
            action->setIcon(icon);
        }
    }
    if (txSummary)
    {
        txSummary->setProperty("darkTheme", dark);
        txSummary->update();
    }
    if (connectionStatusPill)
    {
        connectionStatusPill->setProperty("darkTheme", dark);
        connectionStatusPill->update();
    }
    if (metricsWidget)
    {
        metricsWidget->setDarkTheme(dark);
    }
    if (lastError)
    {
        lastError->setDarkTheme(dark);
    }
    if (copyFeedbackBadge)
    {
        static_cast<CopyFeedbackBadge *>(copyFeedbackBadge)->refreshTheme(dark);
    }
    if (rxThroughput && connected)
    {
        updateRxThroughputStyle(lastRxUtil);
    }
    if (rxFilter)
    {
        if (auto *clearBtn = rxFilter->findChild<QToolButton *>())
        {
            clearBtn->setIcon(clearButtonIcon(dark));
        }
    }
    if (paramFilter)
    {
        if (auto *clearBtn = paramFilter->findChild<QToolButton *>())
        {
            clearBtn->setIcon(clearButtonIcon(dark));
        }
    }
    if (txUndoToast)
    {
        txUndoToast->updateThemeStyle(dark);
    }
    applying = false;
}

