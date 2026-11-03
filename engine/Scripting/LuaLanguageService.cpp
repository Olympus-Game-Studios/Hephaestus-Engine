#include "LuaLanguageService.h"
#include "LuaScriptEngine.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace LuaLanguageService {

namespace {

const std::vector<std::string>& Keywords() {
    static const std::vector<std::string> keywords = {
        "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if", "in",
        "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while",
    };
    return keywords;
}

const std::unordered_set<std::string>& StandardLibrary() {
    static const std::unordered_set<std::string> names = {
        "assert", "collectgarbage", "coroutine", "debug", "dofile", "error", "getmetatable", "io", "ipairs",
        "load", "loadfile", "math", "next", "os", "package", "pairs", "pcall", "print", "rawequal", "rawget",
        "rawlen", "rawset", "require", "select", "setmetatable", "string", "table", "tonumber", "tostring",
        "type", "utf8", "xpcall", "_G", "_VERSION", "warn",
    };
    return names;
}

struct ApiIndex {
    std::vector<LuaScriptEngine::ApiEntry> entries;
    std::unordered_map<std::string, std::string> kindByName;
};

const ApiIndex& Api() {
    static const ApiIndex index = [] {
        ApiIndex built;
        built.entries = LuaScriptEngine::DescribeApi();
        std::sort(built.entries.begin(), built.entries.end(),
                  [](const auto& a, const auto& b) { return a.name < b.name; });
        for (const auto& entry : built.entries) {
            built.kindByName[entry.name] = entry.kind;
        }
        return built;
    }();
    return index;
}

bool IsWordChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

std::string LineAt(const std::string& source, uint32_t line) {
    std::istringstream stream(source);
    std::string text;
    for (uint32_t i = 1; std::getline(stream, text); ++i) {
        if (i == line) {
            if (!text.empty() && text.back() == '\r') {
                text.pop_back();
            }
            return text;
        }
    }
    return {};
}

std::string StripComment(const std::string& text) {
    bool inString = false;
    char quote = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (inString) {
            if (c == '\\') ++i;
            else if (c == quote) inString = false;
        } else if (c == '"' || c == '\'') {
            inString = true;
            quote = c;
        } else if (c == '-' && i + 1 < text.size() && text[i + 1] == '-') {
            return text.substr(0, i);
        }
    }
    return text;
}

// The word ending at the cursor and, when it follows "." or ":", the name before that.
struct Context {
    std::string prefix;
    std::string qualifier;
    char separator = 0;
};

Context ContextAt(const std::string& text, std::size_t cursor) {
    Context context;
    cursor = std::min(cursor, text.size());
    std::size_t start = cursor;
    while (start > 0 && IsWordChar(text[start - 1])) --start;
    context.prefix = text.substr(start, cursor - start);
    if (start > 0 && (text[start - 1] == '.' || text[start - 1] == ':')) {
        context.separator = text[start - 1];
        std::size_t end = start - 1;
        std::size_t begin = end;
        while (begin > 0 && (IsWordChar(text[begin - 1]) || text[begin - 1] == '.')) --begin;
        context.qualifier = text.substr(begin, end - begin);
    }
    return context;
}

uint32_t KindFor(const std::string& luaType) {
    if (luaType == "function") return HP_COMPLETION_FUNCTION;
    if (luaType == "table") return HP_COMPLETION_TYPE;
    return HP_COMPLETION_VARIABLE;
}

struct Declaration {
    std::string name;
    uint32_t line = 0;
    std::string text;
    bool function = false;
};

std::vector<Declaration> Declarations(const std::string& source) {
    static const std::regex functionPattern(R"(function\s+([A-Za-z_][\w.:]*)\s*\(([^)]*)\))");
    static const std::regex localPattern(R"(local\s+([A-Za-z_][\w\s,]*?)\s*(=|$|\bin\b))");
    static const std::regex forPattern(R"(for\s+([A-Za-z_][\w\s,]*?)\s*(=|\bin\b))");
    static const std::regex namePattern(R"([A-Za-z_]\w*)");
    std::vector<Declaration> declarations;
    std::istringstream stream(source);
    std::string raw;
    for (uint32_t number = 1; std::getline(stream, raw); ++number) {
        const std::string text = StripComment(raw);
        std::string trimmed = text;
        trimmed.erase(0, trimmed.find_first_not_of(" \t"));
        while (!trimmed.empty() && (trimmed.back() == '\r' || trimmed.back() == ' ')) trimmed.pop_back();
        for (std::sregex_iterator it(text.begin(), text.end(), functionPattern), end; it != end; ++it) {
            const std::string full = (*it)[1].str();
            const std::size_t split = full.find_last_of(".:");
            declarations.push_back({ split == std::string::npos ? full : full.substr(split + 1), number, trimmed, true });
            const std::string params = (*it)[2].str();
            for (std::sregex_iterator p(params.begin(), params.end(), namePattern), pend; p != pend; ++p) {
                declarations.push_back({ p->str(), number, trimmed, false });
            }
        }
        for (const std::regex* pattern : { &localPattern, &forPattern }) {
            for (std::sregex_iterator it(text.begin(), text.end(), *pattern), end; it != end; ++it) {
                const std::string names = (*it)[1].str();
                for (std::sregex_iterator n(names.begin(), names.end(), namePattern), nend; n != nend; ++n) {
                    if (n->str() != "function") {
                        declarations.push_back({ n->str(), number, trimmed, false });
                    }
                }
            }
        }
    }
    return declarations;
}

} // namespace

std::vector<Completion> Complete(const std::string& source, uint32_t line, uint32_t column) {
    std::vector<Completion> completions;
    std::set<std::string> seen;
    auto add = [&](const std::string& label, uint32_t kind, const std::string& detail) {
        if (!label.empty() && seen.insert(label).second) {
            completions.push_back({ label, kind, detail });
        }
    };
    const Context context = ContextAt(LineAt(source, line), column > 0 ? column - 1 : 0);

    if (context.separator) {
        const std::string scope = context.qualifier + ".";
        for (const auto& entry : Api().entries) {
            if (entry.name.rfind(scope, 0) == 0 && entry.name.find('.', scope.size()) == std::string::npos) {
                add(entry.name.substr(scope.size()), KindFor(entry.kind), entry.kind);
            }
        }
        // Members the file itself uses or defines on that name: "Door.speed", "function Door:open()".
        const std::regex member("\\b" + std::regex_replace(context.qualifier, std::regex(R"(\.)"), R"(\.)") +
                                R"([.:]([A-Za-z_]\w*)(\s*\()?)");
        for (std::sregex_iterator it(source.begin(), source.end(), member), end; it != end; ++it) {
            add((*it)[1].str(), (*it)[2].matched ? HP_COMPLETION_FUNCTION : HP_COMPLETION_FIELD, "in this file");
        }
        return completions;
    }

    for (const std::string& keyword : Keywords()) {
        add(keyword, HP_COMPLETION_KEYWORD, "keyword");
    }
    for (const Declaration& declaration : Declarations(source)) {
        add(declaration.name, declaration.function ? HP_COMPLETION_FUNCTION : HP_COMPLETION_VARIABLE,
            "line " + std::to_string(declaration.line));
    }
    for (const auto& entry : Api().entries) {
        if (entry.name.find('.') == std::string::npos) {
            add(entry.name, KindFor(entry.kind),
                StandardLibrary().count(entry.name) ? "Lua " + entry.kind : "engine " + entry.kind);
        }
    }
    return completions;
}

std::string Hover(const std::string& source, uint32_t line, uint32_t column) {
    const std::string text = LineAt(source, line);
    std::size_t end = std::min<std::size_t>(column > 0 ? column - 1 : 0, text.size());
    while (end < text.size() && IsWordChar(text[end])) ++end;
    const Context context = ContextAt(text, end);
    if (context.prefix.empty()) {
        return {};
    }
    const std::string& word = context.prefix;

    if (std::find(Keywords().begin(), Keywords().end(), word) != Keywords().end()) {
        return "Lua keyword '" + word + "'";
    }
    const std::string qualified = context.separator ? context.qualifier + "." + word : word;
    const auto api = Api().kindByName.find(qualified);
    if (api != Api().kindByName.end()) {
        const std::string root = qualified.substr(0, qualified.find('.'));
        const std::string origin = StandardLibrary().count(root) ? "Lua standard library" : "Hephaestus engine API";
        std::string result = api->second + " " + qualified + "\n" + origin;
        if (api->second == "table") {
            std::size_t members = 0;
            for (const auto& entry : Api().entries) {
                if (entry.name.rfind(qualified + ".", 0) == 0) ++members;
            }
            result += " (" + std::to_string(members) + " members)";
        }
        return result;
    }

    // The nearest declaration at or above the cursor, else the first one below it.
    std::optional<Declaration> best;
    for (const Declaration& declaration : Declarations(source)) {
        if (declaration.name != word) continue;
        const bool above = declaration.line <= line;
        if (!best || (above && (best->line > line || declaration.line > best->line))) {
            best = declaration;
        }
    }
    if (best) {
        return (best->function ? "" : "local " + word + "\n") + best->text + "\n(line " + std::to_string(best->line) + ")";
    }
    return {};
}

} // namespace LuaLanguageService
