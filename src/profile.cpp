#include "profile.h"
#include <QCborArray>
#include <QCborMap>
#include <QCborStreamReader>
#include <QCborValue>
#include <QSet>

namespace
{
/** @brief Require precisely the schema keys, rejecting duplicate and unknown keys. */
bool exactKeys(const QCborMap &map, const QStringList &keys)
{
    if (map.size() != keys.size())
    {
        return false;
    }
    QSet<QString> seen;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
    {
        if (!it.key().isString())
        {
            return false;
        }
        const QString key = it.key().toString();
        if (!keys.contains(key) || seen.contains(key))
        {
            return false;
        }
        seen.insert(key);
    }
    return true;
}

/** @brief Test an integer without accepting floating point or coercing types. */
bool integerIn(const QCborValue &value, qint64 minimum, qint64 maximum)
{
    return value.isInteger() && value.toInteger() >= minimum && value.toInteger() <= maximum;
}
}

QByteArray Profile::encode(const std::vector<Mav::TxEntry> &entries, QString &error)
{
    error.clear();
    if (entries.size() > MaxMessages)
    {
        error = QStringLiteral("A profile supports at most 128 messages.");
        return {};
    }
    QCborArray messages;
    for (size_t index = 0; index < entries.size(); ++index)
    {
        const auto &entry = entries[index];
        if (entry.sysid == 0 || !Mav::validTx(entry))
        {
            error = QStringLiteral("Message %1 has an invalid TX configuration.").arg(index + 1);
            return {};
        }
        QCborMap message;
        message.insert(QStringLiteral("id"), qint64(entry.id));
        message.insert(QStringLiteral("sysid"), entry.sysid);
        message.insert(QStringLiteral("compid"), entry.compid);
        message.insert(QStringLiteral("version1"), entry.version1);
        message.insert(QStringLiteral("hz"), entry.hz);
        message.insert(QStringLiteral("payload"), QByteArray(reinterpret_cast<const char *>(entry.payload.data()), qsizetype(entry.payload.size())));
        messages.append(message);
    }
    QCborMap root;
    root.insert(QStringLiteral("format"), QStringLiteral("MAVLinkLab TX Profile"));
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("dialect"), QStringLiteral("ardupilotmega"));
    root.insert(QStringLiteral("messages"), messages);
    return QCborValue(root).toCbor();
}

bool Profile::decode(const QByteArray &data, std::vector<Mav::TxEntry> &entries, QString &error)
{
    error.clear();
    if (data.isEmpty() || data.size() > MaxFileSize)
    {
        error = QStringLiteral("The profile is empty or exceeds the 1 MiB size limit.");
        return false;
    }
    QCborStreamReader reader(data);
    const QCborValue value = QCborValue::fromCbor(reader);
    if (reader.lastError() != QCborError::NoError || reader.currentOffset() != data.size() || !value.isMap())
    {
        error = QStringLiteral("The profile contains malformed CBOR or trailing data.");
        return false;
    }
    const QCborMap root = value.toMap();
    if (!exactKeys(root, {QStringLiteral("format"), QStringLiteral("version"), QStringLiteral("dialect"), QStringLiteral("messages")}) ||
        !root.value(QStringLiteral("format")).isString() || root.value(QStringLiteral("format")).toString() != QStringLiteral("MAVLinkLab TX Profile") ||
        !integerIn(root.value(QStringLiteral("version")), 1, 1) ||
        !root.value(QStringLiteral("dialect")).isString() || root.value(QStringLiteral("dialect")).toString() != QStringLiteral("ardupilotmega") ||
        !root.value(QStringLiteral("messages")).isArray())
    {
        error = QStringLiteral("Unsupported profile format, schema version or dialect.");
        return false;
    }
    const QCborArray messages = root.value(QStringLiteral("messages")).toArray();
    if (messages.size() > qsizetype(MaxMessages))
    {
        error = QStringLiteral("A profile supports at most 128 messages.");
        return false;
    }
    std::vector<Mav::TxEntry> decoded;
    decoded.reserve(size_t(messages.size()));
    for (qsizetype index = 0; index < messages.size(); ++index)
    {
        const auto item = messages.at(index);
        const QCborMap message = item.toMap();
        const auto rate = message.value(QStringLiteral("hz"));
        if (!item.isMap() || !exactKeys(message, {QStringLiteral("id"), QStringLiteral("sysid"), QStringLiteral("compid"), QStringLiteral("version1"), QStringLiteral("hz"), QStringLiteral("payload")}) ||
            !integerIn(message.value(QStringLiteral("id")), 0, 0xffffff) ||
            !integerIn(message.value(QStringLiteral("sysid")), 1, 255) ||
            !integerIn(message.value(QStringLiteral("compid")), 0, 255) ||
            !message.value(QStringLiteral("version1")).isBool() ||
            (!rate.isInteger() && !rate.isDouble()) ||
            !message.value(QStringLiteral("payload")).isByteArray())
        {
            error = QStringLiteral("Message %1 has invalid fields or field types.").arg(index + 1);
            return false;
        }
        Mav::TxEntry entry;
        entry.id = uint32_t(message.value(QStringLiteral("id")).toInteger());
        entry.sysid = uint8_t(message.value(QStringLiteral("sysid")).toInteger());
        entry.compid = uint8_t(message.value(QStringLiteral("compid")).toInteger());
        entry.version1 = message.value(QStringLiteral("version1")).toBool();
        entry.hz = rate.toDouble();
        const QByteArray payload = message.value(QStringLiteral("payload")).toByteArray();
        entry.payload.assign(payload.begin(), payload.end());
        if (!Mav::validTx(entry))
        {
            error = QStringLiteral("Message %1 has an unsupported ID, version, rate or payload length.").arg(index + 1);
            return false;
        }
        decoded.push_back(std::move(entry));
    }
    entries = std::move(decoded);
    return true;
}
