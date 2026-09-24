#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Hephaestus::Automation::Mcp {

using HttpHeaders = std::vector<std::pair<std::string, std::string>>;

struct HttpRequest {
    std::string method;
    std::string path;
    HttpHeaders headers;
    std::string body;

    [[nodiscard]] std::string Header(std::string_view name) const;
    [[nodiscard]] bool HasHeader(std::string_view name) const;
};

struct HttpResponse {
    int status = 200;
    HttpHeaders headers;
    std::string body;

    [[nodiscard]] std::string Header(std::string_view name) const;
    [[nodiscard]] bool HasHeader(std::string_view name) const;
    void SetHeader(std::string name, std::string value);
};

} // namespace Hephaestus::Automation::Mcp
