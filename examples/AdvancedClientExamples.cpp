/**
 * @file AdvancedClientExamples.cpp
 * @brief Advanced HTTP client examples using utilities and builders
 */

#include "client/Client.h"
#include "uvent/Uvent.h"
#include "uvent/system/SystemContext.h"

#include <cctype>
#include <csignal>
#include <cstring>
#include <iostream>
#include <string>
#include <unordered_map>

using usub::uvent::task::Awaitable;

static void ignore_sigpipe() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, nullptr);
}

// ============================================================================
// Example 1: Using QueryBuilder for search requests
// ============================================================================
Awaitable<void> example_query_builder() {
    std::cout << "\n=== Example 1: Query Builder ===\n";

    // build query manually (no QueryBuilder available)
    std::string url = "http://httpbin.org/get?search=hello%20world&page=1&limit=50";
    std::cout << "Query URL: " << url << std::endl;

    usub::client::HttpClient client;
    auto resp = co_await client.get(url);
    std::cout << "Status: " << resp.getStatus() << std::endl;

    co_return;
}

// ============================================================================
// Example 2: Using ContentType enum
// ============================================================================
Awaitable<void> example_content_types() {
    std::cout << "\n=== Example 2: Content Types ===\n";

    usub::client::HttpClient client;
    // JSON request
    std::unordered_map<std::string, std::string> jh{{"Content-Type", "application/json"}};
    auto json_resp = co_await client.send("http://httpbin.org/post", "POST", jh, R"({"name": "John", "age": 30})");
    std::cout << "JSON request status: " << json_resp.getStatus() << std::endl;

    // Form-urlencoded request
    std::unordered_map<std::string, std::string> fh{{"Content-Type", "application/x-www-form-urlencoded"}};
    auto form_resp = co_await client.send("http://httpbin.org/post", "POST", fh, "username=john&password=secret");
    std::cout << "Form request status: " << form_resp.getStatus() << std::endl;

    co_return;
}

// ============================================================================
// Example 3: Using status code helpers
// ============================================================================
Awaitable<void> example_status_codes() {
    std::cout << "\n=== Example 3: Status Code Helpers ===\n";

    usub::client::HttpClient client;
    auto resp = co_await client.get("http://httpbin.org/status/200");

    std::cout << "Status code: " << resp.getStatus() << std::endl;
    if (resp.getStatus() == 200) std::cout << "Request was successful!\n";

    co_return;
}

// ============================================================================
// Example 4: Using string utilities
// ============================================================================
Awaitable<void> example_string_utils() {
    std::cout << "\n=== Example 4: String Utilities ===\n";

    // Minimal string examples (no utils provided)
    std::string message = "Hello, World!";
    std::string upper = message;
    for (auto &c: upper) c = std::toupper((unsigned char) c);
    std::string lower = message;
    for (auto &c: lower) c = std::tolower((unsigned char) c);
    std::cout << "Uppercase: " << upper << "\n";
    std::cout << "Lowercase: " << lower << "\n";

    co_return;
}

// ============================================================================
// Example 5: Building complex requests with multiple utilities
// ============================================================================
Awaitable<void> example_complex_request() {
    std::cout << "\n=== Example 5: Complex Request with Utilities ===\n";

    std::string full_url = "http://httpbin.org/get?api_key=secret123&format=json&limit=10";
    std::cout << "Full URL: " << full_url << std::endl;
    usub::client::HttpClient client2;
    std::unordered_map<std::string, std::string> hdrs{{"Accept", "application/json"}, {"User-Agent", "AdvancedExample/1.0"}};
    auto resp2 = co_await client2.send(full_url, "GET", hdrs);
    std::cout << "Response status: " << resp2.getStatus() << std::endl;
    if (resp2.getStatus() >= 200 && resp2.getStatus() < 300) {
        std::cout << "Response received successfully!" << std::endl;
        std::cout << "Response size: " << resp2.getBody().size() << " bytes" << std::endl;
    }

    co_return;
}

// ============================================================================
// Example 6: Processing response with utilities
// ============================================================================
Awaitable<void> example_process_response() {
    std::cout << "\n=== Example 6: Processing Response ===\n";

    usub::client::HttpClient client3;
    auto resp3 = co_await client3.send("http://httpbin.org/get?test=value", "GET", {{"Accept", "application/json"}});
    if (resp3.getStatus() != 200) {
        std::cout << "Error: status=" << resp3.getStatus() << std::endl;
        co_return;
    }

    std::cout << "Response headers:\n";
    if (resp3.getHeaders().contains("Content-Type")) {
        const auto &vals = resp3.getHeaders().at("Content-Type");
        if (!vals.empty()) std::cout << "  Content-Type: " << vals.front() << "\n";
    }

    std::string body_preview = resp3.getBody().size() > 500 ? resp3.getBody().substr(0, 500) + "..." : resp3.getBody();
    std::cout << "Response body (preview):\n"
              << body_preview << "\n";

    co_return;
}

// ============================================================================
// Example 7: Error handling with status codes
// ============================================================================
Awaitable<void> example_error_handling() {
    std::cout << "\n=== Example 7: Error Handling ===\n";

    usub::client::HttpClient client4;
    auto resp4 = co_await client4.get("http://httpbin.org/status/404");
    std::cout << "Response status: " << resp4.getStatus() << std::endl;
    if (resp4.getStatus() == 0) {
        std::cout << "Network error: " << resp4.getBody() << std::endl;
    } else if (resp4.getStatus() >= 200 && resp4.getStatus() < 300) {
        std::cout << "Success: " << resp4.getBody() << std::endl;
    } else if (resp4.getStatus() >= 400 && resp4.getStatus() < 500) {
        std::cout << "Client error (4xx): " << resp4.getStatus() << std::endl;
    } else if (resp4.getStatus() >= 500) {
        std::cout << "Server error (5xx): " << resp4.getStatus() << std::endl;
    }

    co_return;
}

// ============================================================================
// Main: Run all examples
// ============================================================================
int main() {
    ignore_sigpipe();

    auto uvent = std::make_shared<usub::Uvent>(1);

    usub::uvent::system::co_spawn(example_query_builder());
    usub::uvent::system::co_spawn(example_content_types());
    usub::uvent::system::co_spawn(example_status_codes());
    usub::uvent::system::co_spawn(example_string_utils());
    usub::uvent::system::co_spawn(example_complex_request());
    usub::uvent::system::co_spawn(example_process_response());
    usub::uvent::system::co_spawn(example_error_handling());

    uvent->run();

    std::cout << "\n=== All examples completed ===\n";
    return 0;
}
