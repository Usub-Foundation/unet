#pragma once

#include <cstdint>
#include <cstddef>

namespace usub::unet::vless
{
    class ResponseParser {
        public: 
        enum class STATE : std::uint8_t {
            PROTOCOL_VERSION,
            ADDONS_LENGTH,
            ADDONS,
            BODY, // FINAL state, it never goes away from here, no resetting etc.
        };

        struct ParserContext {
            STATE state{STATE::PROTOCOL_VERSION};
            std::size_t bytes_read{}; // per state
        };
    };
} // namespace usub::unet::vless
