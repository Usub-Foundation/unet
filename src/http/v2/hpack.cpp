#include "unet/http/v2/hpack.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <optional>

namespace usub::unet::http::v2::hpack {
    namespace {
        struct StaticEntry {
            std::string_view name;
            std::string_view value;
        };

        constexpr std::array<StaticEntry, 61> kStaticTable = {{
                {":authority", ""},
                {":method", "GET"},
                {":method", "POST"},
                {":path", "/"},
                {":path", "/index.html"},
                {":scheme", "http"},
                {":scheme", "https"},
                {":status", "200"},
                {":status", "204"},
                {":status", "206"},
                {":status", "304"},
                {":status", "400"},
                {":status", "404"},
                {":status", "500"},
                {"accept-charset", ""},
                {"accept-encoding", "gzip, deflate"},
                {"accept-language", ""},
                {"accept-ranges", ""},
                {"accept", ""},
                {"access-control-allow-origin", ""},
                {"age", ""},
                {"allow", ""},
                {"authorization", ""},
                {"cache-control", ""},
                {"content-disposition", ""},
                {"content-encoding", ""},
                {"content-language", ""},
                {"content-length", ""},
                {"content-location", ""},
                {"content-range", ""},
                {"content-type", ""},
                {"cookie", ""},
                {"date", ""},
                {"etag", ""},
                {"expect", ""},
                {"expires", ""},
                {"from", ""},
                {"host", ""},
                {"if-match", ""},
                {"if-modified-since", ""},
                {"if-none-match", ""},
                {"if-range", ""},
                {"if-unmodified-since", ""},
                {"last-modified", ""},
                {"link", ""},
                {"location", ""},
                {"max-forwards", ""},
                {"proxy-authenticate", ""},
                {"proxy-authorization", ""},
                {"range", ""},
                {"referer", ""},
                {"refresh", ""},
                {"retry-after", ""},
                {"server", ""},
                {"set-cookie", ""},
                {"strict-transport-security", ""},
                {"transfer-encoding", ""},
                {"user-agent", ""},
                {"vary", ""},
                {"via", ""},
                {"www-authenticate", ""},
        }};

        constexpr std::array<std::uint32_t, 257> kHuffmanCodes = {{
                0x1ff8, 0x7fffd8, 0xfffffe2, 0xfffffe3, 0xfffffe4, 0xfffffe5, 0xfffffe6, 0xfffffe7,
                0xfffffe8, 0xffffea, 0x3ffffffc, 0xfffffe9, 0xfffffea, 0x3ffffffd, 0xfffffeb, 0xfffffec,
                0xfffffed, 0xfffffee, 0xfffffef, 0xffffff0, 0xffffff1, 0xffffff2, 0x3ffffffe, 0xffffff3,
                0xffffff4, 0xffffff5, 0xffffff6, 0xffffff7, 0xffffff8, 0xffffff9, 0xffffffa, 0xffffffb,
                0x14, 0x3f8, 0x3f9, 0xffa, 0x1ff9, 0x15, 0xf8, 0x7fa,
                0x3fa, 0x3fb, 0xf9, 0x7fb, 0xfa, 0x16, 0x17, 0x18,
                0x0, 0x1, 0x2, 0x19, 0x1a, 0x1b, 0x1c, 0x1d,
                0x1e, 0x1f, 0x5c, 0xfb, 0x7ffc, 0x20, 0xffb, 0x3fc,
                0x1ffa, 0x21, 0x5d, 0x5e, 0x5f, 0x60, 0x61, 0x62,
                0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a,
                0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x71, 0x72,
                0xfc, 0x73, 0xfd, 0x1ffb, 0x7fff0, 0x1ffc, 0x3ffc, 0x22,
                0x7ffd, 0x3, 0x23, 0x4, 0x24, 0x5, 0x25, 0x26,
                0x27, 0x6, 0x74, 0x75, 0x28, 0x29, 0x2a, 0x7,
                0x2b, 0x76, 0x2c, 0x8, 0x9, 0x2d, 0x77, 0x78,
                0x79, 0x7a, 0x7b, 0x7ffe, 0x7fc, 0x3ffd, 0x1ffd, 0xffffffc,
                0xfffe6, 0x3fffd2, 0xfffe7, 0xfffe8, 0x3fffd3, 0x3fffd4, 0x3fffd5, 0x7fffd9,
                0x3fffd6, 0x7fffda, 0x7fffdb, 0x7fffdc, 0x7fffdd, 0x7fffde, 0xffffeb, 0x7fffdf,
                0xffffec, 0xffffed, 0x3fffd7, 0x7fffe0, 0xffffee, 0x7fffe1, 0x7fffe2, 0x7fffe3,
                0x7fffe4, 0x1fffdc, 0x3fffd8, 0x7fffe5, 0x3fffd9, 0x7fffe6, 0x7fffe7, 0xffffef,
                0x3fffda, 0x1fffdd, 0xfffe9, 0x3fffdb, 0x3fffdc, 0x7fffe8, 0x7fffe9, 0x1fffde,
                0x7fffea, 0x3fffdd, 0x3fffde, 0xfffff0, 0x1fffdf, 0x3fffdf, 0x7fffeb, 0x7fffec,
                0x1fffe0, 0x1fffe1, 0x3fffe0, 0x1fffe2, 0x7fffed, 0x3fffe1, 0x7fffee, 0x7fffef,
                0xfffea, 0x3fffe2, 0x3fffe3, 0x3fffe4, 0x7ffff0, 0x3fffe5, 0x3fffe6, 0x7ffff1,
                0x3ffffe0, 0x3ffffe1, 0xfffeb, 0x7fff1, 0x3fffe7, 0x7ffff2, 0x3fffe8, 0x1ffffec,
                0x3ffffe2, 0x3ffffe3, 0x3ffffe4, 0x7ffffde, 0x7ffffdf, 0x3ffffe5, 0xfffff1, 0x1ffffed,
                0x7fff2, 0x1fffe3, 0x3ffffe6, 0x7ffffe0, 0x7ffffe1, 0x3ffffe7, 0x7ffffe2, 0xfffff2,
                0x1fffe4, 0x1fffe5, 0x3ffffe8, 0x3ffffe9, 0xffffffd, 0x7ffffe3, 0x7ffffe4, 0x7ffffe5,
                0xfffec, 0xfffff3, 0xfffed, 0x1fffe6, 0x3fffe9, 0x1fffe7, 0x1fffe8, 0x7ffff3,
                0x3fffea, 0x3fffeb, 0x1ffffee, 0x1ffffef, 0xfffff4, 0xfffff5, 0x3ffffea, 0x7ffff4,
                0x3ffffeb, 0x7ffffe6, 0x3ffffec, 0x3ffffed, 0x7ffffe7, 0x7ffffe8, 0x7ffffe9, 0x7ffffea,
                0x7ffffeb, 0xffffffe, 0x7ffffec, 0x7ffffed, 0x7ffffee, 0x7ffffef, 0x7fffff0, 0x3ffffee,
                0x3fffffff,
        }};

        constexpr std::array<std::uint8_t, 257> kHuffmanCodeLen = {{
                13, 23, 28, 28, 28, 28, 28, 28,
                28, 24, 30, 28, 28, 30, 28, 28,
                28, 28, 28, 28, 28, 28, 30, 28,
                28, 28, 28, 28, 28, 28, 28, 28,
                6, 10, 10, 12, 13, 6, 8, 11,
                10, 10, 8, 11, 8, 6, 6, 6,
                5, 5, 5, 6, 6, 6, 6, 6,
                6, 6, 7, 8, 15, 6, 12, 10,
                13, 6, 7, 7, 7, 7, 7, 7,
                7, 7, 7, 7, 7, 7, 7, 7,
                7, 7, 7, 7, 7, 7, 7, 7,
                8, 7, 8, 13, 19, 13, 14, 6,
                15, 5, 6, 5, 6, 5, 6, 6,
                6, 5, 7, 7, 6, 6, 6, 5,
                6, 7, 6, 5, 5, 6, 7, 7,
                7, 7, 7, 15, 11, 14, 13, 28,
                20, 22, 20, 20, 22, 22, 22, 23,
                22, 23, 23, 23, 23, 23, 24, 23,
                24, 24, 22, 23, 24, 23, 23, 23,
                23, 21, 22, 23, 22, 23, 23, 24,
                22, 21, 20, 22, 22, 23, 23, 21,
                23, 22, 22, 24, 21, 22, 23, 23,
                21, 21, 22, 21, 23, 22, 23, 23,
                20, 22, 22, 22, 23, 22, 22, 23,
                26, 26, 20, 19, 22, 23, 22, 25,
                26, 26, 26, 27, 27, 26, 24, 25,
                19, 21, 26, 27, 27, 26, 27, 24,
                21, 21, 26, 26, 28, 27, 27, 27,
                20, 24, 20, 21, 22, 21, 21, 23,
                22, 22, 25, 25, 24, 24, 26, 23,
                26, 27, 26, 26, 27, 27, 27, 27,
                27, 28, 27, 27, 27, 27, 27, 26,
                30,
        }};

        struct HuffmanNode {
            int sym = -1;
            int child[2] = {-1, -1};
        };

        const std::vector<HuffmanNode> &huffman_tree() {
            static std::vector<HuffmanNode> nodes;
            if (!nodes.empty()) { return nodes; }
            nodes.emplace_back();
            for (std::size_t sym = 0; sym < kHuffmanCodes.size(); ++sym) {
                std::uint32_t code = kHuffmanCodes[sym];
                std::uint8_t len = kHuffmanCodeLen[sym];
                int node = 0;
                for (int i = len - 1; i >= 0; --i) {
                    int bit = (code >> i) & 0x1;
                    if (nodes[node].child[bit] == -1) {
                        nodes[node].child[bit] = static_cast<int>(nodes.size());
                        nodes.emplace_back();
                    }
                    node = nodes[node].child[bit];
                }
                nodes[node].sym = static_cast<int>(sym);
            }
            return nodes;
        }

        std::size_t entry_size(std::string_view name, std::string_view value) {
            return name.size() + value.size() + 32;
        }

        std::optional<std::size_t> find_static_exact(std::string_view name, std::string_view value) {
            for (std::size_t i = 0; i < kStaticTable.size(); ++i) {
                if (kStaticTable[i].name == name && kStaticTable[i].value == value) {
                    return i + 1;
                }
            }
            return std::nullopt;
        }

        std::optional<std::size_t> find_static_name(std::string_view name) {
            for (std::size_t i = 0; i < kStaticTable.size(); ++i) {
                if (kStaticTable[i].name == name) {
                    return i + 1;
                }
            }
            return std::nullopt;
        }

        std::optional<std::size_t> find_dynamic_exact(const std::vector<HeaderField> &table, std::string_view name,
                                                      std::string_view value) {
            for (std::size_t i = 0; i < table.size(); ++i) {
                if (table[i].name == name && table[i].value == value) {
                    return kStaticTable.size() + 1 + i;
                }
            }
            return std::nullopt;
        }

        std::optional<std::size_t> find_dynamic_name(const std::vector<HeaderField> &table, std::string_view name) {
            for (std::size_t i = 0; i < table.size(); ++i) {
                if (table[i].name == name) {
                    return kStaticTable.size() + 1 + i;
                }
            }
            return std::nullopt;
        }

        std::optional<HeaderField> lookup_index(const std::vector<HeaderField> &table, std::size_t index) {
            if (index == 0) { return std::nullopt; }
            if (index <= kStaticTable.size()) {
                const auto &entry = kStaticTable[index - 1];
                return HeaderField{std::string(entry.name), std::string(entry.value), Indexing::Incremental};
            }
            std::size_t dyn_index = index - kStaticTable.size() - 1;
            if (dyn_index >= table.size()) { return std::nullopt; }
            return table[dyn_index];
        }

        void evict_dynamic(std::vector<HeaderField> &table, std::size_t &current_size, std::size_t max_size) {
            while (!table.empty() && current_size > max_size) {
                const auto &entry = table.back();
                current_size -= entry_size(entry.name, entry.value);
                table.pop_back();
            }
        }

        void add_dynamic(std::vector<HeaderField> &table, std::size_t &current_size, std::size_t max_size,
                         const HeaderField &entry) {
            const std::size_t size = entry_size(entry.name, entry.value);
            if (size > max_size) {
                table.clear();
                current_size = 0;
                return;
            }
            table.insert(table.begin(), HeaderField{entry.name, entry.value, Indexing::Incremental});
            current_size += size;
            evict_dynamic(table, current_size, max_size);
        }

        void encode_integer(std::string &out, std::uint32_t value, std::uint8_t prefix_bits, std::uint8_t prefix_mask) {
            const std::uint32_t max_prefix = (1u << prefix_bits) - 1u;
            if (value < max_prefix) {
                out.push_back(static_cast<char>(prefix_mask | value));
                return;
            }
            out.push_back(static_cast<char>(prefix_mask | max_prefix));
            value -= max_prefix;
            while (value >= 128) {
                out.push_back(static_cast<char>((value & 0x7f) | 0x80));
                value >>= 7;
            }
            out.push_back(static_cast<char>(value));
        }

        std::expected<std::uint32_t, Error> decode_integer(std::string_view data, std::size_t &pos, std::uint8_t prefix_bits) {
            const std::uint32_t max_prefix = (1u << prefix_bits) - 1u;
            if (pos >= data.size()) {
                return std::unexpected(Error{ErrorCode::BUFFER_UNDERFLOW, "integer prefix missing"});
            }
            std::uint8_t first = static_cast<std::uint8_t>(data[pos++]);
            std::uint32_t value = first & max_prefix;
            if (value < max_prefix) { return value; }
            std::uint32_t m = 0;
            while (pos < data.size()) {
                std::uint8_t byte = static_cast<std::uint8_t>(data[pos++]);
                value += static_cast<std::uint32_t>(byte & 0x7f) << m;
                if ((byte & 0x80) == 0) { return value; }
                m += 7;
                if (m > 28) {
                    return std::unexpected(Error{ErrorCode::INVALID_INTEGER, "integer overflow"});
                }
            }
            return std::unexpected(Error{ErrorCode::BUFFER_UNDERFLOW, "integer continuation missing"});
        }

        std::string huffman_encode(std::string_view input) {
            std::string out;
            std::uint64_t bitbuf = 0;
            int bitcount = 0;
            for (unsigned char ch: input) {
                std::uint32_t code = kHuffmanCodes[ch];
                std::uint8_t len = kHuffmanCodeLen[ch];
                bitbuf = (bitbuf << len) | code;
                bitcount += len;
                while (bitcount >= 8) {
                    bitcount -= 8;
                    std::uint8_t byte = static_cast<std::uint8_t>((bitbuf >> bitcount) & 0xff);
                    out.push_back(static_cast<char>(byte));
                }
            }
            if (bitcount > 0) {
                bitbuf <<= (8 - bitcount);
                bitbuf |= (1u << (8 - bitcount)) - 1u;
                out.push_back(static_cast<char>(bitbuf & 0xff));
            }
            return out;
        }

        std::expected<std::string, Error> huffman_decode(std::string_view input) {
            const auto &nodes = huffman_tree();
            std::string out;
            int node = 0;
            bool prefix_all_ones = true;
            int prefix_len = 0;

            for (unsigned char ch: input) {
                for (int i = 7; i >= 0; --i) {
                    int bit = (ch >> i) & 0x1;
                    node = nodes[node].child[bit];
                    if (node < 0) {
                        return std::unexpected(Error{ErrorCode::INVALID_HUFFMAN, "invalid huffman code"});
                    }
                    if (prefix_all_ones) {
                        if (bit == 1) {
                            ++prefix_len;
                        } else {
                            prefix_all_ones = false;
                        }
                    }
                    if (nodes[node].sym >= 0) {
                        if (nodes[node].sym == 256) {
                            return std::unexpected(Error{ErrorCode::INVALID_HUFFMAN, "unexpected EOS"});
                        }
                        out.push_back(static_cast<char>(nodes[node].sym));
                        node = 0;
                        prefix_all_ones = true;
                        prefix_len = 0;
                    }
                }
            }
            if (node != 0) {
                if (!prefix_all_ones || prefix_len > 30) {
                    return std::unexpected(Error{ErrorCode::INVALID_HUFFMAN, "invalid huffman padding"});
                }
            }
            return out;
        }

        std::string encode_string(std::string_view input, bool use_huffman) {
            std::string encoded;
            if (use_huffman) {
                std::string huff = huffman_encode(input);
                encode_integer(encoded, static_cast<std::uint32_t>(huff.size()), 7, 0x80);
                encoded += huff;
            } else {
                encode_integer(encoded, static_cast<std::uint32_t>(input.size()), 7, 0x00);
                encoded.append(input.data(), input.size());
            }
            return encoded;
        }

        std::expected<std::string, Error> decode_string(std::string_view data, std::size_t &pos) {
            if (pos >= data.size()) {
                return std::unexpected(Error{ErrorCode::BUFFER_UNDERFLOW, "string prefix missing"});
            }
            bool huffman = (static_cast<std::uint8_t>(data[pos]) & 0x80) != 0;
            auto len_res = decode_integer(data, pos, 7);
            if (!len_res) { return std::unexpected(len_res.error()); }
            std::size_t len = *len_res;
            if (data.size() - pos < len) {
                return std::unexpected(Error{ErrorCode::BUFFER_UNDERFLOW, "string data missing"});
            }
            std::string_view raw = data.substr(pos, len);
            pos += len;
            if (!huffman) {
                return std::string(raw);
            }
            return huffman_decode(raw);
        }
    }// namespace

    Encoder::Encoder(std::size_t max_table_size)
        : max_table_size_(max_table_size),
          current_table_size_(0),
          pending_table_size_update_(false) {}

    void Encoder::set_max_dynamic_table_size(std::size_t size) {
        max_table_size_ = size;
        pending_table_size_update_ = true;
        evict_dynamic(dynamic_table_, current_table_size_, max_table_size_);
    }

    std::string Encoder::encode(const std::vector<HeaderField> &headers, bool use_huffman) {
        std::string out;
        if (pending_table_size_update_) {
            encode_integer(out, static_cast<std::uint32_t>(max_table_size_), 5, 0x20);
            pending_table_size_update_ = false;
        }
        for (const auto &header: headers) {
            if (header.indexing == Indexing::Incremental) {
                if (auto exact = find_static_exact(header.name, header.value)) {
                    encode_integer(out, static_cast<std::uint32_t>(*exact), 7, 0x80);
                    continue;
                }
                if (auto exact = find_dynamic_exact(dynamic_table_, header.name, header.value)) {
                    encode_integer(out, static_cast<std::uint32_t>(*exact), 7, 0x80);
                    continue;
                }

                std::optional<std::size_t> name_index = find_static_name(header.name);
                if (!name_index) { name_index = find_dynamic_name(dynamic_table_, header.name); }
                if (name_index) {
                    encode_integer(out, static_cast<std::uint32_t>(*name_index), 6, 0x40);
                } else {
                    encode_integer(out, 0, 6, 0x40);
                    out += encode_string(header.name, use_huffman);
                }
                out += encode_string(header.value, use_huffman);
                add_dynamic(dynamic_table_, current_table_size_, max_table_size_, header);
            } else if (header.indexing == Indexing::Never) {
                std::optional<std::size_t> name_index = find_static_name(header.name);
                if (!name_index) { name_index = find_dynamic_name(dynamic_table_, header.name); }
                if (name_index) {
                    encode_integer(out, static_cast<std::uint32_t>(*name_index), 4, 0x10);
                } else {
                    encode_integer(out, 0, 4, 0x10);
                    out += encode_string(header.name, use_huffman);
                }
                out += encode_string(header.value, use_huffman);
            } else {
                std::optional<std::size_t> name_index = find_static_name(header.name);
                if (!name_index) { name_index = find_dynamic_name(dynamic_table_, header.name); }
                if (name_index) {
                    encode_integer(out, static_cast<std::uint32_t>(*name_index), 4, 0x00);
                } else {
                    encode_integer(out, 0, 4, 0x00);
                    out += encode_string(header.name, use_huffman);
                }
                out += encode_string(header.value, use_huffman);
            }
        }
        return out;
    }

    Decoder::Decoder(std::size_t max_table_size)
        : max_table_size_(max_table_size), current_table_size_(0) {}

    void Decoder::set_max_dynamic_table_size(std::size_t size) {
        max_table_size_ = size;
        evict_dynamic(dynamic_table_, current_table_size_, max_table_size_);
    }

    std::expected<std::vector<HeaderField>, Error> Decoder::decode(std::string_view block) {
        std::vector<HeaderField> headers;
        std::size_t pos = 0;
        bool seen_headers = false;

        while (pos < block.size()) {
            std::uint8_t byte = static_cast<std::uint8_t>(block[pos]);
            if (byte & 0x80) {
                auto index = decode_integer(block, pos, 7);
                if (!index) { return std::unexpected(index.error()); }
                auto entry = lookup_index(dynamic_table_, *index);
                if (!entry) {
                    return std::unexpected(Error{ErrorCode::INVALID_INDEX, "indexed header out of range"});
                }
                headers.push_back(*entry);
                seen_headers = true;
                continue;
            }

            if (byte & 0x40) {
                auto name_index = decode_integer(block, pos, 6);
                if (!name_index) { return std::unexpected(name_index.error()); }
                std::string name;
                if (*name_index == 0) {
                    auto decoded = decode_string(block, pos);
                    if (!decoded) { return std::unexpected(decoded.error()); }
                    name = std::move(*decoded);
                } else {
                    auto entry = lookup_index(dynamic_table_, *name_index);
                    if (!entry) {
                        return std::unexpected(Error{ErrorCode::INVALID_INDEX, "name index out of range"});
                    }
                    name = entry->name;
                }
                auto value = decode_string(block, pos);
                if (!value) { return std::unexpected(value.error()); }
                HeaderField field{name, std::move(*value), Indexing::Incremental};
                headers.push_back(field);
                add_dynamic(dynamic_table_, current_table_size_, max_table_size_, field);
                seen_headers = true;
                continue;
            }

            if (byte & 0x20) {
                if (seen_headers) {
                    return std::unexpected(Error{ErrorCode::INVALID_TABLE_SIZE_UPDATE,
                                                 "dynamic table size update after headers"});
                }
                auto size = decode_integer(block, pos, 5);
                if (!size) { return std::unexpected(size.error()); }
                if (*size > max_table_size_) {
                    return std::unexpected(
                            Error{ErrorCode::INVALID_TABLE_SIZE_UPDATE, "dynamic table size exceeds limit"});
                }
                max_table_size_ = *size;
                evict_dynamic(dynamic_table_, current_table_size_, max_table_size_);
                continue;
            }

            bool never_indexed = (byte & 0x10) != 0;
            auto name_index = decode_integer(block, pos, 4);
            if (!name_index) { return std::unexpected(name_index.error()); }
            std::string name;
            if (*name_index == 0) {
                auto decoded = decode_string(block, pos);
                if (!decoded) { return std::unexpected(decoded.error()); }
                name = std::move(*decoded);
            } else {
                auto entry = lookup_index(dynamic_table_, *name_index);
                if (!entry) {
                    return std::unexpected(Error{ErrorCode::INVALID_INDEX, "name index out of range"});
                }
                name = entry->name;
            }
            auto value = decode_string(block, pos);
            if (!value) { return std::unexpected(value.error()); }
            headers.push_back(HeaderField{name, std::move(*value), never_indexed ? Indexing::Never : Indexing::None});
            seen_headers = true;
        }
        return headers;
    }
}// namespace usub::unet::http::v2::hpack
