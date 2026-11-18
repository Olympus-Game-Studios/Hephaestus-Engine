#pragma once

#include <string>
#include <vector>

namespace Hephaestus {

struct IniEntry {
    std::string key;
    std::string value;
    std::string comment;
};

struct IniSection {
    std::string name;
    std::string comment;
    std::vector<IniEntry> entries;
};

struct IniDocument {
    std::string header;
    std::vector<IniSection> sections;

    const IniSection* Find(const std::string& name) const;
};

// Lines that are not a [section], key = value, blank or a ; / # comment are skipped and described in warnings.
// Keys before the first section belong to a section with an empty name.
IniDocument ParseIni(const std::string& text, std::vector<std::string>& warnings);

// Comments are written as "; " lines above what they describe.
std::string WriteIni(const IniDocument& document);

} // namespace Hephaestus
