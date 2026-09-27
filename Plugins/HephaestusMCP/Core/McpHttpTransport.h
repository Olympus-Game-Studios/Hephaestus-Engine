#pragma once

#include "McpHttpTypes.h"

#include <functional>
#include <memory>

namespace Hephaestus::Automation::Mcp {

class McpHttpTransport {
public:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;

    McpHttpTransport();
    ~McpHttpTransport();

    McpHttpTransport(const McpHttpTransport&) = delete;
    McpHttpTransport& operator=(const McpHttpTransport&) = delete;

    bool Start(int port, Handler handler);
    void Stop();
    [[nodiscard]] bool IsRunning() const noexcept;
    [[nodiscard]] int Port() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> m_Impl;
};

} // namespace Hephaestus::Automation::Mcp
