#include "unet/http/router/radix.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>

#include "unet/http/error_defaults.hpp"

namespace usub::unet::http::router {

    namespace {
        bool isParamNameChar(char c) noexcept {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '_';
        }
    }// namespace

    std::vector<Radix::PatternToken>
    Radix::tokenize(const std::string &pattern, std::vector<std::string> &param_names,
                    const std::unordered_map<std::string_view, const param_constraint *> &constraints) const {
        std::vector<PatternToken> tokens;
        std::string               literal_buf;

        auto flush_literal = [&]() {
            if (!literal_buf.empty()) {
                PatternToken t;
                t.kind = PatternToken::Kind::Literal;
                t.text = std::move(literal_buf);
                literal_buf.clear();
                tokens.push_back(std::move(t));
            }
        };

        std::size_t i = 0;
        while (i < pattern.size()) {
            const char c = pattern[i];

            if (c == '\\' && i + 1 < pattern.size()) {
                literal_buf.push_back(pattern[i + 1]);
                i += 2;
                continue;
            }

            if (c == '{') {
                flush_literal();
                std::size_t close = pattern.find('}', i + 1);
                if (close == std::string::npos)
                    throw std::runtime_error("Unmatched '{' in pattern: " + pattern);

                std::string body = pattern.substr(i + 1, close - i - 1);
                std::size_t colon = body.find(':');
                std::string name  = body.substr(0, colon);
                std::string rx    = (colon == std::string::npos) ? std::string{} : body.substr(colon + 1);

                if (name.empty())
                    throw std::runtime_error("Empty parameter name in: " + pattern);
                for (char nc : name) {
                    if (!isParamNameChar(nc))
                        throw std::runtime_error("Invalid character in parameter name '" + name +
                                                 "' in pattern: " + pattern);
                }

                PatternToken t;
                t.kind = PatternToken::Kind::Param;
                t.text = name;

                if (!rx.empty()) {
                    t.regex      = rx;
                    t.constraint = param_constraint{rx, "Encountered an error..."};
                } else if (auto it = constraints.find(name); it != constraints.end() && it->second) {
                    t.regex      = it->second->pattern;
                    t.constraint = *it->second;
                } else {
                    t.regex      = default_constraint.pattern;
                    t.constraint = default_constraint;
                }

                param_names.push_back(name);
                tokens.push_back(std::move(t));
                i = close + 1;
                continue;
            }

            if (c == '*') {
                // Named wildcard: '*' followed by [A-Za-z0-9_]+, must terminate pattern.
                std::size_t j = i + 1;
                while (j < pattern.size() && isParamNameChar(pattern[j])) ++j;
                if (j == i + 1) {
                    // bare '*' — treat as a literal byte 
                    literal_buf.push_back('*');
                    ++i;
                    continue;
                }
                if (j != pattern.size())
                    throw std::runtime_error("Wildcard '*" + pattern.substr(i + 1, j - i - 1) +
                                              "' must be the last token in pattern: " + pattern);

                flush_literal();
                PatternToken t;
                t.kind = PatternToken::Kind::Wildcard;
                t.text = pattern.substr(i + 1, j - i - 1);
                param_names.push_back(t.text);
                tokens.push_back(std::move(t));
                i = j;
                continue;
            }

            literal_buf.push_back(c);
            ++i;
        }
        flush_literal();
        return tokens;
    }


    void Radix::insert(RadixNode *node, const std::vector<PatternToken> &tokens, std::size_t idx,
                       std::string_view literal_remaining,
                       std::unique_ptr<RouteType> &route, bool has_trailing_slash) {
        while (literal_remaining.empty() && idx < tokens.size() &&
               tokens[idx].kind == PatternToken::Kind::Literal) {
            literal_remaining = std::string_view{tokens[idx].text};
            ++idx;
        }

        if (literal_remaining.empty()) {
            if (idx == tokens.size()) {
                if (!node->route) node->route = std::move(route);
                node->trailing_slash = has_trailing_slash;
                return;
            }

            const PatternToken &tok = tokens[idx];

            if (tok.kind == PatternToken::Kind::Param) {
                if (!node->param_edge) {
                    auto edge   = std::make_unique<ParamEdge>();
                    edge->name  = tok.text;
                    edge->regex = std::regex(tok.regex);
                    if (tok.constraint) edge->constraint = *tok.constraint;
                    edge->child = std::make_unique<RadixNode>();
                    node->param_edge = std::move(edge);
                }
                // Conflict reporting will be added later 
                insert(node->param_edge->child.get(), tokens, idx + 1, {}, route, has_trailing_slash);
                return;
            }

            // Wildcard — terminal.
            if (!node->wildcard_edge) {
                auto edge   = std::make_unique<WildcardEdge>();
                edge->name  = tok.text;
                edge->child = std::make_unique<RadixNode>();
                node->wildcard_edge = std::move(edge);
            }
            auto *child           = node->wildcard_edge->child.get();
            if (!child->route) child->route = std::move(route);
            child->trailing_slash = has_trailing_slash;
            return;
        }

        auto it = std::lower_bound(
                node->literal_edges.begin(), node->literal_edges.end(), literal_remaining[0],
                [](const std::pair<std::string, std::unique_ptr<RadixNode>> &edge, char c) {
                    return static_cast<unsigned char>(edge.first[0]) < static_cast<unsigned char>(c);
                });

        if (it != node->literal_edges.end() && !it->first.empty() && it->first[0] == literal_remaining[0]) {
            const std::string &label = it->first;
            // Compute common-prefix length.
            std::size_t common = 0;
            const std::size_t lim = std::min(label.size(), literal_remaining.size());
            while (common < lim && label[common] == literal_remaining[common]) ++common;

            if (common == label.size()) {
                insert(it->second.get(), tokens, idx, literal_remaining.substr(common), route, has_trailing_slash);
                return;
            }

            auto mid                = std::make_unique<RadixNode>();
            std::string tail_label  = label.substr(common);
            auto old_child          = std::move(it->second);
            mid->literal_edges.emplace_back(std::move(tail_label), std::move(old_child));

            it->first  = label.substr(0, common);
            it->second = std::move(mid);

            insert(it->second.get(), tokens, idx, literal_remaining.substr(common), route, has_trailing_slash);
            return;
        }

        auto new_child = std::make_unique<RadixNode>();
        auto *raw      = new_child.get();
        node->literal_edges.insert(it, {std::string{literal_remaining}, std::move(new_child)});
        insert(raw, tokens, idx, {}, route, has_trailing_slash);
    }


    // ---- match --------------------------------------------------------------

    bool Radix::matchAt(RadixNode *node, std::string_view path, std::string_view cursor,
                        const RequestReader &request, MatchResult &out, std::string *last_error) const {
        if (cursor.empty()) {
            if (node->route) {
                const bool req_has_trailing = !path.empty() && path.back() == '/';
                if (node->trailing_slash == req_has_trailing) {
                    out.route = node->route.get();
                    return true;
                }
            }
            return false;
        }

        // 1) Try literal edges first.
        auto it = std::lower_bound(
                node->literal_edges.begin(), node->literal_edges.end(), cursor[0],
                [](const std::pair<std::string, std::unique_ptr<RadixNode>> &edge, char c) {
                    return static_cast<unsigned char>(edge.first[0]) < static_cast<unsigned char>(c);
                });

        if (it != node->literal_edges.end() && !it->first.empty() && it->first[0] == cursor[0]) {
            const std::string &label = it->first;
            if (cursor.size() >= label.size() && cursor.substr(0, label.size()) == label) {
                if (matchAt(it->second.get(), path, cursor.substr(label.size()), request /*unused*/, out, last_error))
                    return true;
            }
        }

        // 2) Param edge — run regex greedy/anchored at cursor, then try shorter matches.
        if (node->param_edge) {
            ParamEdge &edge = *node->param_edge;

            std::cmatch m;
            // We force the match to anchor at the start by using regex_search
            // with std::regex_constants::match_continuous.
            if (edge.regex && std::regex_search(cursor.data(), cursor.data() + cursor.size(), m, *edge.regex,
                                                 std::regex_constants::match_continuous)) {
                // Try progressively shorter prefixes to enable backtracking when
                // the child literal/param needs to consume some of the captured span.
                std::size_t max_len = static_cast<std::size_t>(m.length(0));
                for (std::size_t len = max_len; len > 0; --len) {
                    std::string_view captured = cursor.substr(0, len);
                    // bind
                    auto prev = out.uri_params.find(std::string_view{edge.name});
                    std::optional<std::string_view> saved;
                    if (prev != out.uri_params.end()) saved = prev->second;
                    out.uri_params[std::string_view{edge.name}] = captured;

                    if (matchAt(edge.child.get(), path, cursor.substr(len), request, out, last_error))
                        return true;

                    // backtrack
                    if (saved) out.uri_params[std::string_view{edge.name}] = *saved;
                    else        out.uri_params.erase(std::string_view{edge.name});
                }
            } else if (last_error) {
                *last_error = edge.constraint ? edge.constraint->description
                                              : ("Invalid value for parameter: " + edge.name);
            }
        }

        // 3) Wildcard — terminal, grabs everything remaining.
        if (node->wildcard_edge) {
            WildcardEdge &edge = *node->wildcard_edge;
            out.uri_params[std::string_view{edge.name}] = cursor;
            if (edge.child->route) {
                const bool req_has_trailing = !path.empty() && path.back() == '/';
                if (edge.child->trailing_slash == req_has_trailing) {
                    out.route = edge.child->route.get();
                    return true;
                }
            }
            out.uri_params.erase(std::string_view{edge.name});
        }

        return false;
    }

    Radix::RouteType &Radix::addRoute(
            const std::set<std::string> &methods, const std::string &pattern,
            std::function<RadixRoute::HandlerFunctionType> handler,
            const std::unordered_map<std::string_view, const param_constraint *> &constraints) {

        std::vector<std::string>  param_names;
        std::vector<PatternToken> tokens = tokenize(pattern, param_names, constraints);

        const bool has_trailing_slash = !pattern.empty() && pattern.back() == '/' &&
                                         !(pattern.size() >= 2 && pattern[pattern.size() - 2] == '\\');

        auto routePtr =
                std::make_unique<RouteType>(methods, param_names, std::move(handler), methods.contains("*"));
        RouteType *rawPtr = routePtr.get();

        insert(root_.get(), tokens, 0, {}, routePtr, has_trailing_slash);

        // Tiny human-readable hint log (kept from the previous behaviour).
        std::ostringstream methods_stream;
        for (auto it = methods.begin(); it != methods.end(); ++it) {
            if (it != methods.begin()) methods_stream << ',';
            methods_stream << *it;
        }
        std::cout << "route methods: " << methods_stream.str() << "\n"
                  << "path: " << pattern << "\n"
                  << "hint: router.addHandler({\"" << methods_stream.str() << "\"}, \"" << pattern
                  << "\", handlerFunction);\n"
                  << std::endl;

        return *rawPtr;
    }

    Radix::RouteType &Radix::addRoute(
            std::string_view method, const std::string &pathPattern,
            std::function<RadixRoute::HandlerFunctionType> function,
            const std::unordered_map<std::string_view, const param_constraint *> &constraints) {
        std::set<std::string> method_set{std::string(method)};
        return this->addRoute(method_set, pathPattern, std::move(function), constraints);
    }

    std::expected<Radix::MatchResult, STATUS_CODE>
    Radix::match(const RequestReader &request, std::string *error_description) {
        std::string_view path = request.metadata.uri.path;
        MatchResult      m{};
        if (!matchAt(root_.get(), path, path, request, m, error_description) || !m.route) {
            return std::unexpected(STATUS_CODE::NOT_FOUND);
        }
        if (m.route->accept_all_methods ||
            m.route->allowed_method_tokenns.contains(request.metadata.method_token)) {
            return m;
        }
        return std::unexpected(STATUS_CODE::METHOD_NOT_ALLOWED);
    }

    usub::uvent::task::Awaitable<void> Radix::invoke(MatchResult &match, RequestReader &request, ResponseWriter &response) {
        if (!match.route) co_return;
        co_await match.route->handler(request, response, match);
        co_return;
    }

    usub::uvent::task::Awaitable<bool>
    Radix::runRouteMiddleware(MIDDLEWARE_PHASE phase, MatchResult &match, RequestReader &request, ResponseWriter &response) {
        if (!match.route) co_return false;
        co_return co_await match.route->middleware_chain.execute(phase, request, response);
    }

    MiddlewareChain &Radix::addMiddleware(MIDDLEWARE_PHASE phase, std::function<MiddlewareFunctionType> middleware) {
        if (phase == MIDDLEWARE_PHASE::HEADER) {
            this->middleware_chain_.emplace_back(phase, std::move(middleware));
        } else {
            std::cerr << "Non header global middlewares are not supported yet" << std::endl;
        }
        return this->middleware_chain_;
    }

    MiddlewareChain &Radix::getMiddlewareChain() { return this->middleware_chain_; }

    Radix &Radix::addErrorHandler(const std::string &level, std::function<ErrorFunctionType> error_handler_fn) {
        this->error_handlers_map.insert_or_assign(level, std::move(error_handler_fn));
        return *this;
    }

    usub::uvent::task::Awaitable<void>
    Radix::error(const std::string &level, RequestReader &request, ResponseWriter &response) {
        if (auto it = this->error_handlers_map.find(level); it != this->error_handlers_map.end()) {
            co_await it->second(request, response);
            co_return;
        }
        co_await defaultErrorResponse(request, response);
    }

    // ---- dump ---------------------------------------------------------------

    std::string Radix::dump() const {
        std::ostringstream buf;
        buf << "/\n";
        printNode(root_.get(), buf, "");
        return buf.str();
    }

    void Radix::printNode(const RadixNode *node, std::ostringstream &buf, const std::string &prefix) const {
        const std::size_t total = node->literal_edges.size() + (node->param_edge ? 1 : 0) +
                                   (node->wildcard_edge ? 1 : 0);
        std::size_t idx = 0;
        for (const auto &[label, child] : node->literal_edges) {
            const bool last = (++idx == total);
            buf << prefix << (last ? "└─" : "├─") << label << (child->route ? " [#]" : "") << "\n";
            printNode(child.get(), buf, prefix + (last ? "   " : "│  "));
        }
        if (node->param_edge) {
            const bool last = (++idx == total);
            std::string lbl = "{" + node->param_edge->name +
                              (node->param_edge->constraint ? ":" + node->param_edge->constraint->pattern : "") + "}";
            buf << prefix << (last ? "└─" : "├─") << lbl
                << (node->param_edge->child->route ? " [#]" : "") << "\n";
            printNode(node->param_edge->child.get(), buf, prefix + (last ? "   " : "│  "));
        }
        if (node->wildcard_edge) {
            const bool last = (++idx == total);
            buf << prefix << (last ? "└─" : "├─") << "*" << node->wildcard_edge->name
                << (node->wildcard_edge->child->route ? " [#]" : "") << "\n";
            printNode(node->wildcard_edge->child.get(), buf, prefix + (last ? "   " : "│  "));
        }
    }

}// namespace usub::unet::http::router
