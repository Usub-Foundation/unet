// WebSocket chat example.
//
// Serves a small HTML page at http://127.0.0.1:22814/
// Open it in a browser, type messages — they are broadcast to every connected client.
//
// Also accepts raw WebSocket connections at ws://127.0.0.1:22814/ws
// so you can test with external tools (websocat, browser devtools, online testers).
//
// Run:
//   ./WSChat
// Then open:
//   http://127.0.0.1:22814/

#include <atomic>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <uvent/Uvent.h>

#include "unet/core/config.hpp"
#include "unet/http.hpp"
#include "unet/ws.hpp"

namespace {

using namespace usub::unet::ws;

// ─── Connection registry ─────────────────────────────────────────────────────
// Maps a numeric ID → Sender so any coroutine can push to any client.

struct Registry {
    std::mutex                          mu;
    std::unordered_map<uint64_t, Sender> clients;
    std::atomic<uint64_t>               next_id{1};

    uint64_t add(Sender s) {
        uint64_t id = next_id++;
        std::scoped_lock lk(mu);
        clients.emplace(id, std::move(s));
        return id;
    }

    void remove(uint64_t id) {
        std::scoped_lock lk(mu);
        clients.erase(id);
    }

    // Broadcast to every client except the sender.
    // Returns a snapshot so we don't hold the lock during co_await.
    std::vector<Sender> others(uint64_t exclude_id) {
        std::scoped_lock lk(mu);
        std::vector<Sender> out;
        out.reserve(clients.size());
        for (auto &[id, s] : clients) {
            if (id != exclude_id && !s.expired()) { out.push_back(s); }
        }
        return out;
    }

    std::size_t count() {
        std::scoped_lock lk(mu);
        return clients.size();
    }
};

// ─── HTML page ───────────────────────────────────────────────────────────────

static constexpr std::string_view HTML = R"html(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>unet WebSocket chat</title>
<style>
  body { font-family: monospace; max-width: 700px; margin: 40px auto; background: #0f0f0f; color: #e0e0e0; }
  h2   { color: #7ecfff; }
  #log { background: #1a1a1a; border: 1px solid #333; padding: 12px; height: 320px;
         overflow-y: auto; margin-bottom: 12px; font-size: 13px; white-space: pre-wrap; }
  #msg { width: calc(100% - 90px); padding: 8px; background: #222; color: #e0e0e0;
         border: 1px solid #555; font-family: monospace; }
  button { padding: 8px 16px; background: #7ecfff; color: #000; border: none; cursor: pointer; }
  .info { color: #888; } .err { color: #f77; } .me { color: #aef7ae; } .them { color: #fff; }
</style>
</head>
<body>
<h2>unet WebSocket chat</h2>
<div id="log"></div>
<input id="msg" placeholder="Type a message and press Enter or Send" autofocus>
<button onclick="send()">Send</button>
<p id="status" class="info">Connecting…</p>
<script>
const log    = document.getElementById('log');
const status = document.getElementById('status');
const input  = document.getElementById('msg');

function append(text, cls) {
  const line = document.createElement('div');
  line.className = cls || '';
  line.textContent = text;
  log.appendChild(line);
  log.scrollTop = log.scrollHeight;
}

const ws = new WebSocket('ws://' + location.host + '/ws');

ws.onopen = () => {
  status.textContent = 'Connected ✓';
  status.className = '';
  append('[connected]', 'info');
};
ws.onmessage = e => append(e.data, 'them');
ws.onerror   = () => { status.textContent = 'Error'; status.className = 'err'; };
ws.onclose   = () => {
  status.textContent = 'Disconnected';
  status.className = 'err';
  append('[disconnected]', 'info');
};

function send() {
  const text = input.value.trim();
  if (!text || ws.readyState !== WebSocket.OPEN) return;
  ws.send(text);
  append('you: ' + text, 'me');
  input.value = '';
}

input.addEventListener('keydown', e => { if (e.key === 'Enter') send(); });
</script>
</body>
</html>)html";

// ─── Config helper ───────────────────────────────────────────────────────────

usub::unet::core::Config make_config(uint16_t port) {
    usub::unet::core::Config cfg;

    usub::unet::core::Config::Object stream;
    stream["host"]    = usub::unet::core::Config::Value{std::string{"0.0.0.0"}};
    stream["port"]    = usub::unet::core::Config::Value{static_cast<std::uint64_t>(port)};
    stream["backlog"] = usub::unet::core::Config::Value{static_cast<std::uint64_t>(128)};
    stream["version"] = usub::unet::core::Config::Value{static_cast<std::uint64_t>(4)};
    stream["tcp"]     = usub::unet::core::Config::Value{std::string{"tcp"}};

    usub::unet::core::Config::Object http;
    http["PlainTextStream"] = usub::unet::core::Config::Value{std::move(stream)};
    cfg.root["HTTP"] = usub::unet::core::Config::Value{std::move(http)};

    return cfg;
}

}  // namespace

int main() {
    static constexpr uint16_t PORT = 22814;

    Registry registry;

    usub::Uvent runtime{2};
    auto cfg = make_config(PORT);
    usub::unet::http::ServerRadix server{runtime, cfg};

    // ── GET / — serve the chat page ──────────────────────────────────────────
    server.handle("GET", "/",
        [](usub::unet::http::Request &, usub::unet::http::Response &response)
        -> usub::uvent::task::Awaitable<void> {
            response.setStatus(200)
                    .addHeader("Content-Type", "text/html; charset=utf-8")
                    .setBody(std::string(HTML));
            co_return;
        }
    );

    // ── GET /ws — WebSocket upgrade ──────────────────────────────────────────
    server.handleUpgrade("GET", "/ws",
        upgradeHandler([&registry](Connection conn) -> usub::uvent::task::Awaitable<void> {
            const uint64_t id = registry.add(conn.sender());
            const std::string joined = "[client " + std::to_string(id) + " joined — " +
                                       std::to_string(registry.count()) + " online]";
            std::cout << joined << "\n";

            // Announce to existing clients.
            for (auto &s : registry.others(id)) {
                co_await s.send_text(joined);
            }

            while (auto frame = co_await conn.recv()) {
                if (frame->opcode != static_cast<uint8_t>(Opcode::TEXT)) { continue; }

                const std::string msg = "[" + std::to_string(id) + "] " + frame->payload;
                std::cout << msg << "\n";

                // Broadcast to everyone else.
                for (auto &s : registry.others(id)) {
                    co_await s.send_text(msg);
                }
            }

            registry.remove(id);
            const std::string left = "[client " + std::to_string(id) + " left — " +
                                     std::to_string(registry.count()) + " online]";
            std::cout << left << "\n";

            for (auto &s : registry.others(id)) {
                co_await s.send_text(left);
            }
        })
    );

    std::cout << "unet WebSocket chat\n";
    std::cout << "  page : http://127.0.0.1:" << PORT << "/\n";
    std::cout << "  ws   : ws://127.0.0.1:"   << PORT << "/ws\n";
    std::cout << "  test : websocat ws://127.0.0.1:" << PORT << "/ws\n\n";

    runtime.run();
    return 0;
}
