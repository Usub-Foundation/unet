#pragma once
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace usub::unet::http::v2::hpack {

    enum class Indexing : std::uint8_t { Incremental, None, Never };

    struct HeaderField {
        std::string name;
        std::string value;
        Indexing indexing = Indexing::Incremental;
    };

    enum class ErrorCode : std::uint8_t {
        OK,
        BUFFER_UNDERFLOW,
        INVALID_INTEGER,
        INVALID_INDEX,
        INVALID_HUFFMAN,
        INVALID_STRING,
        INVALID_TABLE_SIZE_UPDATE,
    };

    struct Error {
        ErrorCode code{};
        std::string message;
    };

    class Encoder {
    public:
        explicit Encoder(std::size_t max_table_size = 4096);
        void set_max_dynamic_table_size(std::size_t size);
        std::string encode(const std::vector<HeaderField> &headers, bool use_huffman = true);

    private:
        std::size_t max_table_size_;
        std::size_t current_table_size_;
        bool pending_table_size_update_;
        std::vector<HeaderField> dynamic_table_;
    };

    class Decoder {
    public:
        explicit Decoder(std::size_t max_table_size = 4096);
        void set_max_dynamic_table_size(std::size_t size);
        std::expected<std::vector<HeaderField>, Error> decode(std::string_view block);

    private:
        std::size_t max_table_size_;
        std::size_t current_table_size_;
        std::vector<HeaderField> dynamic_table_;
    };

}// namespace usub::unet::http::v2::hpack
