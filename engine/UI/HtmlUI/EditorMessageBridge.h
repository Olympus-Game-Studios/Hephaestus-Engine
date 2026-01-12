#pragma once

#include <functional>
#include <unordered_map>
#include <optional>
#include <string>

namespace Hephaestus::HtmlUI {

// Minimal, versioned message bridge between JS and C++.
//
// JS -> C++ sends a JSON-ish string envelope (we parse minimally):
//   {"v":1,"id":"req-1","name":"log","arg":"hello"}
//
// C++ -> JS emits events by calling a known JS function with an envelope:
//   window.hepReceive({v:1, name:"selection_changed", ...})
class EditorMessageBridge {
public:
  using SendToJsFn = std::function<void(const std::string& jsCall)>;
  using HandlerFn = std::function<std::string(const std::string& payloadJson)>;

  explicit EditorMessageBridge(int bridgeVersion);

  void SetSendToJs(SendToJsFn fn);

  // Register a named command handler. The handler receives the raw JSON payload
  // object substring (eg, {"id":1}) and returns a JSON response string.
  void RegisterHandler(const std::string& name, HandlerFn fn);

  // Handle a message received from JS (string is the raw envelope).
  // Returns an optional JSON string response.
  std::optional<std::string> HandleFromJs(const std::string& message);

  // Emit an event to JS. payloadJson should be a JSON object string (eg, {"x":1}).
  void EmitEvent(const std::string& name, const std::string& payloadJson);

  // Minimal parsing helpers for handler implementations.
  static std::optional<int> ParseIntField(const std::string& s, const char* key);
  static std::optional<float> ParseFloatField(const std::string& s, const char* key);
  static std::optional<std::string> ParseStringField(const std::string& s,
                                                     const char* key);
  static std::optional<std::string> ParseObjectField(const std::string& s,
                                                     const char* key);

private:
  int version_ = 1;
  SendToJsFn sendToJs_;
  std::unordered_map<std::string, HandlerFn> handlers_;
  static std::string JsStringEscape(const std::string& s);
};

} // namespace Hephaestus::HtmlUI
