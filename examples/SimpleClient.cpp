// A minimal HTTP client example using include/client/Client.h
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

// Simple coroutine that connects, sends a GET request, and prints the response
Awaitable<void> run_client(const std::string &url, const std::string &path) {
    usub::client::HttpClient client;
    std::cout << "\n===== Client =====\n";
    // use the convenience `get` which accepts a full URL
    auto full = "https://goog.com" + (path.empty() ? std::string("/") : path);
    auto resp = co_await client.get(full);
    if (!resp) {
        std::cerr << "Request failed or no response: " << resp.error().message << std::endl;
        co_return;
    }

    std::cout << "\n===== Response Body =====\n";
    std::cout << resp.value().getBodyHex() << std::endl;
    std::cout << "===== End Response =====\n";

    co_return;
}


#if defined(UNET_USE_UJSON) && UNET_USE_UJSON
static void print_ujson_err(const ujson::Error &e, std::string_view src) {
    std::cerr
            << "ujson ERR: " << (e.msg ? e.msg : "<null>") << "\n"
            << "pos=" << e.pos << " line=" << e.line << " col=" << e.col << "\n"
            << "path=" << e.path << "\n"
            << e.near(src) << "\n";
}

struct Post1 {
    int userId, id;
    std::string title, body;
};

Awaitable<void> run_client_posts_ujson() {
    usub::client::HttpClient client; // unlimited body length

    std::cout << "\n===== Client =====\n";
    auto full = "https://jsonplaceholder.typicode.com/posts/1";

    auto resp = co_await client.get(full);
    if (!resp) {
        std::cerr << "Request failed: " << resp.error().message << "\n";
        co_return;
    }

    std::cout << "body hex: " << resp->getBodyHex() << "\n";
    auto parsed = resp->getAsJson<Post1>();
    if (!parsed) {
        print_ujson_err(parsed.error(), resp->getBody());
        co_return;
    }

    std::cout << "\n===== Parsed =====\n";
    std::cout << "id=" << parsed->id
              << " userId=" << parsed->userId
              << " title='" << parsed->title << "'\n";

    std::cout << "\n===== Dump =====\n";
    std::cout << ujson::dump(*parsed) << "\n";
    std::cout << "===== End =====\n";

    co_return;
}

Awaitable<void> run_client_posts_ujson_strict_fail() {
    usub::client::HttpClient client{std::chrono::milliseconds{0}, std::nullopt}; // unlimited body length

    std::cout << "\n===== Client (strict fail) =====\n";
    auto full = "https://jsonplaceholder.typicode.com/posts/1";

    auto resp = co_await client.get(full);
    if (!resp) {
        std::cerr << "Request failed: " << resp.error().message << "\n";
        co_return;
    }

    std::string body = resp->getBody();
    if (!body.empty() && body.back() == '\n') body.pop_back();

    auto last_brace = body.find_last_of('}');
    if (last_brace == std::string::npos) {
        std::cerr << "Body is not JSON object\n";
        co_return;
    }
    body.insert(last_brace, R"(,"UNKNOWN":123)");

    std::cout << "body hex: " << resp->getBodyHex() << "\n";
    auto parsed = resp->getAsJson<Post1, true>();
    if (!parsed) {
        std::cout << "\n===== Strict parse failed (expected) =====\n";
        print_ujson_err(parsed.error(), body);
        co_return;
    }

    std::cout << "UNEXPECTED OK\n";
    co_return;
}

struct Comment {
    int postId;
    int id;
    std::string name;
    std::string email;
    std::string body;
};

using Comments = std::vector<Comment>;

Awaitable<void> run_client_comments_ujson() {
    usub::client::HttpClient client; // unlimited body length

    std::cout << "\n===== Client =====\n";
    auto full = "https://jsonplaceholder.typicode.com/posts/1/comments";

    auto resp = co_await client.get(full);
    if (!resp) {
        std::cerr << "Request failed: " << resp.error().message << "\n";
        co_return;
    }

    std::cout << "body hex: " << resp->getBodyHex() << "\n";

    auto parsed = resp->getAsJson<Comments>();
    if (!parsed) {
        print_ujson_err(parsed.error(), resp->getBody());
        co_return;
    }

    std::cout << "\n===== Parsed =====\n";
    std::cout << "comments count = " << parsed->size() << "\n";

    for (const auto &c: *parsed) {
        std::cout
                << "id=" << c.id
                << " postId=" << c.postId
                << " email='" << c.email << "'\n";
    }

    std::cout << "\n===== Dump =====\n";
    std::cout << ujson::dump(*parsed) << "\n";
    std::cout << "===== End =====\n";

    co_return;
}
#endif

int main() {
    ignore_sigpipe();

    // Adjust the URL/port if your server listens elsewhere
    const std::string url = "https://google.com";// matches config/https.toml non-SSL listener
    const std::string path = "";                 // endpoint from SimpleServer example

    // Create event loop with a single thread and schedule the client coroutine
    auto uvent = std::make_shared<usub::Uvent>(1);
    usub::uvent::system::co_spawn(run_client(url, path));
    usub::uvent::system::co_spawn(run_client_posts_ujson());
    usub::uvent::system::co_spawn(run_client_posts_ujson_strict_fail());
    usub::uvent::system::co_spawn(run_client_comments_ujson());

    // Run the loop until all tasks complete
    uvent->run();
    return 0;
}
