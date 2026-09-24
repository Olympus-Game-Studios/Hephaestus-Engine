#include <httplib/httplib.h>

#include <iostream>
#include <string>
#include <thread>

namespace {

bool Check(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
    }
    return condition;
}

} // namespace

int main()
{
    bool ok = true;
    httplib::Server server;

    server.Post("/json", [](const httplib::Request& request, httplib::Response& response) {
        if (request.get_header_value("Origin") != "http://127.0.0.1" ||
            request.get_header_value("X-Smoke-Test") != "visible") {
            response.status = httplib::StatusCode::Forbidden_403;
            return;
        }
        response.set_content("{\"ok\":true}", "application/json");
    });
    server.Get("/events", [](const httplib::Request&, httplib::Response& response) {
        response.set_header("X-Accel-Buffering", "no");
        response.set_chunked_content_provider(
            "text/event-stream",
            [](std::size_t, httplib::DataSink& sink) {
                const std::string event = "data: {\"ready\":true}\r\n\r\n";
                sink.write(event.data(), event.size());
                sink.done();
                return true;
            });
    });

    const int port = server.bind_to_any_port("127.0.0.1");
    ok &= Check(port > 0, "Server binds an ephemeral loopback port");
    if (port <= 0) {
        return 1;
    }

    std::thread serverThread([&server]() { server.listen_after_bind(); });

    httplib::Client client("127.0.0.1", port);
    httplib::Headers headers{
        {"Origin", "http://127.0.0.1"},
        {"X-Smoke-Test", "visible"},
    };
    const auto json = client.Post("/json", headers, "{}", "application/json");
    ok &= Check(json && json->status == httplib::StatusCode::OK_200,
                "JSON request succeeds and headers are visible");
    ok &= Check(json && json->get_header_value("Content-Type").find("application/json") == 0,
                "JSON response content type is preserved");
    ok &= Check(json && json->body == "{\"ok\":true}", "JSON response body round-trips");

    const auto events = client.Get("/events");
    ok &= Check(events && events->status == httplib::StatusCode::OK_200,
                "SSE request succeeds");
    ok &= Check(events && events->get_header_value("Content-Type").find("text/event-stream") == 0,
                "SSE response content type is preserved");
    ok &= Check(events && events->get_header_value("X-Accel-Buffering") == "no",
                "SSE buffering header is visible");
    ok &= Check(events && events->body == "data: {\"ready\":true}\r\n\r\n",
                "Chunked SSE body terminates cleanly");

    server.stop();
    serverThread.join();

    if (ok) {
        std::cout << "HttpTransportSmokeTests passed\n";
        return 0;
    }
    return 1;
}
