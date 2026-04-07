#pragma once


#include <expected>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

// TODO: remove later (better place)
#include "unet/utils/string_utils.h"

namespace usub::unet::mime::multipart {

    struct Part {
        std::string content_type;
        std::unordered_map<std::string, std::string> disposition;
        std::string data;
        std::unordered_map<std::string, std::vector<std::string>, usub::utils::CaseInsensitiveHash, usub::utils::CaseInsensitiveEqual> headers;
    };

    class FormData {
    private:
        using parts_map_t = std::unordered_map<std::string, std::vector<Part>, usub::utils::CaseInsensitiveHash, usub::utils::CaseInsensitiveEqual>;

        std::string boundary_;
        parts_map_t parts_by_name_;

    public:
        using iterator = parts_map_t::iterator;
        using const_iterator = parts_map_t::const_iterator;

        explicit FormData(std::string boundary = "");
        FormData(std::string boundary, std::string raw_data);

        std::expected<void, std::string> parse(std::string input);

        std::vector<Part> &operator[](const std::string &name);
        const std::vector<Part> &operator[](const std::string &name) const;

        std::vector<Part> &at(const std::string &name);
        const std::vector<Part> &at(const std::string &name) const;

        bool contains(const std::string &name) const;

        std::size_t size() const;
        bool empty() const;
        void clear();
        std::size_t erase(const std::string &name);

        iterator begin();
        iterator end();
        const_iterator begin() const;
        const_iterator end() const;

        const parts_map_t &parts_by_name() const;
        parts_map_t &parts_by_name();
    };

}// namespace usub::unet::mime::multipart
