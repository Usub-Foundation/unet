#pragma once

#if defined(UVENT_USE_BOOST_ASIO)
#include "unet/core/io_provider/boost_asio.hpp"
#elif defined(UVENT_USE_LIBUV)
#include "unet/core/io_provider/libuv.hpp"
#elif defined(UVENT_USE_LIBEVENT)
#include "unet/core/io_provider/libevent.hpp"
#else
#include "unet/core/io_provider/uvent.hpp"
#endif