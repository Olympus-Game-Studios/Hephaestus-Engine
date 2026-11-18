#include "IniFile.h"

#include <sstream>

namespace Hephaestus {
namespace {

std::string Trim(const std::string& text)
{
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

void WriteComment(std::ostringstream& out, const std::string& comment)
{
    if (comment.empty()) {
        return;
    }
    std::istringstream lines(comment);
    std::string line;
    while (std::getline(lines, line)) {
        out << "; " << line << '\n';
    }
}

std::string OneLine(const std::string& text)
{
    std::string out = text;
    for (char& c : out) {
        if (c == '\r' || c == '\n') {
            c = ' ';
        }
    }
    return Trim(out);
}

} // namespace

const IniSection* IniDocument::Find(const std::string& name) const
{
    for (const auto& section : sections) {
        if (section.name == name) {
            return &section;
        }
    }
    return nullptr;
}

IniDocument ParseIni(const std::string& text, std::vector<std::string>& warnings)
{
    IniDocument document;
    IniSection* current = nullptr;
    std::istringstream lines(text);
    std::string raw;
    int number = 0;
    while (std::getline(lines, raw)) {
        ++number;
        if (number == 1 && raw.rfind("\xEF\xBB\xBF", 0) == 0) {
            raw.erase(0, 3);
        }
        const std::string line = Trim(raw);
        if (line.empty() || line[0] == ';' || line[0] == '#') {
            continue;
        }
        if (line.front() == '[') {
            if (line.back() != ']' || line.size() < 3) {
                warnings.push_back("line " + std::to_string(number) + ": a section name must be written as [Name]");
                current = nullptr;
                continue;
            }
            const std::string name = Trim(line.substr(1, line.size() - 2));
            current = nullptr;
            for (auto& section : document.sections) {
                if (section.name == name) {
                    current = &section;
                }
            }
            if (!current) {
                document.sections.push_back({name, {}, {}});
                current = &document.sections.back();
            }
            continue;
        }
        const auto equals = line.find('=');
        if (equals == std::string::npos || Trim(line.substr(0, equals)).empty()) {
            warnings.push_back("line " + std::to_string(number) + ": expected key = value");
            continue;
        }
        if (!current) {
            if (document.sections.empty() || !document.sections.front().name.empty()) {
                document.sections.insert(document.sections.begin(), {"", {}, {}});
            }
            current = &document.sections.front();
        }
        const std::string key = Trim(line.substr(0, equals));
        const std::string value = Trim(line.substr(equals + 1));
        bool replaced = false;
        for (auto& entry : current->entries) {
            if (entry.key == key) {
                entry.value = value;
                replaced = true;
            }
        }
        if (!replaced) {
            current->entries.push_back({key, value, {}});
        }
    }
    return document;
}

std::string WriteIni(const IniDocument& document)
{
    std::ostringstream out;
    WriteComment(out, document.header);
    for (const auto& section : document.sections) {
        if (out.tellp() > 0) {
            out << '\n';
        }
        WriteComment(out, section.comment);
        if (!section.name.empty()) {
            out << '[' << OneLine(section.name) << "]\n";
        }
        for (const auto& entry : section.entries) {
            WriteComment(out, entry.comment);
            out << OneLine(entry.key) << " = " << OneLine(entry.value) << '\n';
        }
    }
    return out.str();
}

} // namespace Hephaestus
