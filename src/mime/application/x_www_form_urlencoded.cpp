#include "unet/mime/application/x_www_form_urlencoded.hpp"

#include <cctype>
#include <utility>

namespace usub::unet::mime::application::x_www_form_urlencoded {
    namespace {
        int hex_value(char c) {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        }

        bool is_encode_safe(unsigned char c) {
            return std::isalnum(c) || c == '*' || c == '-' || c == '.' || c == '_';
        }
    }// namespace

    std::expected<std::string, std::string> decode_component(std::string_view input) {
        std::string output;
        output.reserve(input.size());

        for (std::size_t i = 0; i < input.size(); ++i) {
            const char c = input[i];

            if (c == '+') {
                output.push_back(' ');
                continue;
            }

            if (c != '%') {
                output.push_back(c);
                continue;
            }

            if (i + 2 >= input.size()) {
                return std::unexpected("Incomplete percent-encoded sequence");
            }

            const int high = hex_value(input[i + 1]);
            const int low = hex_value(input[i + 2]);

            if (high < 0 || low < 0) {
                return std::unexpected("Invalid percent-encoded sequence");
            }

            output.push_back(static_cast<char>((high << 4) | low));
            i += 2;
        }

        return output;
    }

    std::string encode_component(std::string_view input) {
        static constexpr char hex[] = "0123456789ABCDEF";

        std::string output;
        output.reserve(input.size());

        for (const unsigned char c: input) {
            if (c == ' ') {
                output.push_back('+');
            } else if (is_encode_safe(c)) {
                output.push_back(static_cast<char>(c));
            } else {
                output.push_back('%');
                output.push_back(hex[c >> 4]);
                output.push_back(hex[c & 0x0F]);
            }
        }

        return output;
    }

    std::expected<Fields, std::string> parse(std::string_view input) {
        Fields fields;

        std::size_t start = 0;
        while (start <= input.size()) {
            const std::size_t end = input.find('&', start);
            const std::string_view pair = input.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);

            if (!pair.empty()) {
                const std::size_t equals = pair.find('=');
                const std::string_view raw_name = pair.substr(0, equals);
                const std::string_view raw_value = equals == std::string_view::npos ? std::string_view{} : pair.substr(equals + 1);

                auto name = decode_component(raw_name);
                if (!name) return std::unexpected(name.error());

                auto value = decode_component(raw_value);
                if (!value) return std::unexpected(value.error());

                fields.push_back(Field{std::move(*name), std::move(*value)});
            }

            if (end == std::string_view::npos) break;
            start = end + 1;
        }

        return fields;
    }

    std::expected<FieldMap, std::string> parse_to_map(std::string_view input) {
        auto parsed = parse(input);
        if (!parsed) return std::unexpected(parsed.error());

        FieldMap fields;
        for (auto &field: *parsed) {
            fields[std::move(field.name)].push_back(std::move(field.value));
        }

        return fields;
    }

    std::string serialize(const Fields &fields) {
        std::string output;

        for (const auto &field: fields) {
            if (!output.empty()) output.push_back('&');
            output.append(encode_component(field.name));
            output.push_back('=');
            output.append(encode_component(field.value));
        }

        return output;
    }

    std::string serialize_map(const FieldMap &fields) {
        std::string output;

        for (const auto &[name, values]: fields) {
            for (const auto &value: values) {
                if (!output.empty()) output.push_back('&');
                output.append(encode_component(name));
                output.push_back('=');
                output.append(encode_component(value));
            }
        }

        return output;
    }

}// namespace usub::unet::mime::application::x_www_form_urlencoded
