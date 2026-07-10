#include "unet/http/v2/wire/response_serializer.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <span>
#include <vector>

#include "unet/http/v2/wire/flags.hpp"
#include "unet/http/v2/wire/frame_serializer.hpp"


namespace usub::unet::http::v2 {

    namespace {
        bool isForbiddenH2Header(std::string_view name) noexcept {
            return name == "connection" || name == "keep-alive" || name == "proxy-connection" ||
                   name == "transfer-encoding" || name == "upgrade";
        }

        std::string toLower(std::string_view s) {
            std::string out;
            out.resize(s.size());
            std::transform(s.begin(), s.end(), out.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return out;
        }

        // Emit a header block as HEADERS (+ CONTINUATION) fragments. The first
        void emitHeaderBlock(std::string &out, std::uint32_t stream_id, std::string_view header_block,
                             std::uint32_t max_frame_size, bool end_stream, bool as_trailers) {
            const std::size_t total = header_block.size();
            std::size_t off = 0;
            bool first = true;
            do {
                const std::size_t take = std::min<std::size_t>(max_frame_size, total - off);
                const bool last_frag = (off + take) == total;
                const std::span<const std::byte> frag{reinterpret_cast<const std::byte *>(header_block.data() + off),
                                                      take};
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
                // Allow emitting an empty header block as a single empty HEADERS frame.
                if (total == 0) break;
            } while (off < total);
            (void) as_trailers;
        }
    }// namespace


    std::string ResponseSerializer::serialize(std::uint32_t stream_id, const usub::unet::http::ResponseWriter &res,
                                              HpackEncoder &encoder, std::uint32_t max_frame_size, bool end_stream) {
        // Build the initial field list. :status first, then everything else with
        // forbidden HTTP/1 connection fields stripped.
        std::vector<HeaderField> initial_fields;
        initial_fields.reserve(res.headers.size() + 1);
        initial_fields.push_back(
                HeaderField{std::string{":status"}, std::to_string(res.metadata.status_code), Indexing::Incremental});
        for (const auto &h: res.headers) {
            if (isForbiddenH2Header(h.key)) continue;
            initial_fields.push_back(HeaderField{toLower(h.key), h.value, Indexing::Incremental});
        }
        const std::string header_block = encoder.encode(initial_fields, /*use_huffman=*/true);

        // HEADERS (+ CONTINUATION). With trailers present
        const bool has_trailers = res.trailers.size() != 0;

        std::string out;
        emitHeaderBlock(out, stream_id, header_block, max_frame_size,
                        /*end_stream=*/end_stream && !has_trailers,
                        /*as_trailers=*/false);

        if (has_trailers) {
            std::vector<HeaderField> trailer_fields;
            trailer_fields.reserve(res.trailers.size());
            for (const auto &h: res.trailers) {
                if (isForbiddenH2Header(h.key)) continue;
                trailer_fields.push_back(HeaderField{toLower(h.key), h.value, Indexing::Incremental});
            }
            const std::string trailer_block = encoder.encode(trailer_fields, /*use_huffman=*/true);
            emitHeaderBlock(out, stream_id, trailer_block, max_frame_size,
                            /*end_stream=*/true, /*as_trailers=*/true);
        }

        return out;
    }

}// namespace usub::unet::http::v2
