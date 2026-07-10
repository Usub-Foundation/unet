#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>


namespace usub::unet::http::v2 {

    enum class Indexing : std::uint8_t { Incremental, None, Never };

    struct HeaderField {
        std::string name;
        std::string value;
        Indexing indexing = Indexing::Incremental;
    };

    enum class HpackErrorCode : std::uint8_t {
        OK,
        BUFFER_UNDERFLOW,
        INVALID_INTEGER,
        INVALID_INDEX,
        INVALID_HUFFMAN,
        INVALID_STRING,
        INVALID_TABLE_SIZE_UPDATE,
    };

    struct HpackError {
        HpackErrorCode code{};
        std::string message;
    };

    class HpackEncoder {
    public:
        explicit HpackEncoder(std::size_t max_table_size = 4096);
        void setMaxDynamicTableSize(std::size_t size);
        std::string encode(const std::vector<HeaderField> &headers, bool use_huffman = true);

    private:
        std::size_t max_table_size_;
        std::size_t current_table_size_;
        bool pending_table_size_update_;
        std::vector<HeaderField> dynamic_table_;
    };

    class HpackDecoder {
    public:
        explicit HpackDecoder(std::size_t max_table_size = 4096);
        void setMaxDynamicTableSize(std::size_t size);
        std::expected<std::vector<HeaderField>, HpackError> decode(std::string_view block);

    private:
        // settings_max_table_size_: the ceiling advertised by the peer via
        // SETTINGS_HEADER_TABLE_SIZE. Dynamic table size updates in a header block
        // must not exceed this.
        // max_table_size_: the peer's currently-in-use table size (set by
        // HPACK dynamic-table-size-update instructions within a header block).
        std::size_t settings_max_table_size_;
        std::size_t max_table_size_;
        std::size_t current_table_size_;
        std::vector<HeaderField> dynamic_table_;
    };

}// namespace usub::unet::http::v2
