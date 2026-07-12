#pragma once

#include "framework.h"

// Process-wide D3D11/D2D/DWrite objects shared by all panes. All Direct2D use
// stays on the UI thread (render workers will only produce CPU pixmaps), so
// the D2D factory is single-threaded.
class DxResources {
public:
    void EnsureCreated();
    void Discard(); // after device removal; EnsureCreated() rebuilds lazily
    bool IsCreated() const { return m_d3dDevice != nullptr; }

    // Bumped by Discard(). Consumers stamp the generation their device-dependent
    // resources were built on; a mismatch means those resources are stale and
    // must be rebuilt, and only the pane whose stamp matches the current
    // generation may Discard() (prevents one pane from destroying a healthy
    // device its sibling just rebuilt).
    unsigned Generation() const { return m_generation; }

    ID3D11Device* D3dDevice() const { return m_d3dDevice.Get(); }
    IDXGIFactory2* DxgiFactory() const { return m_dxgiFactory.Get(); }
    ID2D1Device* D2dDevice() const { return m_d2dDevice.Get(); }
    // Discard() resets this too: factory-owned objects (stroke styles) follow
    // the device generation, unlike the DWrite factory below.
    ID2D1Factory1* D2dFactory() const { return m_d2dFactory.Get(); }
    IDWriteFactory* DWriteFactory() const { return m_dwriteFactory.Get(); }

private:
    ComPtr<ID3D11Device> m_d3dDevice;
    ComPtr<IDXGIFactory2> m_dxgiFactory;
    ComPtr<ID2D1Factory1> m_d2dFactory;
    ComPtr<ID2D1Device> m_d2dDevice;
    ComPtr<IDWriteFactory> m_dwriteFactory;
    unsigned m_generation = 0;
};
