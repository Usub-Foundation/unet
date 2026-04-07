#pragma once

#include <cstddef>

// According to https://en.wikipedia.org/wiki/IEEE_1541

constexpr std::size_t operator"" KB(unsigned long long int value) {
    return static_cast<std::size_t>(value * 1000);
}

constexpr std::size_t operator"" KiB(unsigned long long int value) {
    return static_cast<std::size_t>(value * 1024);
}

constexpr std::size_t operator"" MB(unsigned long long int value) {
    return static_cast<std::size_t>(value * 1000 * 1000);
}

constexpr std::size_t operator"" MiB(unsigned long long int value) {
    return static_cast<std::size_t>(value * 1024 * 1024);
}

constexpr std::size_t operator"" GB(unsigned long long int value) {
    return static_cast<std::size_t>(value * 1000 * 1000 * 1000);
}

constexpr std::size_t operator"" GiB(unsigned long long int value) {
    return static_cast<std::size_t>(value * 1024 * 1024 * 1024);
}

constexpr std::size_t operator"" TB(unsigned long long int value) {
    return static_cast<std::size_t>(value * 1000 * 1000 * 1000 * 1000);
}

constexpr std::size_t operator"" TiB(unsigned long long int value) {
    return static_cast<std::size_t>(value * 1024 * 1024 * 1024 * 1024);
}

constexpr std::size_t operator"" PB(unsigned long long int value) {
    return static_cast<std::size_t>(value * 1000 * 1000 * 1000 * 1000 * 1000);
}

constexpr std::size_t operator"" PiB(unsigned long long int value) {
    return static_cast<std::size_t>(value * 1024 * 1024 * 1024 * 1024 * 1024);
}
