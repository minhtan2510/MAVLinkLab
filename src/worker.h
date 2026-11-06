#pragma once
#include "core.h"
#include <QByteArray>
#include <QObject>
#include <QElapsedTimer>
#include <QSerialPort>
#include <QTimer>
#include <memory>

struct PortConfig
{
    QString port;
    int baud = 115200;
    QSerialPort::DataBits data = QSerialPort::Data8;
    QSerialPort::Parity parity = QSerialPort::NoParity;
    QSerialPort::StopBits stop = QSerialPort::OneStop;
    QSerialPort::FlowControl flow = QSerialPort::NoFlowControl;
    bool autoReconnect = false;
    bool deassertControlLines = true;
};
Q_DECLARE_METATYPE(PortConfig)
Q_DECLARE_METATYPE(Mav::TxEntry)

struct RxView
{
    mavlink_message_t message{};
    uint64_t count = 0;
    double hz = 0;
    double age = 0;
};
struct TxView
{
    Mav::TxEntry entry;
    double measuredHz = 0;
};
struct Snapshot
{
    std::vector<RxView> rx;
    std::vector<TxView> tx;
    std::map<std::pair<uint32_t, uint8_t>, Mav::ParamCollection> params;
    bool connected = false;
    uint64_t crcErrors = 0;
    uint64_t overflow = 0;
    double rxBytesPerSecond = 0;
    double txBytesPerSecond = 0;
    size_t pending = 0;
};
using SnapshotPtr = std::shared_ptr<const Snapshot>;
Q_DECLARE_METATYPE(SnapshotPtr)

class SerialWorker : public QObject
{
    Q_OBJECT
public:
    /** @brief Construct a worker without creating thread-affine I/O resources. */
    explicit SerialWorker(QObject *parent = nullptr);
public slots:
    /** @brief Create timers and QSerialPort in the worker's owning thread. */
    void initialize();
    /** @brief Configure and open a port; no application TX occurs on connect. */
    void openPort(PortConfig config);
    /** @brief Stop TX and discard pending bytes before closing the port. */
    void closePort();
    /** @brief Add or replace a validated, stopped TX entry. */
    void saveEntry(Mav::TxEntry entry);
    /** @brief Remove an entry and cancel pending output. */
    void removeEntry(quint64 key);
    /** @brief Start or stop one periodic message. */
    void setActive(quint64 key, bool active);
    /**
     * @brief Update the target transmission frequency for an entry.
     * @param key Identifier of the entry to modify.
     * @param hz New target frequency in Hertz (0.1 to 1000).
     */
    void setRate(quint64 key, double hz);
    /** @brief Serialize and queue one explicitly requested message. */
    void sendOnce(quint64 key);
    /** @brief Send raw MAVLink message immediately over the serial port. */
    void sendRawMessage(mavlink_message_t message);
    /** @brief Request all parameters from the specified system/component. */
    void requestParameters(uint8_t targetSystem, uint8_t targetComponent);
    /** @brief Set an individual parameter on the target system/component. */
    void setParameter(uint8_t targetSystem, uint8_t targetComponent, const QString &paramId, float wireValue, uint8_t paramType);
    /** @brief Stop all periodic transmission and cancel pending output. */
    void stopAll();
    /** @brief Start every entry only if the complete periodic load fits the open link. */
    void runAll();
    /** @brief Stop transmission, discard pending output and remove all TX entries. */
    void deleteAll();
    /** @brief Validate and atomically import stopped TX definitions using fresh keys.
     * @param data Encoded profile contents.
     * @param append Preserve existing entries when true; otherwise replace them.
     * @param minimumKey First key not yet allocated by queued GUI commands.
     */
    void importProfile(QByteArray data, bool append, quint64 minimumKey = 1);
    /** @brief Export authoritative TX definitions without runtime state. */
    void exportProfile();
    /** @brief Clear RX statistics and parser state. */
    void clearStatistics();
    /** @brief Emit one bounded snapshot in response to a GUI request. */
    void requestSnapshot();
    /**
     * @brief Enable or disable automatic reconnection on unexpected link drop.
     * @param enabled True to automatically retry connection when link is dropped.
     */
    void setAutoReconnect(bool enabled);
    /** @brief Set control-line policy for the next open without changing an active link. */
    void setDeassertControlLines(bool enabled);
signals:
    void stateChanged(QString state, bool connected);
    void failure(QString message);
    void snapshotReady(SnapshotPtr snapshot);
    void profileExported(QByteArray data);
    void profileExportFailed(QString error);
    void profileImportFailed(QString error);
    void profileImported(int count, bool append, quint64 nextKey);
private:
    /** @brief Deassert and verify host control lines; return an error or an empty string.
     * @note Hardware flow control retains driver ownership of RTS. PTYs have no modem lines.
     * @warning Post-open deassertion cannot prevent a transient pulse during kernel open.
     */
    QString prepareControlLines();
    /** @brief Drain a bounded input chunk through the official parser. */
    void readAvailable();
    /** @brief Advance scheduled transmissions and flush accepted bytes. */
    void tick();
    /** @brief Queue a frame after serialization and capacity checks. */
    void enqueue(Mav::TxEntry &entry);
    /** @brief Handle partial writes with a bounded Qt output buffer. */
    void pump();
    /** @brief Cancel application and Qt pending output. */
    void discardOutput();
    /** @brief Report an error without losing its reason. */
    void fail(const QString &message);
    /**
     * @brief Handle unexpected serial drop and transition to reconnecting state if configured.
     * @param reason Failure message or error description.
     */
    void handleUnexpectedDisconnect(const QString &reason);
    /** @brief Periodic attempt to rediscover and reopen the configured serial port. */
    void attemptReconnect();

    QSerialPort *port = nullptr;
    QTimer *timer = nullptr;
    QTimer *reconnectTimer = nullptr;
    QElapsedTimer clock;
    PortConfig config;
    Mav::Parser parser;
    Mav::Receiver receiver;
    Mav::WriteQueue queue;
    std::deque<uint64_t> owners;
    std::vector<Mav::TxEntry> entries;
    std::map<std::pair<uint8_t, uint8_t>, mavlink_status_t> txStatuses;
    uint64_t rxBytes = 0;
    uint64_t txBytes = 0;
    int64_t lastReport = 0;
    bool closing = false;
    bool reconnecting = false;
};
