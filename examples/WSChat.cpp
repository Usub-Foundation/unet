// WSChat — a browser-testable WebSocket broadcast server, built on the
// high-level ws::Handler API. One ChatHandler instance is shared across
// every connection; per-connection identity comes through ServerWriter
// (w.id() / w.user_data()).
//
// Run:  ./WSChat        (or build target WSChat)
// Open: http://127.0.0.1:22814/
//
// Architecture demonstrated:
//   - GET /         : serves a tiny HTML page that connects to ws://host/ws.
//   - GET /ws       : upgrade route wrapped via ws::upgradeHandler — the
//                     ws subsystem validates the handshake, writes the 101,
//                     and routes every frame on every connection through the
//                     shared ChatHandler instance below.

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <uvent/Uvent.h>

#include <unet/core/config.hpp>
#include <unet/http.hpp>
#include <unet/http/router/radix.hpp>
#include <unet/ws/core/frame.hpp>
#include <unet/ws/handler.hpp>
#include <unet/ws/reader.hpp>
#include <unet/ws/upgrade.hpp>
#include <unet/ws/writer.hpp>

using usub::unet::http::RequestReader;
using usub::unet::http::ResponseWriter;
using usub::unet::http::ServerRadix;
using usub::unet::ws::ClientReader;
using usub::unet::ws::Handler;
using usub::unet::ws::ServerWriter;

namespace {

    constexpr std::string_view HTML = R"html(<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8"><title>unet ws chat</title>
<style>
 body{font-family:monospace;max-width:680px;margin:40px auto;background:#0f0f0f;color:#e0e0e0}
 h2{color:#7ecfff}
 #log{background:#1a1a1a;border:1px solid #333;padding:12px;height:320px;overflow-y:auto;margin-bottom:12px;font-size:13px;white-space:pre-wrap}
 #msg{width:calc(100% - 90px);padding:8px;background:#222;color:#e0e0e0;border:1px solid #555;font-family:monospace}
 button{padding:8px 16px;background:#7ecfff;color:#000;border:none;cursor:pointer}
 .info{color:#888}.err{color:#f77}.me{color:#aef7ae}
</style></head><body>
<h2>unet ws chat</h2><div id=log></div>
<input id=msg placeholder="type and press Enter" autofocus><button onclick=send()>Send</button>
<p id=st class=info>Connecting…</p>
<script>
const log=document.getElementById('log'),st=document.getElementById('st'),inp=document.getElementById('msg');
function add(t,c){const d=document.createElement('div');d.className=c||'';d.textContent=t;log.appendChild(d);log.scrollTop=log.scrollHeight;}
const ws=new WebSocket('ws://'+location.host+'/ws');
ws.onopen=()=>{st.textContent='Connected ✓';st.className='';add('[connected]','info')};
ws.onmessage=e=>add(e.data);
ws.onerror=()=>{st.textContent='Error';st.className='err'};
ws.onclose=()=>{st.textContent='Disconnected';st.className='err';add('[disconnected]','info')};
function send(){const t=inp.value.trim();if(!t||ws.readyState!==1)return;ws.send(t);add('you: '+t,'me');inp.value='';}
inp.addEventListener('keydown',e=>{if(e.key==='Enter')send();});
</script></body></html>)html";

    usub::uvent::task::Awaitable<void> route_index(RequestReader & /*req*/, ResponseWriter &res) {
        res.metadata.status_code = 200;
        res.headers.addHeader(std::string_view{"content-type"}, std::string_view{"text/html; charset=utf-8"});
        co_await res.send(std::string{HTML});
    }

    // The chat handler. One instance lives for the server's lifetime, every
    // connection's events route through it. Per-connection state (the
    // ServerWriter copies that let us broadcast) lives in `peers_`, keyed by
    // ServerWriter::id() which the engine assigns.
    class ChatHandler : public Handler {
    public:
        usub::uvent::task::Awaitable<void> onOpen(ServerWriter writer) {
            const std::uint64_t id = writer.id();
            {
                std::scoped_lock lk(this->mu_);
                this->peers_.emplace(id, writer);
            }
            const std::string joined =
                    "[client " + std::to_string(id) +
                    " joined — " + std::to_string(this->countLocked()) + " online]";
            std::cout << joined << "\n";
            for (auto &peer: this->snapshotOthers(id)) {
                co_await peer.sendText(joined);
            }
        }

        usub::uvent::task::Awaitable<void>
        text(ClientReader &reader, ServerWriter writer) {
            // Reassemble the full message text across any continuation frames.
            // accumulatePayload() pulls the opener implicitly.
            auto msg = co_await reader.accumulatePayload();
            if (!msg) co_return;

            const std::string out = "[" + std::to_string(writer.id()) + "] " + *msg;
            std::cout << out << "\n";
            for (auto &peer: this->snapshotOthers(writer.id())) {
                co_await peer.sendText(out);
            }
        }

        usub::uvent::task::Awaitable<void> onClose(ServerWriter writer) {
            const std::uint64_t id = writer.id();
            {
                std::scoped_lock lk(this->mu_);
                this->peers_.erase(id);
            }
            const std::string left =
                    "[client " + std::to_string(id) +
                    " left — " + std::to_string(this->countLocked()) + " online]";
            std::cout << left << "\n";
            for (auto &peer: this->snapshotOthers(id)) {
                co_await peer.sendText(left);
            }
        }

    private:
        std::vector<ServerWriter> snapshotOthers(std::uint64_t exclude) {
            std::scoped_lock lk(this->mu_);
            std::vector<ServerWriter> out;
            out.reserve(this->peers_.size());
            for (auto &[id, w]: this->peers_) {
                if (id != exclude) out.push_back(w);
            }
            return out;
        }
        std::size_t countLocked() {
            // Caller may already hold mu_, but unordered_map::size is read-only
            // and trivially racy here — we just want a rough number for the log.
            return this->peers_.size();
        }

        std::mutex                                          mu_;
        std::unordered_map<std::uint64_t, ServerWriter>     peers_;
    };

}// namespace

int main() {
    constexpr std::uint16_t PORT = 22814;

    usub::Uvent uvent{static_cast<int>(std::max(1u, std::thread::hardware_concurrency()))};

    usub::unet::core::Config config;
    {
        usub::unet::core::Config::Object plain;
        plain.emplace("host", usub::unet::core::Config::Value{std::string{"0.0.0.0"}});
        plain.emplace("port", usub::unet::core::Config::Value{static_cast<std::uint64_t>(PORT)});
        usub::unet::core::Config::Object http;
        http.emplace("PlainTextStream", usub::unet::core::Config::Value{std::move(plain)});
        // h2c on — same /ws route serves both h1 GET+Upgrade and h2
        // CONNECT+":protocol=websocket" (RFC 8441).
        http.emplace("enable_h2c", usub::unet::core::Config::Value{true});
        config.root.emplace("HTTP", usub::unet::core::Config::Value{std::move(http)});
    }

    ServerRadix server{uvent, config};
    auto        handler = std::make_shared<ChatHandler>();

    server.handle("GET", "/", route_index);
    // GET    — h1 clients send "GET /ws Upgrade: websocket" (RFC 6455).
    // CONNECT — h2 clients send ":method=CONNECT, :protocol=websocket" (RFC 8441).
    // The same handler answers both.
    server.handleUpgrade(std::set<std::string>{"GET", "CONNECT"}, "/ws",
                          usub::unet::ws::upgradeHandler(handler));

    std::cout << "unet WebSocket chat\n"
                 "  page : http://127.0.0.1:" << PORT << "/\n"
                 "  ws   : ws://127.0.0.1:"   << PORT << "/ws\n\n";
    uvent.run();
    return 0;
}
