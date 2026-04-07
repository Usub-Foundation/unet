#include <any>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <uvent/Uvent.h>

#include <unet/core/config.hpp>
#include <unet/http.hpp>
#include <unet/http/router/radix.hpp>
#include <unet/mime/multipart/form_data/generic.hpp>

namespace {

    using Request = usub::unet::http::Request;
    using Response = usub::unet::http::Response;
    using RadixMatch = usub::unet::http::router::RadixMatch;

    struct RequestContext {
        std::uint64_t request_id{0};
        std::chrono::steady_clock::time_point started_at{};
        std::string tenant{};
        bool authenticated{false};
    };

    struct TaskItem {
        std::uint64_t id{0};
        std::string title;
        std::string details;
    };

    class TaskStore {
    public:
        TaskItem create(std::string title, std::string details) {
            std::scoped_lock lock(mutex_);
            TaskItem item{.id = next_id_++, .title = std::move(title), .details = std::move(details)};
            tasks_.push_back(item);
            return item;
        }

        std::vector<TaskItem> list() const {
            std::scoped_lock lock(mutex_);
            return tasks_;
        }

        std::optional<TaskItem> get(std::uint64_t id) const {
            std::scoped_lock lock(mutex_);
            for (const auto &task: tasks_) {
                if (task.id == id) { return task; }
            }
            return std::nullopt;
        }

    private:
        mutable std::mutex mutex_;
        std::vector<TaskItem> tasks_;
        std::uint64_t next_id_{1};
    };

struct AppServices {
    TaskStore tasks;
    std::atomic<std::uint64_t> request_counter{0};
    std::string api_key{"demo-local-key"};
};

std::string json_escape(std::string_view input) {
        std::string out;
        out.reserve(input.size() + 8);
        for (char c: input) {
            if (c == '"') {
                out += "\\\"";
            } else if (c == '\\') {
                out += "\\\\";
            } else if (c == '\n') {
                out += "\\n";
            } else if (c == '\r') {
                out += "\\r";
            } else if (c == '\t') {
                out += "\\t";
            } else {
                out += c;
            }
        }
        return out;
    }

    void set_json(Response &response, std::uint16_t status, std::string body) {
        response.setStatus(status).addHeader("content-type", "application/json").setBody(std::move(body));
    }

    void set_text(Response &response, std::uint16_t status, std::string body) {
        response.setStatus(status).addHeader("content-type", "text/plain; charset=utf-8").setBody(std::move(body));
    }

    std::string read_header(const Request &request, std::string_view key) {
        auto value = request.headers.value(key);
        if (!value.has_value()) { return {}; }
        return std::string(*value);
    }

    RequestContext *get_context(Request &request) { return std::any_cast<RequestContext>(&request.user_data); }

    const RequestContext *get_context(const Request &request) {
        return std::any_cast<RequestContext>(&request.user_data);
    }

    std::uint64_t elapsed_ms(const RequestContext &ctx) {
        using namespace std::chrono;
        return static_cast<std::uint64_t>(duration_cast<milliseconds>(steady_clock::now() - ctx.started_at).count());
    }

    bool attach_context(AppServices &services, Request &request, Response &) {
        RequestContext ctx{};
        ctx.request_id = ++services.request_counter;
        ctx.started_at = std::chrono::steady_clock::now();
        ctx.tenant = read_header(request, "x-tenant");
        request.user_data = std::move(ctx);
        return true;
    }

    bool require_tenant(AppServices &, Request &request, Response &response) {
        const auto *ctx = get_context(request);
        if (!ctx || ctx->tenant.empty()) {
            set_json(response, 400, R"({"error":"missing x-tenant header","hint":"send x-tenant: sandbox"})");
            return false;
        }
        return true;
    }

    bool require_api_key(AppServices &services, Request &request, Response &response) {
        if (read_header(request, "x-api-key") != services.api_key) {
            set_json(response, 401, R"({"error":"invalid api key","hint":"send x-api-key: demo-local-key"})");
            return false;
        }

        if (auto *ctx = get_context(request)) { ctx->authenticated = true; }
        return true;
    }

    bool annotate_body_size(AppServices &, Request &request, Response &response) {
        response.addHeader("x-request-body-size", std::to_string(request.body.size()));
        return true;
    }

std::optional<std::string> parse_boundary(const Request &request) {
        auto ct = request.headers.value("content-type");
        if (!ct.has_value()) { return std::nullopt; }

        std::string value = std::string(*ct);
        const std::string token = "boundary=";
        const auto pos = value.find(token);
        if (pos == std::string::npos) { return std::nullopt; }

        std::string boundary = value.substr(pos + token.size());
        auto semicolon = boundary.find(';');
        if (semicolon != std::string::npos) { boundary = boundary.substr(0, semicolon); }
        if (!boundary.empty() && boundary.front() == '"' && boundary.back() == '"' && boundary.size() >= 2) {
            boundary = boundary.substr(1, boundary.size() - 2);
        }
    return boundary;
}

class AdminController {
public:
    explicit AdminController(std::string name) : name_(std::move(name)) {}

    usub::uvent::task::Awaitable<void> status(Request &request, Response &response) {
        const auto *ctx = get_context(request);

        std::ostringstream json;
        json << "{\"controller\":\"" << json_escape(name_) << "\"";
        if (ctx) {
            json << ",\"request_id\":" << ctx->request_id;
        }
        json << ",\"ok\":true}";

        set_json(response, 200, json.str());
        co_return;
    }

private:
    std::string name_;
};

    usub::uvent::task::Awaitable<void> health(AppServices &, Request &request, Response &response) {
        const auto *ctx = get_context(request);
        const auto request_id = ctx ? std::to_string(ctx->request_id) : "0";

        set_json(response, 200, "{\"service\":\"http_showcase\",\"status\":\"ok\",\"request_id\":" + request_id + "}");
        co_return;
    }

    usub::uvent::task::Awaitable<void> list_tasks(AppServices &services, Request &, Response &response) {
        const auto tasks = services.tasks.list();

        std::ostringstream json;
        json << "{\"tasks\":[";
        for (std::size_t i = 0; i < tasks.size(); ++i) {
            const auto &task = tasks[i];
            if (i != 0) { json << ','; }
            json << "{\"id\":" << task.id << ",\"title\":\"" << json_escape(task.title) << "\",\"details\":\""
                 << json_escape(task.details) << "\"}";
        }
        json << "]}";

        set_json(response, 200, json.str());
        co_return;
    }

    usub::uvent::task::Awaitable<void> get_task(AppServices &services, Request &, Response &response,
                                                const RadixMatch::UriParams &params) {
        auto it = params.find("id");
        if (it == params.end()) {
            set_json(response, 400, R"({"error":"task id is missing"})");
            co_return;
        }

        const std::uint64_t id = static_cast<std::uint64_t>(std::stoull(it->second));
        auto task = services.tasks.get(id);
        if (!task.has_value()) {
            set_json(response, 404, R"({"error":"task not found"})");
            co_return;
        }

        set_json(response, 200,
                 "{\"id\":" + std::to_string(task->id) + ",\"title\":\"" + json_escape(task->title) +
                         "\",\"details\":\"" + json_escape(task->details) + "\"}");
        co_return;
    }

    usub::uvent::task::Awaitable<void> create_task(AppServices &services, Request &request, Response &response) {
        auto created = services.tasks.create("task-" + std::to_string(services.request_counter.load()), request.body);

        set_json(response, 201,
                 "{\"created\":true,\"task\":{\"id\":" + std::to_string(created.id) + ",\"title\":\"" +
                         json_escape(created.title) + "\"}}");
        co_return;
    }

    usub::uvent::task::Awaitable<void> upload(AppServices &, Request &request, Response &response) {
        auto boundary = parse_boundary(request);
        if (!boundary.has_value()) {
            set_json(
                    response, 400,
                    R"({"error":"multipart boundary missing","hint":"use Content-Type: multipart/form-data; boundary=..."})");
            co_return;
        }

        usub::unet::mime::multipart::FormData form(*boundary);
        auto parsed = form.parse(request.body);
        if (!parsed) {
            set_json(response, 400,
                     "{\"error\":\"multipart parse failed\",\"details\":\"" + json_escape(parsed.error()) + "\"}");
            co_return;
        }

        std::size_t part_count = 0;
        std::size_t total_bytes = 0;
        for (const auto &[name, parts]: form.parts_by_name()) {
            (void) name;
            part_count += parts.size();
            for (const auto &part: parts) { total_bytes += part.data.size(); }
        }

        set_json(response, 200,
                 "{\"parts\":" + std::to_string(part_count) + ",\"bytes\":" + std::to_string(total_bytes) + "}");
        co_return;
    }

    usub::uvent::task::Awaitable<void> files(AppServices &, Request &, Response &response, RadixMatch &match) {
        set_text(response, 200, "virtual file tail: " + std::string(match.param("*").value_or("")) + "\n");
        co_return;
    }

    usub::uvent::task::Awaitable<void> debug(AppServices &, Request &request, Response &response, RadixMatch &match) {
        const auto *ctx = get_context(request);

        std::ostringstream body;
        body << "{\"method\":\"" << json_escape(request.metadata.method_token) << "\",\"path\":\""
             << json_escape(request.metadata.uri.path) << "\",\"query\":\"" << json_escape(request.metadata.uri.query)
             << "\",\"tail\":\"" << json_escape(std::string(match.param("*").value_or(""))) << "\"";

        if (ctx) {
            body << ",\"request_id\":" << ctx->request_id << ",\"tenant\":\"" << json_escape(ctx->tenant)
                 << "\",\"authenticated\":" << (ctx->authenticated ? "true" : "false")
                 << ",\"elapsed_ms\":" << elapsed_ms(*ctx);
        }

        body << '}';
        set_json(response, 200, body.str());
        co_return;
    }

    void log_error(const Request &request, Response &response) {
        std::cerr << "[http_showcase] " << request.metadata.method_token << ' ' << request.metadata.uri.path << " -> "
                  << response.metadata.status_code << '\n';
    }

    void not_found(const Request &, Response &response) { set_json(response, 404, R"({"error":"route not found"})"); }

    void method_not_allowed(const Request &, Response &response) {
        set_json(response, 405, R"({"error":"method not allowed"})");
    }

void register_routes(usub::unet::http::ServerRadix &server, AppServices &services, AdminController &controller) {
        using usub::unet::http::MIDDLEWARE_PHASE;
        using usub::unet::http::router::param_constraint;

        static const param_constraint numeric_constraint{R"([0-9]+)", "id must be numeric"};
        static const std::unordered_map<std::string_view, const param_constraint *> task_constraints{
                {"id", &numeric_constraint}};

        server.addErrorHandler("log", log_error);
        server.addErrorHandler("404", not_found);
        server.addErrorHandler("405", method_not_allowed);

        server.addMiddleware(MIDDLEWARE_PHASE::HEADER, std::bind_front(attach_context, std::ref(services)));
        server.addMiddleware(MIDDLEWARE_PHASE::HEADER, std::bind_front(require_tenant, std::ref(services)));

        server.handle("GET", "/health", std::bind_front(health, std::ref(services)));
        server.handle("GET", "/v1/tasks", std::bind_front(list_tasks, std::ref(services)));
        server.handle("GET", "/v1/tasks/{id}", std::bind_front(get_task, std::ref(services)), task_constraints);

        server.handle("POST", "/v1/tasks", std::bind_front(create_task, std::ref(services)))
                .addMiddleware(MIDDLEWARE_PHASE::HEADER, std::bind_front(require_api_key, std::ref(services)))
                .addMiddleware(MIDDLEWARE_PHASE::BODY, std::bind_front(annotate_body_size, std::ref(services)));

        server.handle("POST", "/v1/upload", std::bind_front(upload, std::ref(services)))
                .addMiddleware(MIDDLEWARE_PHASE::HEADER, std::bind_front(require_api_key, std::ref(services)))
                .addMiddleware(MIDDLEWARE_PHASE::BODY, std::bind_front(annotate_body_size, std::ref(services)));

    server.handle("GET", "/v1/files/*", std::bind_front(files, std::ref(services)));
    server.handle("*", "/v1/debug/*", std::bind_front(debug, std::ref(services)));

    // Class-member handler binding example (C++23): bind instance + member function.
    // Equivalent alternatives: lambda capture or std::bind.
    server.handle("GET", "/v1/controller/status", std::bind_front(&AdminController::status, &controller));
}

    usub::unet::core::Config make_server_config() {
        usub::unet::core::Config config{};

        usub::unet::core::Config::Object stream_cfg{};
        stream_cfg["host"] = usub::unet::core::Config::Value{std::string{"127.0.0.1"}};
        stream_cfg["port"] = usub::unet::core::Config::Value{static_cast<std::uint64_t>(22813)};
        stream_cfg["backlog"] = usub::unet::core::Config::Value{static_cast<std::uint64_t>(128)};
        stream_cfg["version"] = usub::unet::core::Config::Value{static_cast<std::uint64_t>(4)};
        stream_cfg["tcp"] = usub::unet::core::Config::Value{std::string{"tcp"}};

        usub::unet::core::Config::Object http_cfg{};
        http_cfg["PlainTextStream"] = usub::unet::core::Config::Value{std::move(stream_cfg)};
        config.root["HTTP"] = usub::unet::core::Config::Value{std::move(http_cfg)};

        return config;
    }

}// namespace

int main() {
    usub::Uvent runtime{2};
    auto config = make_server_config();
    usub::unet::http::ServerRadix server{runtime, config};

    AppServices services{};
    AdminController controller{"admin-v1"};
    register_routes(server, services, controller);

    std::cout << "http showcase server listening on http://"
              << config.getString("HTTP.PlainTextStream.host", "127.0.0.1") << ':'
              << config.getUInt("HTTP.PlainTextStream.port", 22813) << "\n";
    std::cout << "quick check: curl -i -H 'x-tenant: sandbox' http://127.0.0.1:22813/health\n";

    runtime.run();
    return 0;
}
