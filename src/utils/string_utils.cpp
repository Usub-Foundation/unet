#include "unet/utils/string_utils.h"

namespace usub::utils {

    std::size_t CaseInsensitiveHash::operator()(const std::string &key) const {
        std::size_t hash = 0;
        for (char c: key) {
            hash ^= std::hash<char>()(std::tolower(c)) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        }
        return hash;
    }


    bool CaseInsensitiveEqual::operator()(const std::string &lhs, const std::string &rhs) const {
        return std::equal(lhs.begin(), lhs.end(),
                          rhs.begin(), rhs.end(),
                          [](char a, char b) {
                              return std::tolower(a) == std::tolower(b);
                          });
    }


    std::vector<std::string> split(const std::string &s, char delimiter) {
        std::vector<std::string> tokens;
        std::string token;
        std::istringstream tokenStream(s);
        while (std::getline(tokenStream, token, delimiter)) {
            tokens.push_back(token);
        }
        return tokens;
    }

    void trim(std::string &s) {
        // Trim leading spaces
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
                    return !std::isspace(ch);
                }));
        // Trim trailing spaces
        s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
                    return !std::isspace(ch);
                }).base(),
                s.end());
    }

    std::string trim_copy(const std::string &s) {
        std::string result = s;
        trim(result);
        return result;
    }

    std::string toLower(const std::string &s) {
        std::string result = s;
        std::transform(result.begin(), result.end(), result.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        return result;
    }

}// namespace usub::utils