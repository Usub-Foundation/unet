/**
 * @file example_new_client.cpp
 * @brief Demonstration of the new modern HTTP client API
 *
 * This example shows the improvements over the old SimpleClient.cpp:
 * - Fluent builder API (like cpr/requests)
 * - Support for both HTTP and HTTPS with same interface
 * - Cleaner coroutine-based API
 * - Better error handling
 */

#include "client/Client.h"
#include "uvent/Uvent.h"
#include "uvent/system/SystemContext.h"

#include <csignal>
#include <iostream>
#include <string>

using usub::uvent::task::Awaitable;

static void ignore_sigpipe() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, nullptr);
}

// ============================================================================
// Example 1: Simple GET request
// ============================================================================
Awaitable<void> example_simple_get() {
    std::cout << "\n=== Example 1: Simple GET Request ===\n";

    usub::client::HttpClient client;
    auto resp = co_await client.get("http://example.com");

    std::cout << "Status: " << resp.getStatus() << std::endl;
    std::cout << "OK: " << ((resp.getStatus() >= 200 && resp.getStatus() < 300) ? "yes" : "no") << std::endl;
    std::cout << "Body length: " << resp.getBody().size() << std::endl;
    std::cout << "Body preview: " << resp.getBody().substr(0, 200) << "...\n";

    co_return;
}

// ============================================================================
// Example 2: GET with builder pattern
// ============================================================================
Awaitable<void> example_get_with_headers() {
    std::cout << "\n=== Example 2: GET with Custom Headers ===\n";

    usub::client::HttpClient client;
    std::unordered_map<std::string, std::string> headers{
            {"User-Agent", "MyApp/1.0"},
            {"Accept-Language", "en-US"}};

    auto resp = co_await client.send("http://example.com", "GET", headers, "");

    std::cout << "Status: " << resp.getStatus() << std::endl;
    if (resp.getHeaders().contains("Content-Type")) {
        const auto &vals = resp.getHeaders().at("Content-Type");
        if (!vals.empty()) std::cout << "Content-Type: " << vals.front() << std::endl;
    }
    std::cout << "Response size: " << resp.getBody().size() << std::endl;

    co_return;
}

// ============================================================================
// Example 3: POST with body
// ============================================================================
Awaitable<void> example_post_request() {
    std::cout << "\n=== Example 3: POST Request with Body ===\n";
    usub::client::HttpClient client;
    std::unordered_map<std::string, std::string> headers{{"Content-Type", "application/json"}};
    auto resp = co_await client.send("http://httpbin.org/post", "POST", headers,
                                     R"({"message": "Hello, World!", "timestamp": 1234567890})");

    std::cout << "Status: " << resp.getStatus() << std::endl;
    std::cout << "Response body:\n"
              << resp.getBody() << std::endl;

    co_return;
}

// ============================================================================
// Example 4: Multiple requests sequentially
// ============================================================================
Awaitable<void> example_multiple_requests() {
    std::cout << "\n=== Example 4: Multiple Sequential Requests ===\n";
    usub::client::HttpClient client;

    std::vector<std::string> urls = {
            "http://example.com",
            "http://example.org",
            "http://example.net"};

    for (const auto &url: urls) {
        auto resp = co_await client.get(url);
        std::cout << "URL: " << url << " | Status: " << resp.getStatus() << " | Size: " << resp.getBody().size() << "\n";
    }

    co_return;
}

// ============================================================================
// Example 5: Different HTTP methods
// ============================================================================
Awaitable<void> example_http_methods() {
    std::cout << "\n=== Example 5: Different HTTP Methods ===\n";
    usub::client::HttpClient client;

    // PUT request
    auto put_resp = co_await client.send("http://httpbin.org/put", "PUT", {}, "Updated content");
    std::cout << "PUT Status: " << put_resp.getStatus() << std::endl;

    // DELETE request
    auto del_resp = co_await client.send("http://httpbin.org/delete", "DELETE");
    std::cout << "DELETE Status: " << del_resp.getStatus() << std::endl;

    // PATCH request
    auto patch_resp = co_await client.send("http://httpbin.org/patch", "PATCH", {}, "Patched content");
    std::cout << "PATCH Status: " << patch_resp.getStatus() << std::endl;

    co_return;
}

// ============================================================================
// Example 6: HTTPS request (when USE_OPEN_SSL is enabled)
// ============================================================================
#ifdef USE_OPEN_SSL
Awaitable<void> example_https_request() {
    std::cout << "\n=== Example 6: HTTPS Request ===\n";
    usub::client::HttpClient client;
    std::unordered_map<std::string, std::string> headers{{"User-Agent", "MyApp/1.0"}};
    auto resp = co_await client.send("https://example.com", "GET", headers, "");

    std::cout << "Status: " << resp.getStatus() << std::endl;
    std::cout << "HTTPS Success: " << ((resp.getStatus() >= 200 && resp.getStatus() < 300) ? "yes" : "no") << std::endl;
    std::cout << "Response size: " << resp.getBody().size() << std::endl;

    co_return;
}
#endif

// ============================================================================
// Example 7: Chaining headers and building complex requests
// ============================================================================
Awaitable<void> example_complex_request() {
    std::cout << "\n=== Example 7: Complex Request with Multiple Headers ===\n";
    usub::client::HttpClient client;
    std::unordered_map<std::string, std::string> headers{
            {"Content-Type", "application/json"},
            {"Authorization", "Bearer token123"},
            {"X-Custom-Header", "CustomValue"},
            {"Accept", "application/json"}};

    auto resp = co_await client.send("http://httpbin.org/post", "POST", headers,
                                     R"({
            "name": "John Doe",
            "age": 30,
            "email": "john@example.com"
        })");

    std::cout << "Status: " << resp.getStatus() << std::endl;
    if (resp.getHeaders().contains("Server")) {
        const auto &vals = resp.getHeaders().at("Server");
        if (!vals.empty()) std::cout << "Server: " << vals.front() << std::endl;
    }
    std::cout << "Response preview: " << resp.getBody().substr(0, 300) << "...\n";

    co_return;
}

// ============================================================================
// Main: Run examples
// ============================================================================
int main() {
    ignore_sigpipe();

    // Create event loop with single thread
    auto uvent = std::make_shared<usub::Uvent>(1);

    // Spawn all examples as coroutines
    usub::uvent::system::co_spawn(example_simple_get());
    usub::uvent::system::co_spawn(example_get_with_headers());
    usub::uvent::system::co_spawn(example_post_request());
    usub::uvent::system::co_spawn(example_multiple_requests());
    usub::uvent::system::co_spawn(example_http_methods());

#ifdef USE_OPEN_SSL
    usub::uvent::system::co_spawn(example_https_request());
#endif

    usub::uvent::system::co_spawn(example_complex_request());

    // Run the event loop
    uvent->run();

    std::cout << "\n=== All examples completed ===\n";
    return 0;
}
