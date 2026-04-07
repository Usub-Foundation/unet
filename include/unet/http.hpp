#pragma once

#include <uvent/Uvent.h>

#include "unet/http/core/request.hpp"
#include "unet/http/core/response.hpp"
#include "unet/http/server.hpp"

using ServerHandler = usub::uvent::task::Awaitable<void>;