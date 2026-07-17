#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <uvent/Uvent.h>

#include <unet/core/config.hpp>
#include <unet/core/streams/plaintext.hpp>
#ifdef _WIN32
#include <unet/core/streams/schannel.hpp>
#else
#include <unet/core/streams/openssl.hpp>
#endif
#include <unet/http.hpp>
#include <unet/http/router/radix.hpp>
#include <unet/ws/handler.hpp>
#include <unet/ws/reader.hpp>
#include <unet/ws/upgrade.hpp>
#include <unet/ws/writer.hpp>


namespace demo_file {

    // Generated at build time by examples/CMakeLists.txt next to the binary.
    constexpr const char *kServedFile = "unet_demo_file.txt";

    int openReadOnly(const char *path) {
#ifdef _WIN32
        return _open(path, _O_RDONLY | _O_BINARY);
#else
        return ::open(path, O_RDONLY);
#endif
    }

    std::uint64_t sizeOf(int fd) {
#ifdef _WIN32
        return static_cast<std::uint64_t>(_filelengthi64(fd));
#else
        struct stat st{};
        return ::fstat(fd, &st) == 0 ? static_cast<std::uint64_t>(st.st_size) : 0;
#endif
    }

    void closeFd(int fd) {
#ifdef _WIN32
        _close(fd);
#else
        ::close(fd);
#endif
    }

}// namespace demo_file



namespace plain_function_handlers {

    usub::uvent::task::Awaitable<void>
    hello(usub::unet::http::RequestReader &,
          usub::unet::http::ResponseWriter &res) {
        res.metadata.status_code = 200;
        res.headers.addHeader(std::string_view{"content-type"}, std::string_view{"text/plain"});
        co_await res.send(std::string{"hello, world\n"});
    }

    usub::uvent::task::Awaitable<void>
    greetByName(usub::unet::http::RequestReader &,
                usub::unet::http::ResponseWriter &res,
                const usub::unet::http::router::RadixMatch::UriParams &params) {
        std::string_view name{"stranger"};
        if (auto it = params.find("name"); it != params.end()) name = it->second;
        res.metadata.status_code = 200;
        res.headers.addHeader(std::string_view{"content-type"}, std::string_view{"text/plain"});
        std::string body = "hello, ";
        body.append(name);
        body += "\n";
        co_await res.send(std::move(body));
    }

    usub::uvent::task::Awaitable<void>
    echoRequestBody(usub::unet::http::RequestReader &req,
                    usub::unet::http::ResponseWriter &res) {
        constexpr std::size_t kMaxEcho = 1 << 20;
        auto collected = co_await req.collect(kMaxEcho);
        if (!collected) {
            res.metadata.status_code = 413;
            co_await res.send(std::string{"payload too large\n"});
            co_return;
        }
        res.metadata.status_code = 200;
        res.headers.addHeader(std::string_view{"content-type"}, std::string_view{"application/octet-stream"});
        co_await res.send(std::move(*collected));
    }

}// namespace plain_function_handlers



namespace streaming_file_and_metadata {

    usub::uvent::task::Awaitable<void>
    chunkedTicks(usub::unet::http::RequestReader &,
                 usub::unet::http::ResponseWriter &res) {
        constexpr int         kChunks     = 10;
        constexpr std::size_t kPayloadLen = 24;
        constexpr auto        kGap        = std::chrono::milliseconds{300};

        static constexpr std::string_view kAlphabet =
                "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

        std::mt19937 rng{std::random_device{}()};
        std::uniform_int_distribution<std::size_t> pick{0, kAlphabet.size() - 1};

        res.metadata.status_code = 200;
        res.headers.addHeader(std::string_view{"content-type"}, std::string_view{"text/plain"});
        if (!co_await res.start()) co_return;

        for (int i = 1; i <= kChunks; ++i) {
            std::string payload;
            payload.reserve(kPayloadLen);
            for (std::size_t j = 0; j < kPayloadLen; ++j) payload.push_back(kAlphabet[pick(rng)]);

            std::string line = "[data" + std::to_string(i) + "] " + payload + "\n";
            if (!co_await res.chunk(std::move(line))) co_return;

            if (i < kChunks) co_await usub::uvent::system::this_coroutine::sleep_for(kGap);
        }
        co_await res.end();
    }

    usub::uvent::task::Awaitable<void>
    contentLengthFile(usub::unet::http::RequestReader &,
                      usub::unet::http::ResponseWriter &res) {
        const int fd = demo_file::openReadOnly(demo_file::kServedFile);
        if (fd < 0) {
            res.metadata.status_code = 404;
            co_await res.send(std::string{"file missing on disk\n"});
            co_return;
        }
        res.metadata.status_code = 200;
        res.headers.addHeader(std::string_view{"content-type"}, std::string_view{"text/plain"});
        co_await res.file(fd, demo_file::sizeOf(fd));
        demo_file::closeFd(fd);
    }

    usub::uvent::task::Awaitable<void>
    inspectRequest(usub::unet::http::RequestReader &req,
                   usub::unet::http::ResponseWriter &res) {
        std::string body;
        body += "peer.ip   = " + req.peer.ip + "\n";
        body += "peer.port = " + std::to_string(req.peer.port) + "\n";
        body += "peer.alpn = " + req.peer.alpn + "\n";
        body += std::string{"peer.ssl  = "} + (req.peer.ssl ? "true" : "false") + "\n";
        body += "query     = " + req.metadata.uri.query + "\n";
        if (auto h = req.headers.value("user-agent")) {
            body += "user-agent= ";
            body += *h;
            body += "\n";
        }
        res.metadata.status_code = 200;
        res.headers.addHeader(std::string_view{"content-type"}, std::string_view{"text/plain"});
        co_await res.send(std::move(body));
    }

}// namespace streaming_file_and_metadata



namespace error_handler_overrides {

    usub::uvent::task::Awaitable<void>
    notFoundAsJson(usub::unet::http::RequestReader &req,
                   usub::unet::http::ResponseWriter &res) {
        res.headers.addHeader(std::string_view{"content-type"}, std::string_view{"application/json"});
        std::string body;
        body += "{\"error\":\"not_found\",\"path\":\"";
        body += req.metadata.uri.path;
        body += "\"}\n";
        co_await res.send(std::move(body));
    }

    usub::uvent::task::Awaitable<void>
    timeoutStreamed(usub::unet::http::RequestReader &,
                    usub::unet::http::ResponseWriter &res) {
        res.headers.addHeader(std::string_view{"content-type"}, std::string_view{"text/plain"});
        if (!co_await res.start()) co_return;
        (void) co_await res.chunk(std::string{"took too long\n"});
        (void) co_await res.chunk(std::string{"try again with a warmer TCP stack\n"});
        co_await res.end();
    }

    usub::uvent::task::Awaitable<void>
    methodNotAllowed(usub::unet::http::RequestReader &req,
                     usub::unet::http::ResponseWriter &res) {
        res.headers.addHeader(std::string_view{"content-type"}, std::string_view{"text/plain"});
        std::string body = req.metadata.method_token;
        body += " is not allowed on ";
        body += req.metadata.uri.path;
        body += "\n";
        co_await res.send(std::move(body));
    }

    template<class Server>
    void install(Server &server) {
        server.addErrorHandler("404", notFoundAsJson);
        server.addErrorHandler("405", methodNotAllowed);
        server.addErrorHandler("408", timeoutStreamed);
    }

}// namespace error_handler_overrides



namespace class_based_handler {

    class UserApi {
    public:
        explicit UserApi(std::string greeting) : greeting_(std::move(greeting)) {}

        usub::uvent::task::Awaitable<void>
        show(usub::unet::http::RequestReader &,
             usub::unet::http::ResponseWriter &res,
             const usub::unet::http::router::RadixMatch::UriParams &params) {
            std::string_view id{"anon"};
            if (auto it = params.find("id"); it != params.end()) id = it->second;

            std::scoped_lock lock(this->mu_);
            const std::uint64_t seen = ++this->requests_seen_;

            res.metadata.status_code = 200;
            res.headers.addHeader(std::string_view{"content-type"}, std::string_view{"text/plain"});
            std::string body;
            body += this->greeting_;
            body += " user=";
            body.append(id);
            body += " req#=";
            body += std::to_string(seen);
            body += "\n";
            co_await res.send(std::move(body));
        }

    private:
        std::string   greeting_;
        std::mutex    mu_;
        std::uint64_t requests_seen_{0};
    };

}// namespace class_based_handler



namespace websocket_chat {

    class BroadcastHandler : public usub::unet::ws::Handler {
    public:
        static constexpr std::size_t   kMaxHistory     = 200;
        static constexpr std::uint64_t kPingIntervalMs = 15000;

        static usub::uvent::task::Awaitable<void>
        pingLoop(usub::unet::ws::ServerWriter writer) {
            while (!writer.expired()) {
                co_await usub::uvent::system::this_coroutine::sleep_for(
                        std::chrono::milliseconds{kPingIntervalMs});
                if (writer.expired()) co_return;
                co_await writer.ping();
            }
        }

        usub::uvent::task::Awaitable<void>
        onOpen(usub::unet::ws::ServerWriter writer) {
            const std::uint64_t id = writer.id();
            std::vector<std::string> replay;
            {
                std::scoped_lock lock(this->mu_);
                this->peers_.emplace(id, writer);
                replay.assign(this->history_.begin(), this->history_.end());
            }
            for (const auto &line: replay) {
                co_await writer.sendText(line);
            }
            const std::string joined = "[joined " + std::to_string(id) + "]";
            this->record(joined);
            for (auto &peer: this->snapshotOthers(id)) {
                co_await peer.sendText(joined);
            }
            usub::uvent::system::co_spawn(pingLoop(writer));
        }

        usub::uvent::task::Awaitable<void>
        text(usub::unet::ws::ClientReader &reader,
             usub::unet::ws::ServerWriter writer) {
            auto msg = co_await reader.accumulatePayload();
            if (!msg) co_return;
            const std::string relay = std::to_string(writer.id()) + ": " + *msg;
            this->record(relay);
            for (auto &peer: this->snapshotOthers(writer.id())) {
                co_await peer.sendText(relay);
            }
        }

        usub::uvent::task::Awaitable<void>
        onClose(usub::unet::ws::ServerWriter writer) {
            const std::uint64_t id = writer.id();
            {
                std::scoped_lock lock(this->mu_);
                this->peers_.erase(id);
            }
            const std::string left = "[left " + std::to_string(id) + "]";
            this->record(left);
            for (auto &peer: this->snapshotOthers(id)) {
                co_await peer.sendText(left);
            }
        }

        usub::uvent::task::Awaitable<void>
        historyDump(usub::unet::http::RequestReader &,
                    usub::unet::http::ResponseWriter &res) {
            std::string body;
            {
                std::scoped_lock lock(this->mu_);
                body.reserve(this->history_.size() * 32);
                for (const auto &line: this->history_) {
                    body.append(line);
                    body.push_back('\n');
                }
            }
            res.metadata.status_code = 200;
            res.headers.addHeader(std::string_view{"content-type"}, std::string_view{"text/plain; charset=utf-8"});
            co_await res.send(std::move(body));
        }

    private:
        std::vector<usub::unet::ws::ServerWriter> snapshotOthers(std::uint64_t exclude) {
            std::scoped_lock lock(this->mu_);
            std::vector<usub::unet::ws::ServerWriter> out;
            out.reserve(this->peers_.size());
            for (auto &[id, writer]: this->peers_) {
                if (id != exclude) out.push_back(writer);
            }
            return out;
        }

        void record(std::string line) {
            std::scoped_lock lock(this->mu_);
            this->history_.push_back(std::move(line));
            if (this->history_.size() > kMaxHistory) {
                this->history_.erase(this->history_.begin(),
                                     this->history_.begin() + (this->history_.size() - kMaxHistory));
            }
        }

        std::mutex                                                     mu_;
        std::unordered_map<std::uint64_t, usub::unet::ws::ServerWriter> peers_;
        std::vector<std::string>                                       history_;
    };

}// namespace websocket_chat



namespace middleware_and_lifecycle_hooks {

    usub::uvent::task::Awaitable<bool>
    logEachRequest(usub::unet::http::RequestReader &req,
                   usub::unet::http::ResponseWriter &) {
        std::cout << "[mw] " << req.metadata.method_token
                  << " " << req.metadata.uri.path
                  << " (from " << req.peer.ip << ":" << req.peer.port << ")\n";
        co_return true;
    }

    usub::uvent::task::Awaitable<bool>
    requireAuthOnSecretPath(usub::unet::http::RequestReader &req,
                            usub::unet::http::ResponseWriter &res) {
        if (req.metadata.uri.path != "/secret") co_return true;
        const auto header = req.headers.value("x-auth");
        if (header && *header == "please") co_return true;
        res.metadata.status_code = 401;
        res.headers.addHeader(std::string_view{"content-type"}, std::string_view{"text/plain"});
        co_await res.send(std::string{"missing or invalid X-Auth\n"});
        co_return false;
    }

    void tuneEachHttp1Connection(usub::unet::http::v1::Connection &conn) {
        conn.keep_alive_max_requests   = 100;
        conn.max_keep_alive_timeout    = std::chrono::milliseconds{15000};
        conn.default_keep_alive_timeout = std::chrono::milliseconds{10000};
    }

    void tuneEachHttp2Connection(usub::unet::http::v2::Connection &conn) {
        conn.max_continuations_per_header_block = 8;
        conn.idle_timeout                        = std::chrono::milliseconds{30000};
    }

    void announceEachHttp2Stream(usub::unet::http::v2::Connection &conn,
                                 std::uint32_t stream_id) {
        std::cout << "[h2] stream=" << stream_id
                  << " on connection alpn='" << conn.peer.alpn << "' from "
                  << conn.peer.ip << ":" << conn.peer.port << "\n";
    }

    usub::uvent::task::Awaitable<void>
    secretHandler(usub::unet::http::RequestReader &,
                  usub::unet::http::ResponseWriter &res) {
        res.metadata.status_code = 200;
        res.headers.addHeader(std::string_view{"content-type"}, std::string_view{"text/plain"});
        co_await res.send(std::string{"you know the phrase\n"});
    }

}// namespace middleware_and_lifecycle_hooks



namespace landing_page {

    constexpr std::string_view kLandingHtml = R"html(<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8"><title>unet HTTPServer</title>
<style>body{font-family:monospace;max-width:720px;margin:40px auto;background:#0f0f0f;color:#e0e0e0}
h2{color:#7ecfff}a{color:#aef7ae}li{margin:6px 0}</style></head><body>
<h2>unet HTTPServer</h2>
<ul>
 <li><a href="/hello">GET /hello</a></li>
 <li><a href="/greet/world">GET /greet/{name}</a></li>
 <li>POST /echo &mdash; body echoes back</li>
 <li><a href="/stream">GET /stream</a> &mdash; chunked (raw output)</li>
 <li><a href="/stream-page">GET /stream-page</a> &mdash; browser demo, watch chunks arrive live</li>
 <li><a href="/file">GET /file</a> &mdash; content-length</li>
 <li><a href="/inspect?a=1&amp;b=2">GET /inspect</a> &mdash; peer + query + header</li>
 <li><a href="/users/42">GET /users/{id}</a> &mdash; class handler via std::bind_front</li>
 <li><a href="/secret">GET /secret</a> &mdash; needs <code>X-Auth: please</code></li>
 <li><a href="/does-not-exist">GET /does-not-exist</a> &mdash; JSON 404 via error handler</li>
 <li><a href="/ws-page">WebSocket page</a></li>
 <li><a href="/ws-history">GET /ws-history</a> &mdash; chat history via HTTP (same class binds ws + http)</li>
</ul>
</body></html>)html";

    constexpr std::string_view kStreamPageHtml = R"html(<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8"><title>unet stream demo</title>
<style>body{font-family:monospace;max-width:720px;margin:40px auto;background:#0f0f0f;color:#e0e0e0}
h2{color:#7ecfff}#log{background:#1a1a1a;border:1px solid #333;padding:12px;height:340px;overflow-y:auto;
white-space:pre-wrap;line-height:1.35}
button{padding:8px 16px;background:#7ecfff;color:#000;border:none;cursor:pointer;margin-right:8px}
.ts{color:#7ecfff}.dt{color:#aef7ae}</style></head><body>
<h2>unet stream demo</h2>
<p>Click <b>Start</b>. Each line is a chunk from <code>GET /stream</code> as the browser receives it.
Timestamps come from <code>performance.now()</code> at receive time, so the deltas show real network arrival gaps.</p>
<button onclick=start()>Start</button><button onclick=clear_()>Clear</button>
<div id=log></div>
<script>
const log = document.getElementById('log');
function clear_() { log.textContent = ''; }
function line(html) {
    const d = document.createElement('div');
    d.innerHTML = html;
    log.appendChild(d);
    log.scrollTop = log.scrollHeight;
}
async function start() {
    clear_();
    const t0 = performance.now();
    let prev = t0;
    line('<span class="ts">t+0ms</span> connecting...');
    let response;
    try {
        response = await fetch('/stream');
    } catch (err) {
        line('<span class="ts">error</span> ' + err.message);
        return;
    }
    if (!response.body) {
        line('<span class="ts">error</span> streaming not supported by this browser');
        return;
    }
    const reader = response.body.getReader();
    const decoder = new TextDecoder();
    let buf = '';
    while (true) {
        const { value, done } = await reader.read();
        if (done) break;
        buf += decoder.decode(value, { stream: true });
        let nl;
        while ((nl = buf.indexOf('\n')) >= 0) {
            const chunk = buf.slice(0, nl);
            buf = buf.slice(nl + 1);
            const now = performance.now();
            const dt = Math.round(now - prev);
            const abs = Math.round(now - t0);
            prev = now;
            line(`<span class="ts">t+${abs}ms</span> <span class="dt">Δ${dt}ms</span> ${chunk.replace(/</g,'&lt;')}`);
        }
    }
    if (buf.length) {
        const abs = Math.round(performance.now() - t0);
        line(`<span class="ts">t+${abs}ms</span> ${buf.replace(/</g,'&lt;')} <i>(no trailing newline)</i>`);
    }
    const abs = Math.round(performance.now() - t0);
    line(`<span class="ts">t+${abs}ms</span> <i>[done]</i>`);
}
</script></body></html>)html";

    constexpr std::string_view kWsPageHtml = R"html(<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8"><title>unet ws</title>
<style>body{font-family:monospace;max-width:680px;margin:40px auto;background:#0f0f0f;color:#e0e0e0}
h2{color:#7ecfff}#log{background:#1a1a1a;border:1px solid #333;padding:12px;height:280px;overflow-y:auto;white-space:pre-wrap}
#msg{width:calc(100% - 90px);padding:8px;background:#222;color:#e0e0e0;border:1px solid #555}
button{padding:8px 16px;background:#7ecfff;color:#000;border:none;cursor:pointer}</style></head><body>
<h2>unet ws</h2><div id=log></div>
<input id=msg placeholder="type and press Enter" autofocus><button onclick=send()>Send</button>
<script>
const log=document.getElementById('log'),inp=document.getElementById('msg');
function add(t){const d=document.createElement('div');d.textContent=t;log.appendChild(d);log.scrollTop=log.scrollHeight;}
const ws=new WebSocket((location.protocol==='https:'?'wss://':'ws://')+location.host+'/ws');
ws.onopen=()=>add('[connected]');ws.onmessage=e=>add(e.data);ws.onclose=()=>add('[disconnected]');
function send(){const t=inp.value.trim();if(!t||ws.readyState!==1)return;ws.send(t);add('me: '+t);inp.value='';}
inp.addEventListener('keydown',e=>{if(e.key==='Enter')send();});
</script></body></html>)html";

    usub::uvent::task::Awaitable<void>
    index(usub::unet::http::RequestReader &,
          usub::unet::http::ResponseWriter &res) {
        res.metadata.status_code = 200;
        res.headers.addHeader(std::string_view{"content-type"}, std::string_view{"text/html; charset=utf-8"});
        co_await res.send(std::string{kLandingHtml});
    }

    usub::uvent::task::Awaitable<void>
    wsIndex(usub::unet::http::RequestReader &,
            usub::unet::http::ResponseWriter &res) {
        res.metadata.status_code = 200;
        res.headers.addHeader(std::string_view{"content-type"}, std::string_view{"text/html; charset=utf-8"});
        co_await res.send(std::string{kWsPageHtml});
    }

    usub::uvent::task::Awaitable<void>
    streamIndex(usub::unet::http::RequestReader &,
                usub::unet::http::ResponseWriter &res) {
        res.metadata.status_code = 200;
        res.headers.addHeader(std::string_view{"content-type"}, std::string_view{"text/html; charset=utf-8"});
        co_await res.send(std::string{kStreamPageHtml});
    }

}// namespace landing_page


int main() {
    usub::unet::core::Config config;
    {
        usub::unet::core::Config::Object plain_section;
        plain_section.emplace("host", usub::unet::core::Config::Value{std::string{"127.0.0.1"}});
        plain_section.emplace("port", usub::unet::core::Config::Value{static_cast<std::uint64_t>(8080)});

        usub::unet::core::Config::Object tls_section;
        tls_section.emplace("host", usub::unet::core::Config::Value{std::string{"127.0.0.1"}});
        tls_section.emplace("port", usub::unet::core::Config::Value{static_cast<std::uint64_t>(8443)});
#ifdef _WIN32
        tls_section.emplace("pfx",      usub::unet::core::Config::Value{std::string{"server.pfx"}});
        tls_section.emplace("password", usub::unet::core::Config::Value{std::string{"demo"}});
#else
        tls_section.emplace("cert", usub::unet::core::Config::Value{std::string{"cert.pem"}});
        tls_section.emplace("key",  usub::unet::core::Config::Value{std::string{"key.pem"}});
#endif

        usub::unet::core::Config::Object http_section;
        http_section.emplace("PlainTextStream", usub::unet::core::Config::Value{std::move(plain_section)});
#ifdef _WIN32
        http_section.emplace("SChannelStream", usub::unet::core::Config::Value{std::move(tls_section)});
#else
        http_section.emplace("OpenSSLStream", usub::unet::core::Config::Value{std::move(tls_section)});
#endif
        http_section.emplace("enable_h2c", usub::unet::core::Config::Value{true});

        config.root.emplace("HTTP", usub::unet::core::Config::Value{std::move(http_section)});
    }

    usub::Uvent uvent{static_cast<int>(std::max(1u, std::thread::hardware_concurrency()))};

    usub::unet::http::ServerImpl<
            usub::unet::http::router::Radix,
            usub::unet::core::stream::PlainText,
#ifdef _WIN32
            usub::unet::core::stream::SChannelStream<"h2", "http/1.1">
#else
            usub::unet::core::stream::OpenSSLStream<"h2", "http/1.1">
#endif
            > server{uvent, config};

    server.addMiddleware(usub::unet::http::MIDDLEWARE_PHASE::HEADER,
                          middleware_and_lifecycle_hooks::logEachRequest);
    server.addMiddleware(usub::unet::http::MIDDLEWARE_PHASE::HEADER,
                          middleware_and_lifecycle_hooks::requireAuthOnSecretPath);

    server.onHTTP1Connection(middleware_and_lifecycle_hooks::tuneEachHttp1Connection);
    server.onHTTP2Connection(middleware_and_lifecycle_hooks::tuneEachHttp2Connection);
    server.onHTTP2Stream(middleware_and_lifecycle_hooks::announceEachHttp2Stream);

    error_handler_overrides::install(server);

    server.handle("GET",  "/",              landing_page::index);
    server.handle("GET",  "/ws-page",       landing_page::wsIndex);
    server.handle("GET",  "/stream-page",   landing_page::streamIndex);

    server.handle("GET",  "/hello",         plain_function_handlers::hello);
    server.handle("GET",  "/greet/{name}",  plain_function_handlers::greetByName);
    server.handle("POST", "/echo",          plain_function_handlers::echoRequestBody);

    server.handle("GET",  "/stream",        streaming_file_and_metadata::chunkedTicks);
    server.handle("GET",  "/file",          streaming_file_and_metadata::contentLengthFile);
    server.handle("GET",  "/inspect",       streaming_file_and_metadata::inspectRequest);

    server.handle("GET",  "/secret",        middleware_and_lifecycle_hooks::secretHandler);

    auto user_api = std::make_shared<class_based_handler::UserApi>(std::string{"hi"});
    server.handle("GET", "/users/{id}",
                   std::bind_front(&class_based_handler::UserApi::show, user_api.get()));

    auto chat_handler = std::make_shared<websocket_chat::BroadcastHandler>();
    server.handleUpgrade(std::set<std::string>{"GET", "CONNECT"}, "/ws",
                          usub::unet::ws::upgradeHandler(chat_handler));
    server.handle("GET", "/ws-history",
                   std::bind_front(&websocket_chat::BroadcastHandler::historyDump, chat_handler.get()));

    std::cout << "unet HTTPServer\n"
                 "  plaintext : http://127.0.0.1:8080/\n"
                 "  tls       : https://127.0.0.1:8443/ (needs cert/key in examples/)\n"
                 "  ws        : ws://127.0.0.1:8080/ws (via /ws-page)\n";

    uvent.run();
    return 0;
}
