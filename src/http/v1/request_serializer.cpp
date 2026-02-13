#include "unet/http/v1/request_serializer.hpp"

namespace usub::unet::http::v1 {

    namespace {

        inline std::string_view version_to_sv(VERSION v) {
            switch (v) {
                case VERSION::HTTP_1_1:
                    return "HTTP/1.1";
                case VERSION::HTTP_1_0:
                    return "HTTP/1.0";
                default:
                    return "HTTP/1.1";//TODO: proper version hanlding, defaulting to 1.1 is bingle bongle, dingle dangle, yickety doo, yickety da, ping pong, lippy-tappy-too-tah
            }
        }

        inline bool method_no_body(std::string_view m) {
            return m == "GET" || m == "HEAD" || m == "OPTIONS" || m == "TRACE";
        }

        inline void append_origin_target(std::string &out, const decltype(Request::metadata.uri) &uri) {
            if (!uri.path.empty()) {
                out.append(uri.path);
            } else {
                out.push_back('/');
            }
            if (!uri.query.empty()) {
                out.push_back('?');
                out.append(uri.query);
            }
        }

    }// namespace

    RequestSerializer::SerializerContext &RequestSerializer::getContext() { return context_; }

    std::string RequestSerializer::serialize(const Request &request) {
        std::string rv;
        rv.reserve(16 * 1024);

        rv.append(request.metadata.method_token);
        rv.append(" ");

        append_origin_target(rv, request.metadata.uri);

        rv.append(" ");
        rv.append(version_to_sv(request.metadata.version));
        rv.append("\r\n");

        for (const auto &h: request.headers.all()) {
            rv.append(h.key);
            rv.append(": ");
            rv.append(h.value);
            rv.append("\r\n");
        }

        rv.append("\r\n");


        if (!method_no_body(request.metadata.method_token)) { rv.append(request.body); }

        return rv;
    }

}// namespace usub::unet::http::v1
