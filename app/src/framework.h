#pragma once

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00 // Windows 10+
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

using Microsoft::WRL::ComPtr;

// Graphics-stack failure (device creation or loss). Recoverable by dropping
// and rebuilding device-dependent resources; distinct from std::exception so
// unrelated failures are not misdiagnosed as device loss.
struct GraphicsError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// For graphics calls whose failure means the device stack must be rebuilt;
// callers that can degrade gracefully must check HRESULTs instead.
inline void ThrowIfFailed(HRESULT hr, const char* what) {
    if (FAILED(hr))
        throw GraphicsError(what);
}
