#include "DxResources.h"

namespace {

HRESULT CreateD3dDevice(UINT flags, D3D_DRIVER_TYPE driverType, ComPtr<ID3D11Device>* device) {
    static constexpr D3D_FEATURE_LEVEL kLevels[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_9_1,
    };
    return D3D11CreateDevice(nullptr, driverType, nullptr, flags, kLevels, ARRAYSIZE(kLevels),
                             D3D11_SDK_VERSION, device->ReleaseAndGetAddressOf(), nullptr, nullptr);
}

} // namespace

void DxResources::EnsureCreated() {
    if (m_d3dDevice)
        return;

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT; // required for D2D interop
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    HRESULT hr = CreateD3dDevice(flags, D3D_DRIVER_TYPE_HARDWARE, &m_d3dDevice);
#ifdef _DEBUG
    if (FAILED(hr)) { // debug layer not installed on this machine
        flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = CreateD3dDevice(flags, D3D_DRIVER_TYPE_HARDWARE, &m_d3dDevice);
    }
#endif
    if (FAILED(hr))
        hr = CreateD3dDevice(flags, D3D_DRIVER_TYPE_WARP, &m_d3dDevice);
    ThrowIfFailed(hr, "D3D11CreateDevice");

    ComPtr<IDXGIDevice> dxgiDevice;
    ThrowIfFailed(m_d3dDevice.As(&dxgiDevice), "ID3D11Device -> IDXGIDevice");
    ComPtr<IDXGIAdapter> adapter;
    ThrowIfFailed(dxgiDevice->GetAdapter(&adapter), "IDXGIDevice::GetAdapter");
    ThrowIfFailed(adapter->GetParent(IID_PPV_ARGS(&m_dxgiFactory)), "IDXGIAdapter::GetParent");

    D2D1_FACTORY_OPTIONS options = {};
#ifdef _DEBUG
    options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif
    ThrowIfFailed(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1),
                                    &options,
                                    reinterpret_cast<void**>(m_d2dFactory.ReleaseAndGetAddressOf())),
                  "D2D1CreateFactory");
    ThrowIfFailed(m_d2dFactory->CreateDevice(dxgiDevice.Get(), &m_d2dDevice),
                  "ID2D1Factory1::CreateDevice");

    ThrowIfFailed(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                      reinterpret_cast<IUnknown**>(
                                          m_dwriteFactory.ReleaseAndGetAddressOf())),
                  "DWriteCreateFactory");
}

void DxResources::Discard() {
    ++m_generation;
    m_d2dDevice.Reset();
    m_d2dFactory.Reset();
    m_dxgiFactory.Reset();
    m_d3dDevice.Reset();
    // m_dwriteFactory is device-independent; keep it.
}
