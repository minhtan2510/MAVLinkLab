#include "profile.h"
#include <QCborArray>
#include <QCborMap>
#include <QCborValue>
#include <QCoreApplication>
#include <iostream>
#include <limits>
#include <stdexcept>

/** @brief Fail an expectation in both debug and release builds. */
static void check(bool condition, const char *description)
{
    if (!condition)
    {
        throw std::runtime_error(description);
    }
}

/** @brief Create a fully populated configuration with deliberately dirty runtime state. */
static Mav::TxEntry fixture(uint32_t id = 0)
{
    Mav::TxEntry entry;
    entry.id = id;
    entry.sysid = 42;
    entry.compid = 0;
    entry.hz = 0.1;
    entry.payload.resize(mavlink_get_msg_entry(id)->max_msg_len);
    for (size_t i = 0; i < entry.payload.size(); ++i)
    {
        entry.payload[i] = uint8_t((i % 3 == 0) ? 0 : 255 - i);
    }
    entry.key = 123;
    entry.active = true;
    entry.count = 500;
    entry.deadline = 12345;
    entry.error = "runtime error";
    entry.measured.add(1);
    return entry;
}

/** @brief Exercise strict portable schema boundaries and atomic failed decoding. */
int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    try
    {
        QString error;
        const auto original = fixture();
        const QByteArray bytes = Profile::encode({original, original}, error);
        check(!bytes.isEmpty() && error.isEmpty(), "Encode valid messages");
        std::vector<Mav::TxEntry> decoded;
        check(Profile::decode(bytes, decoded, error), "Decode round trip");
        check(decoded.size() == 2 && decoded[0].payload == original.payload && decoded[1].payload == original.payload,
              "Preserve exact binary bytes including NULs and duplicate messages");
        check(decoded[0].id == original.id && decoded[0].sysid == 42 && decoded[0].compid == 0 &&
              decoded[0].hz == 0.1 && decoded[0].version1 == original.version1, "Preserve configuration");
        check(!decoded[0].active && decoded[0].count == 0 && decoded[0].deadline == 0 && decoded[0].key == 0 &&
              decoded[0].error.empty() && decoded[0].measured.times.empty(), "Discard all runtime state");
        const QCborMap valid = QCborValue::fromCbor(bytes).toMap();
        const QCborMap validMessage = valid.value(QStringLiteral("messages")).toArray().at(0).toMap();
        const auto reject = [&](const QByteArray &input)
        {
            std::vector<Mav::TxEntry> output{original};
            check(!Profile::decode(input, output, error) && !error.isEmpty(), "Reject malformed input with error");
            check(output.size() == 1 && output[0].key == original.key && output[0].active &&
                  output[0].payload == original.payload, "Failure leaves existing entries intact");
        };
        const auto rejectRoot = [&](const QString &key, const QCborValue &value)
        {
            QCborMap root = valid;
            root.insert(key, value);
            reject(QCborValue(root).toCbor());
        };
        const auto rejectMessage = [&](const QString &key, const QCborValue &value)
        {
            QCborMap message = validMessage;
            message.insert(key, value);
            QCborMap root = valid;
            root.insert(QStringLiteral("messages"), QCborArray{message});
            reject(QCborValue(root).toCbor());
        };
        reject({});
        reject(QByteArray(Profile::MaxFileSize + 1, '\0'));
        reject(bytes.left(bytes.size() - 1));
        reject(bytes + QByteArray(1, '\0'));
        reject(QCborValue(12).toCbor());
        rejectRoot(QStringLiteral("format"), QStringLiteral("other"));
        rejectRoot(QStringLiteral("version"), 2);
        rejectRoot(QStringLiteral("version"), 1.0);
        rejectRoot(QStringLiteral("dialect"), QStringLiteral("common"));
        rejectRoot(QStringLiteral("messages"), QCborMap{});
        rejectRoot(QStringLiteral("extra"), true);
        rejectMessage(QStringLiteral("id"), 0xffffff);
        rejectMessage(QStringLiteral("id"), -1);
        rejectMessage(QStringLiteral("id"), 0.0);
        rejectMessage(QStringLiteral("sysid"), 0);
        rejectMessage(QStringLiteral("sysid"), 256);
        rejectMessage(QStringLiteral("compid"), -1);
        rejectMessage(QStringLiteral("compid"), 256);
        rejectMessage(QStringLiteral("version1"), 1);
        rejectMessage(QStringLiteral("hz"), QStringLiteral("1"));
        rejectMessage(QStringLiteral("hz"), 0.09);
        rejectMessage(QStringLiteral("hz"), 1001.0);
        rejectMessage(QStringLiteral("hz"), std::numeric_limits<double>::infinity());
        rejectMessage(QStringLiteral("hz"), std::numeric_limits<double>::quiet_NaN());
        rejectMessage(QStringLiteral("payload"), QCborArray{});
        rejectMessage(QStringLiteral("payload"), QByteArray(1, '\0'));
        rejectMessage(QStringLiteral("extra"), true);
        QCborMap badLastMessage = validMessage;
        badLastMessage.insert(QStringLiteral("sysid"), 0);
        rejectRoot(QStringLiteral("messages"), QCborArray{validMessage, badLastMessage});
        QCborMap integerRate = validMessage;
        integerRate.insert(QStringLiteral("hz"), 1);
        QCborMap integerRateRoot = valid;
        integerRateRoot.insert(QStringLiteral("messages"), QCborArray{integerRate});
        check(Profile::decode(QCborValue(integerRateRoot).toCbor(), decoded, error) && decoded[0].hz == 1.0,
              "Accept numeric integer rates without accepting numeric strings");
        QCborMap missing = valid;
        missing.remove(QStringLiteral("version"));
        reject(QCborValue(missing).toCbor());
        QCborArray excessive;
        for (size_t i = 0; i <= Profile::MaxMessages; ++i)
        {
            excessive.append(validMessage);
        }
        rejectRoot(QStringLiteral("messages"), excessive);
        // Two identical format keys must fail even when their values agree.
        QByteArray duplicate = bytes;
        check(uint8_t(duplicate[0]) == 0xa4, "Expected four-key definite map fixture");
        duplicate[0] = char(0xa5);
        duplicate += QCborValue(QStringLiteral("format")).toCbor();
        duplicate += QCborValue(QStringLiteral("MAVLinkLab TX Profile")).toCbor();
        reject(duplicate);
        auto extended = fixture(300);
        extended.version1 = true;
        check(Profile::encode({extended}, error).isEmpty(), "Reject extended ID on MAVLink1 export");
        QCborMap extendedMessage = validMessage;
        extendedMessage.insert(QStringLiteral("id"), 300);
        extendedMessage.insert(QStringLiteral("version1"), true);
        extendedMessage.insert(QStringLiteral("payload"), QByteArray(qsizetype(extended.payload.size()), '\0'));
        rejectRoot(QStringLiteral("messages"), QCborArray{extendedMessage});
        auto invalid = original;
        invalid.sysid = 0;
        check(Profile::encode({invalid}, error).isEmpty(), "Reject zero sender ID on export");
        std::vector<Mav::TxEntry> many(Profile::MaxMessages, original);
        check(Profile::decode(Profile::encode(many, error), decoded, error) && decoded.size() == Profile::MaxMessages,
              "Accept exact message limit");
        many.push_back(original);
        check(Profile::encode(many, error).isEmpty(), "Reject excessive export");
        check(Profile::decode(Profile::encode({}, error), decoded, error) && decoded.empty(), "Empty profile is valid");
        std::cout << "Profile tests passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
