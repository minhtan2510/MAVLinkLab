#!/usr/bin/env python3
"""
Generate MAVLink enum lookup tables and field mappings from official XML definitions.
Usage:
    python3 tools/generate_mavlink_enums.py [definitions_dir]
"""

import sys
import os
import xml.etree.ElementTree as ET
from pathlib import Path


def escape_c_string(s: str) -> str:
    """Escape string for C string literal."""
    if not s:
        return ""
    # Flatten multi-line descriptions into a single clean line
    cleaned = " ".join(s.split())
    cleaned = cleaned.replace("\\", "\\\\").replace('"', '\\"')
    return cleaned


def parse_xml_definitions(definitions_dir: Path):
    """Parse all dialect XML files in the definitions directory."""
    enums = {}          # enum_name -> list of (entry_name, entry_value, description)
    field_mappings = {} # (msgid, field_name) -> enum_name
    all_symbols = {}    # symbol_name -> value

    # We inspect standard MAVLink XML files in order: minimal, common, ardupilotmega, and others if present
    xml_files = []
    preferred_order = ["minimal.xml", "common.xml", "ardupilotmega.xml"]
    for name in preferred_order:
        p = definitions_dir / name
        if p.exists():
            xml_files.append(p)
    
    for p in sorted(definitions_dir.glob("*.xml")):
        if p not in xml_files:
            xml_files.append(p)

    for xml_path in xml_files:
        try:
            tree = ET.parse(xml_path)
            root = tree.getroot()
        except Exception as e:
            print(f"Warning: Failed to parse {xml_path}: {e}", file=sys.stderr)
            continue

        # Extract enums
        for enum_el in root.findall(".//enum"):
            enum_name = enum_el.get("name")
            if not enum_name:
                continue
            
            if enum_name not in enums:
                enums[enum_name] = []

            auto_val = 0
            for entry_el in enum_el.findall("entry"):
                entry_name = entry_el.get("name")
                if not entry_name:
                    continue

                raw_val = entry_el.get("value")
                if raw_val is not None:
                    raw_val = raw_val.strip()
                    try:
                        if raw_val.startswith("0x") or raw_val.startswith("0X"):
                            val = int(raw_val, 16)
                        elif "<<" in raw_val:
                            # Evaluate simple bit-shifts
                            parts = raw_val.split("<<")
                            val = int(parts[0].strip(), 0) << int(parts[1].strip(), 0)
                        else:
                            val = int(raw_val, 10)
                    except ValueError:
                        val = auto_val
                else:
                    val = auto_val

                auto_val = val + 1
                desc_el = entry_el.find("description")
                desc = desc_el.text if desc_el is not None and desc_el.text else ""

                # Avoid duplicate entry names within the same enum
                if not any(e[0] == entry_name for e in enums[enum_name]):
                    enums[enum_name].append((entry_name, val, desc))
                all_symbols[entry_name] = val

        # Extract message fields with enum attribute
        for msg_el in root.findall(".//message"):
            msg_id_str = msg_el.get("id")
            if not msg_id_str:
                continue
            try:
                msgid = int(msg_id_str, 0)
            except ValueError:
                continue

            for field_el in msg_el.findall("field"):
                field_name = field_el.get("name")
                enum_name = field_el.get("enum")
                if field_name and enum_name:
                    field_mappings[(msgid, field_name)] = enum_name

    return enums, field_mappings, all_symbols


def generate_header(header_path: Path):
    """Generate mavlink_enums.h header file."""
    content = """#pragma once

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
 * @brief Lookup enum symbol name from value for a field.
 * @param msgid Message identifier.
 * @param fieldName Name of the field.
 * @param value Integer value.
 * @return Enum entry name or empty string if not found.
 */
std::string lookupEnumName(uint32_t msgid, const std::string &fieldName, uint64_t value);

} // namespace Mav
"""
    with open(header_path, "w", encoding="utf-8") as f:
        f.write(content)


def generate_source(source_path: Path, enums, field_mappings, all_symbols):
    """Generate mavlink_enums.cpp source file in BSD/Allman style."""
    lines = []
    lines.append('#include "mavlink_enums.h"')
    lines.append('#include <unordered_map>')
    lines.append('#include <algorithm>')
    lines.append('')
    lines.append('namespace Mav')
    lines.append('{')
    lines.append('')
    lines.append('namespace')
    lines.append('{')
    lines.append('struct FieldKey')
    lines.append('{')
    lines.append('    uint32_t msgid;')
    lines.append('    std::string field;')
    lines.append('    bool operator==(const FieldKey &other) const')
    lines.append('    {')
    lines.append('        return msgid == other.msgid && field == other.field;')
    lines.append('    }')
    lines.append('};')
    lines.append('')
    lines.append('struct FieldKeyHash')
    lines.append('{')
    lines.append('    std::size_t operator()(const FieldKey &k) const')
    lines.append('    {')
    lines.append('        return std::hash<uint32_t>()(k.msgid) ^ (std::hash<std::string>()(k.field) << 1);')
    lines.append('    }')
    lines.append('};')
    lines.append('')
    lines.append('const std::unordered_map<FieldKey, std::string, FieldKeyHash> &fieldToEnumMap()')
    lines.append('{')
    lines.append('    static const std::unordered_map<FieldKey, std::string, FieldKeyHash> map = {')

    for (msgid, field), enum_name in sorted(field_mappings.items()):
        lines.append(f'        {{{{{msgid}, "{field}"}}, "{enum_name}"}},')

    lines.append('    };')
    lines.append('    return map;')
    lines.append('}')
    lines.append('')
    lines.append('const std::unordered_map<std::string, std::vector<EnumEntry>> &enumDefinitions()')
    lines.append('{')
    lines.append('    static const std::unordered_map<std::string, std::vector<EnumEntry>> defs = {')

    for enum_name in sorted(enums.keys()):
        entries = enums[enum_name]
        lines.append(f'        {{"{enum_name}", {{')
        for entry_name, val, desc in entries:
            escaped_desc = escape_c_string(desc)
            lines.append(f'            {{"{entry_name}", {val}ULL, "{escaped_desc}"}},')
        lines.append('        }},')

    lines.append('    };')
    lines.append('    return defs;')
    lines.append('}')
    lines.append('')
    lines.append('const std::unordered_map<std::string, uint64_t> &symbolMap()')
    lines.append('{')
    lines.append('    static const std::unordered_map<std::string, uint64_t> map = {')

    for sym_name in sorted(all_symbols.keys()):
        val = all_symbols[sym_name]
        lines.append(f'        {{"{sym_name}", {val}ULL}},')

    lines.append('    };')
    lines.append('    return map;')
    lines.append('}')
    lines.append('')
    lines.append('} // namespace')
    lines.append('')
    lines.append('std::string fieldEnumType(uint32_t msgid, const std::string &fieldName)')
    lines.append('{')
    lines.append('    const auto &map = fieldToEnumMap();')
    lines.append('    const auto it = map.find({msgid, fieldName});')
    lines.append('    return it != map.end() ? it->second : std::string();')
    lines.append('}')
    lines.append('')
    lines.append('const std::vector<EnumEntry> &fieldEnumEntries(uint32_t msgid, const std::string &fieldName)')
    lines.append('{')
    lines.append('    static const std::vector<EnumEntry> empty;')
    lines.append('    const std::string type = fieldEnumType(msgid, fieldName);')
    lines.append('    if (type.empty())')
    lines.append('    {')
    lines.append('        return empty;')
    lines.append('    }')
    lines.append('    const auto &defs = enumDefinitions();')
    lines.append('    const auto it = defs.find(type);')
    lines.append('    return it != defs.end() ? it->second : empty;')
    lines.append('}')
    lines.append('')
    lines.append('std::optional<uint64_t> lookupEnumValue(const std::string &symbol)')
    lines.append('{')
    lines.append('    if (symbol.empty())')
    lines.append('    {')
    lines.append('        return std::nullopt;')
    lines.append('    }')
    lines.append('    // Clean leading/trailing spaces')
    lines.append('    const auto first = symbol.find_first_not_of(" \\t\\r\\n");')
    lines.append('    if (first == std::string::npos)')
    lines.append('    {')
    lines.append('        return std::nullopt;')
    lines.append('    }')
    lines.append('    const auto last = symbol.find_last_not_of(" \\t\\r\\n");')
    lines.append('    std::string clean = symbol.substr(first, last - first + 1);')
    lines.append('    // Handle formats like "MAV_PARAM_TYPE_REAL32 (9)" or "MAV_PARAM_TYPE_REAL32"')
    lines.append('    const auto paren = clean.find(\'(\');')
    lines.append('    if (paren != std::string::npos)')
    lines.append('    {')
    lines.append('        clean = clean.substr(0, paren);')
    lines.append('        const auto endTrim = clean.find_last_not_of(" \\t\\r\\n");')
    lines.append('        if (endTrim != std::string::npos)')
    lines.append('        {')
    lines.append('            clean = clean.substr(0, endTrim + 1);')
    lines.append('        }')
    lines.append('    }')
    lines.append('    const auto &symbols = symbolMap();')
    lines.append('    const auto it = symbols.find(clean);')
    lines.append('    if (it != symbols.end())')
    lines.append('    {')
    lines.append('        return it->second;')
    lines.append('    }')
    lines.append('    return std::nullopt;')
    lines.append('}')
    lines.append('')
    lines.append('std::string lookupEnumName(uint32_t msgid, const std::string &fieldName, uint64_t value)')
    lines.append('{')
    lines.append('    const auto &entries = fieldEnumEntries(msgid, fieldName);')
    lines.append('    for (const auto &entry : entries)')
    lines.append('    {')
    lines.append('        if (entry.value == value)')
    lines.append('        {')
    lines.append('            return entry.name;')
    lines.append('        }')
    lines.append('    }')
    lines.append('    return {};')
    lines.append('}')
    lines.append('')
    lines.append('} // namespace Mav')
    lines.append('')

    with open(source_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))


def main():
    repo_root = Path(__file__).resolve().parent.parent
    def_dir = repo_root / "third_party" / "mavlink" / "message_definitions"
    if len(sys.argv) > 1:
        def_dir = Path(sys.argv[1])
    elif not def_dir.is_dir():
        def_dir = repo_root / "third_party" / "mavlink" / "message_definitions" / "v1.0"

    if not def_dir.is_dir():
        print(f"Error: MAVLink message definitions directory not found: {def_dir}", file=sys.stderr)
        sys.exit(1)

    print(f"Scanning XML definitions in: {def_dir}")
    enums, field_mappings, all_symbols = parse_xml_definitions(def_dir)
    print(f"Found {len(enums)} enums, {len(all_symbols)} symbols, {len(field_mappings)} mapped fields.")

    src_dir = repo_root / "src"
    header_path = src_dir / "mavlink_enums.h"
    source_path = src_dir / "mavlink_enums.cpp"

    print(f"Writing header to: {header_path}")
    generate_header(header_path)

    print(f"Writing source to: {source_path}")
    generate_source(source_path, enums, field_mappings, all_symbols)
    print("Done generating MAVLink enum tables.")


if __name__ == "__main__":
    main()
