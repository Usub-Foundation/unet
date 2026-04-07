#pragma once

#include <cstdint>
#include <string>

// The URI Is much simpler then I thought initially. No need to overcomplicate it.
// Threr is no set rule for almost anything in URI, except general syntax rules. (alphanum and special chars)
// im parts, otherwise ots all

/*
     foo://example.com:8042/over/there?name=ferret#nose
     \_/   \______________/\_________/ \_________/ \__/
      |           |            |            |        |
   scheme     authority       path        query   fragment
      |   _____________________|__
     / \ /                        \
     urn:example:animal:ferret:nose
*/

namespace usub::unet::uri {

    struct Authority {
        std::string userinfo;// Usually empty
        std::string host;
        std::uint16_t port = 0;
    };

    struct URI {
        std::string scheme;
        Authority authority;
        std::string path;
        std::string query;
        std::string fragment;
    };

}// namespace usub::unet::uri