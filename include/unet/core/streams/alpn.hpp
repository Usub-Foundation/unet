#pragma once

#include <array>
#include <climits>
#include <cstddef>

namespace usub::unet::core::stream {
    // TODO: Maybe in c++26 string_view will be possible
    template<std::size_t N>
    struct FixedString {
        std::array<char, N> value{};

        constexpr FixedString(const char (&str)[N]) {
            for (std::size_t i = 0; i < N; ++i) {
                this->value[i] = str[i];
            }
        }

        [[nodiscard]] constexpr std::size_t size() const { return N - 1; }
        [[nodiscard]] constexpr const char *data() const { return this->value.data(); }
    };

    template<std::size_t N>
    FixedString(const char (&)[N]) -> FixedString<N>;

    template<std::size_t Size, std::size_t N>
    constexpr void appendAlpnProtocol(std::array<unsigned char, Size> &encoded, std::size_t &offset,
                                      const FixedString<N> &protocol) {
        static_assert(N > 1, "ALPN must not be empty");
        static_assert((N - 1) <= static_cast<std::size_t>(UCHAR_MAX), "ALPN must fit in one length byte");
        encoded[offset++] = static_cast<unsigned char>(N - 1);
        for (std::size_t i = 0; i < (N - 1); ++i) {
            encoded[offset++] = static_cast<unsigned char>(protocol.value[i]);
        }
    }

    template<FixedString... Protocols>
    struct AlpnWireFormatTraits;

    template<>
    struct AlpnWireFormatTraits<> {
        static constexpr auto kDefaultProtocol = FixedString{"http/1.1"};
        static constexpr std::size_t kWireSize = kDefaultProtocol.size() + 1;

        static constexpr std::array<unsigned char, kWireSize> kWireFormat = []() {
            std::array<unsigned char, kWireSize> encoded{};
            std::size_t offset = 0;
            appendAlpnProtocol(encoded, offset, kDefaultProtocol);
            return encoded;
        }();
    };

    template<FixedString First, FixedString... Rest>
    struct AlpnWireFormatTraits<First, Rest...> {
        static constexpr std::size_t kWireSize = (First.size() + 1) + (0 + ... + (Rest.size() + 1));

        static constexpr std::array<unsigned char, kWireSize> kWireFormat = []() {
            std::array<unsigned char, kWireSize> encoded{};
            std::size_t offset = 0;
            appendAlpnProtocol(encoded, offset, First);
            (appendAlpnProtocol(encoded, offset, Rest), ...);
            return encoded;
        }();
    };
}// namespace usub::unet::core::stream
