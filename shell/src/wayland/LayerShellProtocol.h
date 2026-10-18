#pragma once

// The generated layer-shell header names a parameter `namespace`, a C++
// keyword; rename it for the duration of the include. Everything it pulls in
// is included first so the macro only touches the protocol declarations.
#include <cstddef>
#include <cstdint>
#include <wayland-client.h>

#define namespace namespace_
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#undef namespace
