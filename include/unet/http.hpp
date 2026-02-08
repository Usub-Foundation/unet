#pragma once

#include <uvent/Uvent.h>

#include "unet/http/request.hpp"
#include "unet/http/response.hpp"
#include "unet/http/server.hpp"

using ServerHandler = usub::uvent::task::Awaitable<void>;