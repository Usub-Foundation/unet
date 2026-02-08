#pragma once

// TODO: FindBetterPlacement for all this

#include <sstream>
#include <string>
#include <vector>

#include <algorithm>
namespace usub::utils {
    struct CaseInsensitiveHash {
        std::size_t operator()(const std::string &key) const;
    };

    struct CaseInsensitiveEqual {
        bool operator()(const std::string &lhs, const std::string &rhs) const;
    };

    std::vector<std::string> split(const std::string &s, char delimiter);

    void trim(std::string &s);

    std::string trim_copy(const std::string &s);

    std::string toLower(const std::string &s);

}// namespace usub::utils