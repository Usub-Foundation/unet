#pragma once

#include <array>
#include <cstdint>

namespace usub::unet::http {

    constexpr std::array<std::uint8_t, 256> buildTcharTable() {
        std::array<std::uint8_t, 256> table{};
        for (char c = 'A'; c <= 'Z'; ++c) table[static_cast<unsigned char>(c)] = 1;
        for (char c = 'a'; c <= 'z'; ++c) table[static_cast<unsigned char>(c)] = 1;
        for (char c = '0'; c <= '9'; ++c) table[static_cast<unsigned char>(c)] = 1;
        for (char c: {'!', '#', '$', '%', '&', '\'', '*', '+', '-', '.', '^', '_', '`', '|', '~'}) {
            table[static_cast<unsigned char>(c)] = 1;
        }
        return table;
    }

    constexpr std::array<std::uint8_t, 256> buildVcharObsTable() {
        std::array<std::uint8_t, 256> table{};
        for (char c = '!'; c <= '~'; ++c) table[static_cast<unsigned char>(c)] = 1;
        for (unsigned char c = 128; c <= 255 && c >= 128; ++c) table[c] = 1;

        // Should be supported?
        table[' '] = 1;
        table['\t'] = 1;
        return table;
    }

    constexpr std::array<std::uint8_t, 256> buildSchemeTable() {
        std::array<std::uint8_t, 256> table{};
        for (char c = 'A'; c <= 'Z'; ++c) table[static_cast<unsigned char>(c)] = 1;
        for (char c = 'a'; c <= 'z'; ++c) table[static_cast<unsigned char>(c)] = 1;
        for (char c = '0'; c <= '9'; ++c) table[static_cast<unsigned char>(c)] = 1;
        for (char c: {'+', '-', '.'}) { table[static_cast<unsigned char>(c)] = 1; }
        return table;
    }

    constexpr std::array<std::uint8_t, 256> buildPathTable() {
        std::array<std::uint8_t, 256> table{};
        for (char c = 'A'; c <= 'Z'; ++c) table[static_cast<unsigned char>(c)] = 1;
        for (char c = 'a'; c <= 'z'; ++c) table[static_cast<unsigned char>(c)] = 1;
        for (char c = '0'; c <= '9'; ++c) table[static_cast<unsigned char>(c)] = 1;
        for (char c: {'-', '.', '_', '~'}) { table[static_cast<unsigned char>(c)] = 1; }
        for (char c: {'!', '$', '&', '\'', '(', ')', '*', '+', ',', ';', '='}) {
            table[static_cast<unsigned char>(c)] = 1;
        }
        for (char c: {':', '@', '/'}) { table[static_cast<unsigned char>(c)] = 1; }
        return table;
    }

    constexpr std::array<std::uint8_t, 256> buildQueryTable() {
        std::array<std::uint8_t, 256> table{};
        for (char c = 'A'; c <= 'Z'; ++c) table[static_cast<unsigned char>(c)] = 1;
        for (char c = 'a'; c <= 'z'; ++c) table[static_cast<unsigned char>(c)] = 1;
        for (char c = '0'; c <= '9'; ++c) table[static_cast<unsigned char>(c)] = 1;
        for (char c: {'-', '.', '_', '~'}) { table[static_cast<unsigned char>(c)] = 1; }
        for (char c: {'!', '$', '&', '\'', '(', ')', '*', '+', ',', ';', '='}) {
            table[static_cast<unsigned char>(c)] = 1;
        }
        for (char c: {':', '@', '/', '?', '%'}) { table[static_cast<unsigned char>(c)] = 1; }
        return table;
    }

    constexpr std::array<std::uint8_t, 256> buildHostTable() {
        std::array<std::uint8_t, 256> table{};
        for (char c = 'A'; c <= 'Z'; ++c) table[static_cast<unsigned char>(c)] = 1;
        for (char c = 'a'; c <= 'z'; ++c) table[static_cast<unsigned char>(c)] = 1;
        for (char c = '0'; c <= '9'; ++c) table[static_cast<unsigned char>(c)] = 1;
        for (char c: {'-', '.', '_', '~'}) { table[static_cast<unsigned char>(c)] = 1; }
        for (char c: {'!', '$', '&', '\'', '(', ')', '*', '+', ',', ';', '=', '%'}) {
            table[static_cast<unsigned char>(c)] = 1;
        }
        return table;
    }

    constexpr std::array<std::uint8_t, 256> buildVersionTable() {
        std::array<std::uint8_t, 256> table{};
        table['H'] = 1;
        table['T'] = 1;
        table['P'] = 1;
        table['/'] = 1;
        table['1'] = 1;
        table['.'] = 1;
        table['0'] = 1;
        return table;
    }

    constexpr std::array<std::uint8_t, 256> tchar_table = buildTcharTable();
    constexpr std::array<std::uint8_t, 256> vchar_obs_table = buildVcharObsTable();
    constexpr std::array<std::uint8_t, 256> scheme_table = buildSchemeTable();
    constexpr std::array<std::uint8_t, 256> path_table = buildPathTable();
    constexpr std::array<std::uint8_t, 256> query_table = buildQueryTable();
    constexpr std::array<std::uint8_t, 256> host_table = buildHostTable();
    constexpr std::array<std::uint8_t, 256> version_table = buildVersionTable();

    inline bool isVersion(unsigned char c) { return version_table[c] != 0; }

    inline bool isAlpha(unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

    inline bool isTchar(unsigned char c) { return tchar_table[c] != 0; }

    inline bool isVcharOrObs(unsigned char c) { return vchar_obs_table[c] != 0; }

    inline bool isSchemeChar(unsigned char c) { return scheme_table[c] != 0; }

    inline bool isPathChar(unsigned char c) { return path_table[c] != 0; }

    inline bool isQueryChar(unsigned char c) { return query_table[c] != 0; }

    inline bool isHostChar(unsigned char c) { return host_table[c] != 0; }

    inline char asciiLower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c; }

    inline bool isHexDigit(unsigned char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }

}// namespace usub::unet::http