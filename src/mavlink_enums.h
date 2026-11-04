#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Mav
{

/** @brief Single entry of a MAVLink enumeration. */
struct EnumEntry
{
    const char *name;
    uint64_t value;
    const char *description;
};

/**
 * @brief Get the enum type name for a specific message field.
 * @param msgid Message identifier.
 * @param fieldName Name of the field.
 * @return Enum type name, or empty string if not an enum field.
 */
std::string fieldEnumType(uint32_t msgid, const std::string &fieldName);

/**
 * @brief Get list of enum entries for a given enum type name.
 * @param enumName Name of the enum type (e.g. "MAV_COMPONENT").
 * @return Vector of enum entries, or empty vector if none.
 */
const std::vector<EnumEntry> &enumEntries(const std::string &enumName);

/**
 * @brief Get list of enum entries for a given message field.
 * @param msgid Message identifier.
 * @param fieldName Name of the field.
 * @return Vector of enum entries, or empty vector if none.
 */
const std::vector<EnumEntry> &fieldEnumEntries(uint32_t msgid, const std::string &fieldName);

/**
 * @brief Lookup integer value from an enum symbol name.
 * @param symbol The symbol string (e.g. "MAV_PARAM_TYPE_REAL32" or "MAV_PARAM_TYPE_REAL32 (9)").
 * @return Integer value if recognized, std::nullopt otherwise.
 */
std::optional<uint64_t> lookupEnumValue(const std::string &symbol);

/**
 * @brief Lookup enum symbol name from value for an enum type name.
 * @param enumName Name of the enum type (e.g. "MAV_COMPONENT").
 * @param value Integer value.
 * @return Enum entry name or empty string if not found.
 */
std::string lookupEnumNameByType(const std::string &enumName, uint64_t value);

/**
 * @brief Lookup enum symbol name from value for a field.
 * @param msgid Message identifier.
 * @param fieldName Name of the field.
 * @param value Integer value.
 * @return Enum entry name or empty string if not found.
 */
std::string lookupEnumName(uint32_t msgid, const std::string &fieldName, uint64_t value);

} // namespace Mav
