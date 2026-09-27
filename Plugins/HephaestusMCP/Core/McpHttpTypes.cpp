#include "McpHttpTypes.h"

#include <algorithm>
#include <cctype>

namespace Hephaestus::Automation::Mcp {
namespace {

bool EqualsIgnoreCase(std::string_view left, std::string_view right)
{
    return left.size() == right.size() &&
           std::equal(left.begin(), left.end(), right.begin(),
                      [](unsigned char a, unsigned char b) {
                          return std::tolower(a) == std::tolower(b);
                      });
}

} // namespace

std::string HttpRequest::Header(std::string_view name) const
{
    for (const auto& [headerName, value] : headers) {
        if (EqualsIgnoreCase(headerName, name)) return value;
    }
    return {};
}

bool HttpRequest::HasHeader(std::string_view name) const
{
    for (const auto& [headerName, value] : headers) {
        (void)value;
        if (EqualsIgnoreCase(headerName, name)) return true;
    }
    return false;
}

std::string HttpResponse::Header(std::string_view name) const
{
    for (const auto& [headerName, value] : headers) {
        if (EqualsIgnoreCase(headerName, name)) return value;
    }
    return {};
}

bool HttpResponse::HasHeader(std::string_view name) const
{
    for (const auto& [headerName, value] : headers) {
        (void)value;
        if (EqualsIgnoreCase(headerName, name)) return true;
    }
    return false;
}

void HttpResponse::SetHeader(std::string name, std::string value)
{
    for (auto& [headerName, headerValue] : headers) {
        if (EqualsIgnoreCase(headerName, name)) {
            headerValue = std::move(value);
            return;
        }
    }
    headers.emplace_back(std::move(name), std::move(value));
}

} // namespace Hephaestus::Automation::Mcp
