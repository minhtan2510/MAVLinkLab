#pragma once

#include "core.h"
#include <QByteArray>
#include <QString>

/** @brief Versioned CBOR serialization for portable TX configuration only.
 * Schema 1 root map: format (text, "MAVLinkLab TX Profile"), version (integer, 1),
 * dialect (text, "ardupilotmega"), messages (array, at most 128 maps).
 * Each message map: id, sysid, compid (integers), version1 (boolean), hz (number),
 * payload (byte string containing exactly the generated full payload length).
 * Unknown/duplicate keys, unsupported messages and incompatible versions fail.
 * Runtime keys, active state, counters, errors and timing are never persisted.
 */
namespace Profile
{
inline constexpr qsizetype MaxFileSize = 1024 * 1024;
inline constexpr size_t MaxMessages = 128;

/** @brief Encode validated entries as schema 1 CBOR, excluding runtime state.
 * @param error Failure description, cleared on success.
 * @return CBOR data on success, or an empty byte array on failure.
 */
QByteArray encode(const std::vector<Mav::TxEntry> &entries, QString &error);

/** @brief Decode and validate a complete profile before changing entries.
 * @param entries Replaced on success with stopped entries and zero runtime state.
 * @param error Failure description, cleared on success.
 * @return True on success; malformed or incompatible input leaves entries intact.
 */
bool decode(const QByteArray &data, std::vector<Mav::TxEntry> &entries, QString &error);
}
