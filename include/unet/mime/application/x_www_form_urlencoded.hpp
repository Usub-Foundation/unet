#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace usub::unet::mime::application::x_www_form_urlencoded {

    inline constexpr std::string_view content_type = "application/x-www-form-urlencoded";

    struct Field {
        std::string name;
        std::string value;
    };

    using Fields = std::vector<Field>;
    using FieldMap = std::unordered_map<std::string, std::vector<std::string>>;

    std::expected<std::string, std::string> decode_component(std::string_view input);
    std::string encode_component(std::string_view input);

    std::expected<Fields, std::string> parse(std::string_view input);
    std::expected<FieldMap, std::string> parse_to_map(std::string_view input);

    std::string serialize(const Fields &fields);
    std::string serialize_map(const FieldMap &fields);

}// namespace usub::unet::mime::application::x_www_form_urlencoded
