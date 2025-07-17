#include <xtl.h>
#include <imgui.h>

#include "imgui_impl_xbox360.h"
#include "imgui_impl_dx9.h"

Direct3D *g_pD3D = nullptr;
D3DDevice *g_pd3dDevice = nullptr;

void CreateD3DDevice()
{
    // Create the D3D object
    g_pD3D = Direct3DCreate9(D3D_SDK_VERSION);

    // D3DDevice creation options
    D3DPRESENT_PARAMETERS d3dpp = {};

    // Definition is always 720p on Xbox 360, other definitions are created by the
    // hardware scaler
    d3dpp.BackBufferWidth = 1280;
    d3dpp.BackBufferHeight = 720;

    // Make sure the gamma is correct
    d3dpp.BackBufferFormat = static_cast<D3DFORMAT>(MAKESRGBFMT(D3DFMT_A8R8G8B8));
    d3dpp.FrontBufferFormat = static_cast<D3DFORMAT>(MAKESRGBFMT(D3DFMT_LE_X8R8G8B8));

    // Depth stencil
    d3dpp.EnableAutoDepthStencil = TRUE;
    d3dpp.AutoDepthStencilFormat = D3DFMT_D24S8;

    // VSync
    d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;

    // Create the Direct3D device
    g_pD3D->CreateDevice(0, D3DDEVTYPE_HAL, NULL, D3DCREATE_HARDWARE_VERTEXPROCESSING, &d3dpp, &g_pd3dDevice);
}

bool InitImGui()
{
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup platform backend
    if (!ImGui_ImplXbox360_Init())
    {
        OutputDebugStringA("Xbox360 Init failed\n");
        return false;
    }

    // Setup renderer backend
    if (!ImGui_ImplDX9_Init(g_pd3dDevice))
    {
        OutputDebugStringA("DX9 Init failed\n");
        return false;
    }

    return true;
}

void Render()
{
    // Start the Dear ImGui frame
    ImGui_ImplDX9_NewFrame();
    ImGui_ImplXbox360_NewFrame();
    ImGui::NewFrame();

    // Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
    ImGui::ShowDemoWindow();

    // Render Dear ImGui
    ImGui::EndFrame();
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    g_pd3dDevice->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(115, 140, 153), 1.0f, 0);
    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    g_pd3dDevice->Present(nullptr, nullptr, nullptr, nullptr);
}

void __cdecl main()
{
    CreateD3DDevice();

    if (!InitImGui())
        return;

    for (;;)
        Render();
}
