#include "upq/PgPool.h"
#include "upq/PgTransaction.h"
#include "upq/PgReflect.h"
#include "upq/PgNotificationMultiplexer.h"
#include "uvent/Uvent.h"
#include "unet/http.hpp"
#include "unet/http/router/radix.hpp"

#include <csignal>
#include <cstring>
#include <optional>
#include <array>
#include <list>
#include <set>
#include <vector>
#include <string>
#include <tuple>
#include <iostream>

using usub::uvent::task::Awaitable;

// ---------------- Models ----------------
struct NewUser
{
    std::string name;
    std::optional<std::string> password;
    std::vector<int> roles;
    std::vector<std::string> tags;
};

struct UserRow
{
    int64_t id;
    std::string username;
    std::optional<std::string> password;
    std::vector<int> roles;
    std::vector<std::string> tags;
};

struct Upd
{
    std::vector<int> roles;
    int64_t id;
};


// ---------------- Utils ----------------
static inline std::string jescape(std::string_view s)
{
    std::string o;
    o.reserve(s.size() + 8);
    for (char c : s)
    {
        if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else o += c;
    }
    return o;
}

// ---------------- Schema ----------------
Awaitable<bool> ensure_schema(usub::pg::PgPool& pool)
{
    auto q = [&](std::string_view sql) -> Awaitable<bool>
    {
        auto r = co_await pool.query_awaitable(std::string{sql});
        co_return r.ok;
    };

    if (!co_await q(R"SQL(
        CREATE TABLE IF NOT EXISTS public.users(
            id BIGSERIAL PRIMARY KEY,
            name TEXT,
            password TEXT
        )
    )SQL"))
        co_return false;

    if (!co_await q(R"SQL(
        CREATE TABLE IF NOT EXISTS public.users_reflect(
            id BIGSERIAL PRIMARY KEY,
            name TEXT NOT NULL,
            password TEXT,
            roles INT4[] NOT NULL,
            tags  TEXT[] NOT NULL
        )
    )SQL"))
        co_return false;

    if (!co_await q(R"SQL(CREATE TABLE IF NOT EXISTS public.users_r(
    id BIGSERIAL PRIMARY KEY, name TEXT NOT NULL, password TEXT,
    roles INT4[] NOT NULL DEFAULT '{}', tags TEXT[] NOT NULL DEFAULT '{}');)SQL"))
        co_return false;

    if (!co_await q(R"SQL(CREATE TABLE IF NOT EXISTS public.array_test_multi(
    id BIGSERIAL PRIMARY KEY, a_int4_1 INT4[] NOT NULL, a_int4_2 INT4[] NOT NULL,
    a_float8 FLOAT8[] NOT NULL, a_bool BOOL[] NOT NULL, a_text TEXT[] NOT NULL, comment TEXT);)SQL"))
        co_return false;

    if (!co_await q(R"SQL(
        CREATE TABLE IF NOT EXISTS public.array_test_multi(
            id BIGSERIAL PRIMARY KEY,
            a_int4_1 INT4[]  NOT NULL,
            a_int4_2 INT4[]  NOT NULL,
            a_float8 FLOAT8[] NOT NULL,
            a_bool BOOL[]    NOT NULL,
            a_text TEXT[]    NOT NULL,
            comment TEXT
        )
    )SQL"))
        co_return false;

    if (!co_await q(R"SQL(
        CREATE TABLE IF NOT EXISTS public.bigdata(
            id BIGSERIAL PRIMARY KEY,
            payload TEXT
        )
    )SQL"))
        co_return false;

    co_return true;
}

// ---------------- DB helpers ----------------
Awaitable<std::optional<UserRow>> fetch_user_by_id(usub::pg::PgPool& pool, int64_t id)
{
    auto rows = co_await pool.query_reflect<UserRow>(
        "SELECT id, name AS username, password, '{}'::int4[] AS roles, '{}'::text[] AS tags "
        "FROM users WHERE id=$1 LIMIT 1;", id);
    if (rows.empty()) co_return std::nullopt;
    co_return rows.front();
}

// ---------------- HTTP Handlers (factories capturing PgPool&) ----------------
auto make_get_user_handler(usub::pg::PgPool& pool)
{
    return [&pool](auto&, auto& resp, const usub::unet::http::router::RadixMatch::UriParams &uri_params) -> Awaitable<void>
    {
        auto id_it = uri_params.find("id");
        std::string idraw = id_it != uri_params.end() ? id_it->second : "0";
        int64_t id = 0;
        try { id = std::stoll(idraw); }
        catch (...)
        {
            resp.setStatus(400)
                .addHeader("Content-Type", "application/json")
                .setBody("{\"error\":\"invalid id\"}\n");
            co_return;
        }
        auto u = co_await fetch_user_by_id(pool, id);
        if (!u)
        {
            resp.setStatus(404)
                .addHeader("Content-Type", "application/json")
                .setBody("{\"error\":\"user not found\"}\n");
            co_return;
        }
        std::string body = "{\"id\":" + std::to_string(u->id) + ",\"name\":\"" + jescape(u->username) + "\"}";
        resp.setStatus(200).addHeader("Content-Type", "application/json").setBody(body);
        co_return;
    };
}

auto make_post_users_seed(usub::pg::PgPool& pool)
{
    return [&pool](auto&, auto& resp) -> Awaitable<void>
    {
        NewUser a{.name = "Alice", .password = std::nullopt, .roles = {1, 2, 5}, .tags = {"admin", "core"}};
        auto r1 = co_await pool.exec_reflect(
            "INSERT INTO users_reflect(name,password,roles,tags) VALUES($1,$2,$3,$4);", a);
        if (!r1.ok)
        {
            resp.setStatus(500).setBody("A: " + r1.error + "\n");
            co_return;
        }

        NewUser b{
            .name = "Bob", .password = std::optional<std::string>{"x"},
            .roles = {3, 4}, .tags = {"beta", "labs"}
        };
        auto r2 = co_await pool.exec_reflect(
            "INSERT INTO users_reflect(name,password,roles,tags) VALUES($1,$2,$3,$4);", b);
        if (!r2.ok)
        {
            resp.setStatus(500).setBody("B: " + r2.error + "\n");
            co_return;
        }

        resp.setStatus(200).addHeader("Content-Type", "application/json")
            .setBody("{\"inserted\":" + std::to_string(r1.rows_affected + r2.rows_affected) + "}\n");
        co_return;
    };
}

auto make_get_users(usub::pg::PgPool& pool)
{
    return [&pool](auto&, auto& resp) -> Awaitable<void>
    {
        auto rows = co_await pool.query_reflect<UserRow>(
            "SELECT id, name AS username, password, roles, tags FROM users_reflect ORDER BY id LIMIT 100;");
        std::string body = "[";
        for (size_t i = 0; i < rows.size(); ++i)
        {
            const auto& r = rows[i];
            body += "{\"id\":" + std::to_string(r.id) + ",\"name\":\"" + jescape(r.username) + "\",\"roles\":[";
            for (size_t k = 0; k < r.roles.size(); ++k)
            {
                body += std::to_string(r.roles[k]);
                if (k + 1 < r.roles.size()) body += ',';
            }
            body += "],\"tags\":[";
            for (size_t k = 0; k < r.tags.size(); ++k)
            {
                body += "\"" + jescape(r.tags[k]) + "\"";
                if (k + 1 < r.tags.size()) body += ',';
            }
            body += "]}";
            if (i + 1 < rows.size()) body += ",";
        }
        body += "]";
        resp.setStatus(200).addHeader("Content-Type", "application/json").setBody(body);
        co_return;
    };
}

auto make_post_tx_demo(usub::pg::PgPool& pool)
{
    return [&pool](auto&, auto& resp) -> Awaitable<void>
    {
        usub::pg::PgTransaction tx(&pool);
        if (!(co_await tx.begin()))
        {
            resp.setStatus(500).setBody("begin failed\n");
            co_return;
        }

        NewUser nu{.name = "Kirill", .password = std::nullopt, .roles = {1, 2, 5}, .tags = {"cpp", "uvent", "reflect"}};
        auto ins = co_await tx.query_reflect("INSERT INTO users_r(name,password,roles,tags) VALUES($1,$2,$3,$4)", nu);
        if (!ins.ok)
        {
            co_await tx.rollback();
            resp.setStatus(500).setBody(ins.error + "\n");
            co_return;
        }

        auto sub = tx.make_subtx();
        if (co_await sub.begin())
        {
            Upd u{.roles = {9, 9, 9}, .id = 1};
            (void)co_await sub.query_reflect("UPDATE users_r SET roles=$1 WHERE id=$2", u);
            co_await sub.rollback();
        }

        auto rows = co_await tx.query_reflect<UserRow>(
            "SELECT id, name AS username, password, roles, tags FROM users_r ORDER BY id DESC LIMIT 5");
        if (!(co_await tx.commit()))
        {
            resp.setStatus(500).setBody("commit failed\n");
            co_return;
        }

        std::string body = "[";
        for (size_t i = 0; i < rows.size(); ++i)
        {
            body += "{\"id\":" + std::to_string(rows[i].id) + ",\"name\":\"" + jescape(rows[i].username) + "\"}";
            if (i + 1 < rows.size()) body += ",";
        }
        body += "]";
        resp.setStatus(200).addHeader("Content-Type", "application/json").setBody(body);
        co_return;
    };
}

auto make_post_arrays_demo(usub::pg::PgPool& pool)
{
    return [&pool](auto&, auto& resp) -> Awaitable<void>
    {
        std::vector<std::string> arr = {"test", "array"};
        auto r1 = co_await pool.query_awaitable(
            "INSERT INTO array_test(test_array,comment) VALUES($1,$2);", arr, "comment");
        if (!r1.ok)
        {
            resp.setStatus(500).setBody("array_test: " + r1.error + "\n");
            co_return;
        }

        std::array<int, 3> ai{1, 2, 3};
        int ci[3]{4, 5, 6};
        std::list<double> ld{1.25, 2.5};
        std::vector<std::optional<bool>> vb{true, std::nullopt, false};
        std::initializer_list<const char*> il = {"x", "y"};
        auto r2 = co_await pool.query_awaitable(R"SQL(
            INSERT INTO array_test_multi(a_int4_1,a_int4_2,a_float8,a_bool,a_text,comment)
            VALUES($1,$2,$3,$4,$5,$6);
        )SQL", ai, ci, ld, vb, il, "multi-insert");
        if (!r2.ok)
        {
            resp.setStatus(500).setBody("array_test_multi: " + r2.error + "\n");
            co_return;
        }

        resp.setStatus(200).addHeader("Content-Type", "application/json").setBody("{\"ok\":true}\n");
        co_return;
    };
}

auto make_post_bigdata_seed(usub::pg::PgPool& pool)
{
    return [&pool](auto&, auto& resp) -> Awaitable<void>
    {
        auto acquired = co_await pool.acquire_connection();
        if (!acquired)
        {
            resp.setStatus(500).setBody("no conn\n");
            co_return;
        }
        auto conn = *acquired;
        if (!conn || !conn->connected())
        {
            resp.setStatus(500).setBody("no conn\n");
            co_return;
        }
        auto st = co_await conn->copy_in_start("COPY public.bigdata(payload) FROM STDIN");
        if (!st.ok)
        {
            co_await pool.release_connection_async(conn);
            resp.setStatus(500).setBody(st.error + "\n");
            co_return;
        }
        for (int i = 0; i < 100; i++)
        {
            std::string line = "payload line " + std::to_string(i) + "\n";
            auto c = co_await conn->copy_in_send_chunk(line.data(), line.size());
            if (!c.ok)
            {
                co_await pool.release_connection_async(conn);
                resp.setStatus(500).setBody(c.error + "\n");
                co_return;
            }
        }
        auto fin = co_await conn->copy_in_finish();
        co_await pool.release_connection_async(conn);
        if (!fin.ok)
        {
            resp.setStatus(500).setBody(fin.error + "\n");
            co_return;
        }
        resp.setStatus(200).addHeader("Content-Type", "application/json")
            .setBody("{\"rows_affected\":" + std::to_string(fin.rows_affected) + "}\n");
        co_return;
    };
}

auto make_get_bigdata_dump(usub::pg::PgPool& pool)
{
    return [&pool](auto&, auto& resp) -> Awaitable<void>
    {
        auto acquired = co_await pool.acquire_connection();
        if (!acquired)
        {
            resp.setStatus(500).setBody("no conn\n");
            co_return;
        }
        auto conn = *acquired;
        if (!conn || !conn->connected())
        {
            resp.setStatus(500).setBody("no conn\n");
            co_return;
        }
        auto st = co_await conn->copy_out_start(
            "COPY (SELECT id, payload FROM public.bigdata ORDER BY id LIMIT 50) TO STDOUT");
        if (!st.ok)
        {
            co_await pool.release_connection_async(conn);
            resp.setStatus(500).setBody(st.error + "\n");
            co_return;
        }
        std::string out;
        out.reserve(4096);
        while (true)
        {
            auto ch = co_await conn->copy_out_read_chunk();
            if (!ch.ok)
            {
                out += "\n-- err: " + ch.err.message + "\n";
                break;
            }
            if (ch.value.empty()) break;
            out.append(ch.value.begin(), ch.value.end());
        }
        co_await pool.release_connection_async(conn);
        resp.setStatus(200).addHeader("Content-Type", "text/plain").setBody(out);
        co_return;
    };
}

auto make_get_bigdata_cursor(usub::pg::PgPool& pool)
{
    return [&pool](auto&, auto& resp) -> Awaitable<void>
    {
        auto acquired = co_await pool.acquire_connection();
        if (!acquired)
        {
            resp.setStatus(500).setBody("no conn\n");
            co_return;
        }
        auto conn = *acquired;
        if (!conn || !conn->connected())
        {
            resp.setStatus(500).setBody("no conn\n");
            co_return;
        }
        std::string cname = conn->make_cursor_name();
        auto decl = co_await conn->cursor_declare(cname, "SELECT id, payload FROM public.bigdata ORDER BY id");
        if (!decl.ok)
        {
            co_await pool.release_connection_async(conn);
            resp.setStatus(500).setBody(decl.error + "\n");
            co_return;
        }
        std::string out;
        while (true)
        {
            auto ck = co_await conn->cursor_fetch_chunk(cname, 10);
            if (!ck.ok)
            {
                out += "{\"error\":\"" + ck.error + "\"}\n";
                break;
            }
            if (ck.rows.empty()) break;
            for (auto& r : ck.rows)
            {
                if (r.cols.size() >= 2)
                    out += "{\"id\":" + std::string(r.cols[0]) + ",\"payload\":\"" + jescape(r.cols[1]) + "\"}\n";
            }
            if (ck.done) break;
        }
        (void)co_await conn->cursor_close(cname);
        co_await pool.release_connection_async(conn);
        resp.setStatus(200).addHeader("Content-Type", "application/x-ndjson").setBody(out);
        co_return;
    };
}

// ---------------- Notifications (no globals) ----------------
struct BalanceLogger : usub::pg::IPgNotifyHandler
{
    Awaitable<void> operator()(std::string ch, std::string payload, int pid) override
    {
        std::cout << "[BALANCE] ch=" << ch << " pid=" << pid << " payload=" << payload << "\n";
        co_return;
    }
};

struct RiskAlerter : usub::pg::IPgNotifyHandler
{
    Awaitable<void> operator()(std::string ch, std::string payload, int pid) override
    {
        std::cout << "[RISK] ch=" << ch << " pid=" << pid << " payload=" << payload << "\n";
        co_return;
    }
};

Awaitable<void> run_notifications(usub::pg::PgPool& pool)
{
    auto acquired = co_await pool.acquire_connection();
    if (!acquired) co_return;
    auto conn = *acquired;
    if (!conn || !conn->connected()) co_return;

    usub::pg::PgNotificationMultiplexer mux(
        conn, pool.host(), pool.port(), pool.user(), pool.db(), pool.password(), {512});

    auto h1 = co_await mux.add_handler("balances.updated", std::make_shared<BalanceLogger>());
    auto h2 = co_await mux.add_handler("risk.test", std::make_shared<RiskAlerter>());
    if (!h1 || !h2)
    {
        co_await pool.release_connection_async(conn);
        std::cout << "[notify] subscribe failed\n";
        co_return;
    }

    co_await mux.run();

    co_await pool.release_connection_async(conn);
    co_return;
}

// ---------------- main ----------------
int main()
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, nullptr);
    
    usub::pg::PgPool pool(
        "localhost", // host
        "12432", // port
        "postgres", // user
        "postgres", // db
        "password", // password
        /*max_pool_size*/ 32
    );

    usub::Uvent uvent(1);

    uvent.for_each_thread([&](int idx, usub::uvent::thread::ThreadLocalStorage*)
    {
        usub::uvent::system::co_spawn_static(ensure_schema(pool), idx);
    });

    usub::uvent::system::co_spawn(run_notifications(pool));

    usub::unet::http::ServerRadix srv{uvent};

    srv.handle("GET", "/hello",
               [](auto&, auto& resp)-> Awaitable<void>
               {
                   resp.setStatus(200).addHeader("Content-Type", "text/plain").
                        setBody("Hello World\n");
                   co_return;
               });

    srv.handle("GET", "/user/{id}", make_get_user_handler(pool));
    srv.handle("POST", "/users/seed", make_post_users_seed(pool));
    srv.handle("GET", "/users", make_get_users(pool));
    srv.handle("POST", "/tx/demo", make_post_tx_demo(pool));
    srv.handle("POST", "/arrays/demo", make_post_arrays_demo(pool));
    srv.handle("POST", "/bigdata/seed", make_post_bigdata_seed(pool));
    srv.handle("GET", "/bigdata/dump", make_get_bigdata_dump(pool));
    srv.handle("GET", "/bigdata/cursor", make_get_bigdata_cursor(pool));

    uvent.run();
    return 0;
}
