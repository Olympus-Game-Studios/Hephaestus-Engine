#include "Core/IniFile.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

bool Check(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
    }
    return condition;
}

std::string Value(const Hephaestus::IniDocument& document, const std::string& section, const std::string& key)
{
    const auto* found = document.Find(section);
    if (!found) {
        return "<no section>";
    }
    for (const auto& entry : found->entries) {
        if (entry.key == key) {
            return entry.value;
        }
    }
    return "<no key>";
}

bool TestParse()
{
    std::vector<std::string> warnings;
    const auto document = Hephaestus::ParseIni(
        "\xEF\xBB\xBF; header comment\n"
        "loose = before any section\n"
        "[Audio]\n"
        "# hash comment\n"
        "  MusicVolume =  0.3  \r\n"
        "Empty =\n"
        "not a setting\n"
        "[Controls]\n"
        "Jump = key:J, pad:SOUTH\n"
        "Jump = key:K\n"
        "[Broken\n"
        "Orphan = 1\n"
        "[Audio]\n"
        "MasterVolume = 0.8\n",
        warnings);
    bool ok = Check(Value(document, "", "loose") == "before any section", "keys before a section are kept");
    ok &= Check(Value(document, "Audio", "MusicVolume") == "0.3", "values are trimmed, CRLF included");
    ok &= Check(Value(document, "Audio", "Empty").empty(), "an empty value is kept as empty");
    ok &= Check(Value(document, "Controls", "Jump") == "key:K", "a repeated key keeps its last value");
    ok &= Check(Value(document, "Audio", "MasterVolume") == "0.8", "a section written twice is merged");
    ok &= Check(Value(document, "", "Orphan") == "1",
                "keys after a malformed section header land outside any named section");
    ok &= Check(warnings.size() == 2 && warnings[0].find("line 7") != std::string::npos &&
                    warnings[1].find("line 11") != std::string::npos,
                "a line that is not a setting and a malformed section header are reported by line");
    return ok;
}

bool TestRoundTrip()
{
    Hephaestus::IniDocument document;
    document.header = "Game settings.\nSecond line.";
    document.sections.push_back({"Video", "Picture", {{"FieldOfView", "95", "Degrees"}, {"UIScale", "1", ""}}});
    document.sections.push_back({"Controls", "", {{"Jump", "key:SPACE, pad:SOUTH", ""}}});
    const std::string text = Hephaestus::WriteIni(document);
    bool ok = Check(text.rfind("; Game settings.\n; Second line.\n", 0) == 0, "the header is written as comments");
    ok &= Check(text.find("; Degrees\nFieldOfView = 95\n") != std::string::npos, "comments sit above their key");
    std::vector<std::string> warnings;
    const auto parsed = Hephaestus::ParseIni(text, warnings);
    ok &= Check(warnings.empty(), "written files read back without warnings");
    ok &= Check(Value(parsed, "Video", "FieldOfView") == "95" && Value(parsed, "Video", "UIScale") == "1" &&
                    Value(parsed, "Controls", "Jump") == "key:SPACE, pad:SOUTH",
                "values round-trip");
    return ok;
}

} // namespace

int main()
{
    bool ok = TestParse();
    ok &= TestRoundTrip();
    if (ok) {
        std::cout << "IniFileTests passed\n";
        return 0;
    }
    return 1;
}
