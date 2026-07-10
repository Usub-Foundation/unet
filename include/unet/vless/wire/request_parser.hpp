#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string_view>

#include "unet/vless/core/request.hpp"
#include "unet/vless/error.hpp"

namespace usub::unet::vless {
    class RequestParser {
    public:
        enum class STATE : std::uint8_t {
            PROTOCOL_VERSION,
            UUID,
            ADDONS_LENGTH,
            ADDONS,
            COMMAND,
            DESTINATION_PORT,
            ADDRESS_TYPE,
            ADDRESS_IP4,
            ADDRESS_DOMAIN_LENGTH,
            ADDRESS_DOMAIN,
            ADDRESS_IP6,
            ADDRESS_OTHER,// For extencibility for future, may be worth having this
            COMPLETE,     // FINAL state, next is body it never goes away from here, no resetting etc.
            FAILED,
        };

        struct ParserContext {
            STATE state{STATE::PROTOCOL_VERSION};
            std::size_t bytes_read{};// per state
        };

        [[nodiscard]] ParserContext &getContext() noexcept { return context_; }
        [[nodiscard]] const ParserContext &getContext() const noexcept { return context_; }

        std::expected<void, ParseError> step(Request &request, std::string_view::const_iterator &begin,
                                             const std::string_view::const_iterator end);

    private:
        ParserContext context_;
    };
}// namespace usub::unet::vless
