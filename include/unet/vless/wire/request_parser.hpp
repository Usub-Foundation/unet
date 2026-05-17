#pragma once

#include <cstdint>
#include <cstddef>

namespace usub::unet::vless
{
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
            ADDRESS_OTHER, // For extencibility for future, may be worth having this
            BODY, // FINAL state, it never goes away from here, no resetting etc.
        };

        struct ParserContext {
            STATE state{STATE::PROTOCOL_VERSION};
            std::size_t bytes_read{}; // per state
        };
    };
} // namespace usub::unet::vless
