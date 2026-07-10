#include "unet/http/v2/wire/request_serializer.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "unet/http/v2/wire/flags.hpp"
#include "unet/http/v2/wire/frame_serializer.hpp"


namespace usub::unet::http::v2 {

    namespace {
        bool isForbiddenH2Header(std::string_view name) noexcept {
            return name == "connection" || name == "keep-alive" ||
                   name == "proxy-connection" || name == "transfer-encoding" ||
                   name == "upgrade" || name == "host";
        }

        std::string toLower(std::string_view s) {
            std::string out;
            out.resize(s.size());
            std::transform(s.begin(), s.end(), out.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return out;
        }

        std::string buildAuthority(const usub::unet::http::RequestReader &req) {
            // Prefer the parsed URI authority; fall back to the explicit string.
            const auto &a = req.metadata.uri.authority;
            if (!a.host.empty()) {
                if (a.port == 0) return a.host;
                return a.host + ":" + std::to_string(a.port);
            }
            return req.metadata.authority;
        }

        std::string buildPath(const usub::unet::http::RequestReader &req) {
            std::string p = req.metadata.uri.path.empty() ? std::string{"/"} : req.metadata.uri.path;
            if (!req.metadata.uri.query.empty()) {
                p += '?';
                p += req.metadata.uri.query;
            }
            return p;
        }

        void emitHeaderBlock(std::string &out, std::uint32_t stream_id,
                              std::string_view header_block, std::uint32_t max_frame_size,
                              bool end_stream) {
            const std::size_t total = header_block.size();
            std::size_t off   = 0;
            bool        first = true;
            do {
                const std::size_t take      = std::min<std::size_t>(max_frame_size, total - off);
                const bool        last_frag = (off + take) == total;
                const std::span<const std::byte> frag{
                        reinterpret_cast<const std::byte *>(header_block.data() + off), take};
                std::uint8_t flags = 0;
                if (last_frag) flags |= FLAGS::END_HEADERS;
                if (last_frag && end_stream) flags |= FLAGS::END_STREAM;
                if (first) {
                    out += FrameSerializer::serializeHeaders(stream_id, frag, flags);
                    first = false;
                } else {
                    out += FrameSerializer::serializeContinuation(stream_id, frag, flags);
                }
                off += take;
                if (total == 0) break;
            } while (off < total);
        }
    }// namespace


    std::string RequestSerializer::serialize(std::uint32_t stream_id,
                                                const usub::unet::http::RequestReader &req,
                                                HpackEncoder &encoder,
                                                std::uint32_t max_frame_size,
                                                bool          end_stream) {
        const bool is_connect = req.metadata.method_token == "CONNECT";

        std::vector<HeaderField> fields;
        fields.reserve(req.headers.size() + 4);

        fields.push_back(HeaderField{std::string{":method"}, req.metadata.method_token, Indexing::Incremental});

        if (!is_connect) {
            const std::string scheme = req.metadata.uri.scheme.empty()
                                              ? std::string{"http"}
                                              : req.metadata.uri.scheme;
            fields.push_back(HeaderField{std::string{":scheme"}, scheme, Indexing::Incremental});
            fields.push_back(HeaderField{std::string{":path"}, buildPath(req), Indexing::Incremental});
        }

        const std::string authority = buildAuthority(req);
        if (!authority.empty()) {
            fields.push_back(HeaderField{std::string{":authority"}, authority, Indexing::Incremental});
        }

        for (const auto &h : req.headers) {
            if (isForbiddenH2Header(h.key)) continue;
            fields.push_back(HeaderField{toLower(h.key), h.value, Indexing::Incremental});
        }

        const std::string header_block = encoder.encode(fields, /*use_huffman=*/true);

        // Body bytes are NOT emitted here. RequestReader::body is a channel
        // drained by the user; the client session is responsible for streaming
        // DATA frames as bytes flow through.
        std::string out;
        emitHeaderBlock(out, stream_id, header_block, max_frame_size, end_stream);
        return out;
    }

}// namespace usub::unet::http::v2
