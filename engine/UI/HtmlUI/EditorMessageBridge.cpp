#include "EditorMessageBridge.h"

#include <cctype>
#include <sstream>
#include <unordered_map>

namespace Hephaestus::HtmlUI {

EditorMessageBridge::EditorMessageBridge(int bridgeVersion)
    : version_(bridgeVersion) {}

void EditorMessageBridge::SetSendToJs(SendToJsFn fn) { sendToJs_ = std::move(fn); }

void EditorMessageBridge::RegisterHandler(const std::string& name, HandlerFn fn) {
  handlers_[name] = std::move(fn);
}

static size_t FindKey(const std::string& s, const char* key) {
  std::string needle = std::string("\"") + key + "\"";
  return s.find(needle);
}

static std::optional<size_t> FindFirstNonSpaceAfterColon(const std::string& s,
                                                        size_t keyPos) {
  size_t colon = s.find(':', keyPos);
  if (colon == std::string::npos)
    return std::nullopt;
  size_t i = colon + 1;
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
    ++i;
  if (i >= s.size())
    return std::nullopt;
  return i;
}

std::optional<int> EditorMessageBridge::ParseIntField(const std::string& s,
                                                      const char* key) {
  size_t k = FindKey(s, key);
  if (k == std::string::npos)
    return std::nullopt;
  size_t colon = s.find(':', k);
  if (colon == std::string::npos)
    return std::nullopt;
  size_t i = colon + 1;
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
    ++i;
  bool neg = false;
  if (i < s.size() && s[i] == '-') {
    neg = true;
    ++i;
  }
  int value = 0;
  bool any = false;
  while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
    any = true;
    value = value * 10 + (s[i] - '0');
    ++i;
  }
  if (!any)
    return std::nullopt;
  return neg ? -value : value;
}

std::optional<std::string>
EditorMessageBridge::ParseStringField(const std::string& s, const char* key) {
  size_t k = FindKey(s, key);
  if (k == std::string::npos)
    return std::nullopt;
  size_t colon = s.find(':', k);
  if (colon == std::string::npos)
    return std::nullopt;
  size_t q1 = s.find('"', colon + 1);
  if (q1 == std::string::npos)
    return std::nullopt;
  size_t q2 = s.find('"', q1 + 1);
  if (q2 == std::string::npos)
    return std::nullopt;
  return s.substr(q1 + 1, q2 - (q1 + 1));
}

std::optional<float> EditorMessageBridge::ParseFloatField(const std::string& s,
                                                          const char* key) {
  size_t k = FindKey(s, key);
  if (k == std::string::npos)
    return std::nullopt;
  size_t colon = s.find(':', k);
  if (colon == std::string::npos)
    return std::nullopt;
  size_t i = colon + 1;
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
    ++i;

  // Support optional quotes (some JS may send numbers as strings)
  bool quoted = false;
  if (i < s.size() && s[i] == '"') {
    quoted = true;
    ++i;
  }

  bool neg = false;
  if (i < s.size() && s[i] == '-') {
    neg = true;
    ++i;
  }

  bool any = false;
  double value = 0.0;
  while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
    any = true;
    value = value * 10.0 + (s[i] - '0');
    ++i;
  }

  if (i < s.size() && s[i] == '.') {
    ++i;
    double place = 0.1;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
      any = true;
      value += (s[i] - '0') * place;
      place *= 0.1;
      ++i;
    }
  }

  if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
    // Minimal exponent support.
    ++i;
    bool expNeg = false;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
      expNeg = (s[i] == '-');
      ++i;
    }
    int exp = 0;
    bool expAny = false;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
      expAny = true;
      exp = exp * 10 + (s[i] - '0');
      ++i;
    }
    if (expAny) {
      double scale = 1.0;
      for (int j = 0; j < exp; ++j) scale *= 10.0;
      value = expNeg ? (value / scale) : (value * scale);
    }
  }

  if (quoted) {
    // Consume closing quote if present.
    if (i < s.size() && s[i] == '"')
      ++i;
  }

  if (!any)
    return std::nullopt;
  if (neg)
    value = -value;
  return static_cast<float>(value);
}

std::optional<std::vector<int>>
EditorMessageBridge::ParseIntArrayField(const std::string& s, const char* key) {
  size_t k = FindKey(s, key);
  if (k == std::string::npos)
    return std::nullopt;

  auto startOpt = FindFirstNonSpaceAfterColon(s, k);
  if (!startOpt)
    return std::nullopt;
  size_t i = *startOpt;
  if (i >= s.size() || s[i] != '[')
    return std::nullopt;

  ++i; // skip '['
  std::vector<int> values;
  while (i < s.size()) {
    while (i < s.size() && (std::isspace(static_cast<unsigned char>(s[i])) || s[i] == ','))
      ++i;
    if (i >= s.size())
      break;
    if (s[i] == ']') {
      return values;
    }

    bool neg = false;
    if (s[i] == '-') {
      neg = true;
      ++i;
    }

    bool any = false;
    int value = 0;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
      any = true;
      value = value * 10 + (s[i] - '0');
      ++i;
    }

    if (any) {
      values.push_back(neg ? -value : value);
      continue;
    }

    // Skip invalid token until next comma or end bracket.
    while (i < s.size() && s[i] != ',' && s[i] != ']')
      ++i;
    if (i < s.size() && s[i] == ']')
      return values;
  }

  return std::nullopt;
}

std::optional<std::string>
EditorMessageBridge::ParseObjectField(const std::string& s, const char* key) {
  size_t k = FindKey(s, key);
  if (k == std::string::npos)
    return std::nullopt;

  auto startOpt = FindFirstNonSpaceAfterColon(s, k);
  if (!startOpt)
    return std::nullopt;
  size_t start = *startOpt;

  if (s[start] != '{')
    return std::nullopt;

  int depth = 0;
  bool inString = false;
  bool escape = false;
  for (size_t i = start; i < s.size(); ++i) {
    char c = s[i];
    if (inString) {
      if (escape) {
        escape = false;
        continue;
      }
      if (c == '\\') {
        escape = true;
        continue;
      }
      if (c == '"') {
        inString = false;
      }
      continue;
    }

    if (c == '"') {
      inString = true;
      continue;
    }

    if (c == '{') {
      ++depth;
      continue;
    }
    if (c == '}') {
      --depth;
      if (depth == 0) {
        // include closing brace
        return s.substr(start, (i - start) + 1);
      }
    }
  }

  return std::nullopt;
}

std::string EditorMessageBridge::JsStringEscape(const std::string& s) {
  std::ostringstream out;
  for (char c : s) {
    switch (c) {
    case '\\':
      out << "\\\\";
      break;
    case '\"':
      out << "\\\"";
      break;
    case '\n':
      out << "\\n";
      break;
    case '\r':
      out << "\\r";
      break;
    case '\t':
      out << "\\t";
      break;
    default:
      out << c;
      break;
    }
  }
  return out.str();
}

std::optional<std::string>
EditorMessageBridge::HandleFromJs(const std::string& message) {
  auto v = ParseIntField(message, "v");
  if (!v || *v != version_) {
    return std::string("{\"ok\":false,\"error\":\"bridge_version_mismatch\"}");
  }

  auto name = ParseStringField(message, "name");
  if (!name) {
    return std::string("{\"ok\":false,\"error\":\"missing_name\"}");
  }

  // Extract payload object if present.
  std::string payload = "{}";
  if (auto p = ParseObjectField(message, "payload")) {
    payload = *p;
  }

  // Legacy minimal command (initial shell).
  if (*name == "log") {
    (void)payload;
    return std::string("{\"ok\":true}");
  }

  auto it = handlers_.find(*name);
  if (it == handlers_.end()) {
    return std::string("{\"ok\":false,\"error\":\"unknown_command\"}");
  }

  try {
    return it->second(payload);
  } catch (...) {
    return std::string("{\"ok\":false,\"error\":\"handler_exception\"}");
  }
}

void EditorMessageBridge::EmitEvent(const std::string& name,
                                   const std::string& payloadJson) {
  if (!sendToJs_)
    return;

  // Call: window.hepReceive({v:1,name:"...",payload:...});
  std::ostringstream js;
  js << "window.hepReceive && window.hepReceive({";
  js << "v:" << version_ << ",";
  js << "name:\"" << JsStringEscape(name) << "\",";
  js << "payload:" << payloadJson;
  js << "});";

  sendToJs_(js.str());
}

} // namespace Hephaestus::HtmlUI
