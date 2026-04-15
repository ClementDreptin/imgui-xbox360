#include <imgui.h>

#include "imgui_impl_xbox360.h"
#include "imgui_impl_dx9.h"
#include "Utils.h"

// TODO:
// - Fix crash after launching dash a second time. Add missing ImGui cleanup code
//   (cf. default dx9 backend, don't forget AddRef and Release on the resources)

typedef enum _ImGuiState
{
    ImGuiState_None,
    ImGuiState_RequiresInitialization,
    ImGuiState_CanRender,
    ImGuiState_RequiresCleanup,
} ImGuiState;

// Global state
bool g_Running = true;
ImGuiState g_ImGuiState = ImGuiState_None;
D3DDevice *g_pd3dDevice = NULL;
Detour *g_pXuiRenderEndDetour = NULL;

#define DASH_TITLE_ID 0xFFFE07D1

bool GetD3DDevice(HANDLE hDC)
{
    // We can't link against xuirender.lib and rely on it because the current SDK may
    // have a different of XUI than the one used by the Dashboard, and the internal
    // struct behind hDC could be different. Instead, we import this function from xam.xex
    // and assume the version of XUI is the same between xam.xex and dash.xex
    HRESULT (*XuiRenderGetDevice)(HANDLE hDC, D3DDevice **ppDevice) = static_cast<decltype(XuiRenderGetDevice)>(ResolveExport("xam.xex", 2095));

    return SUCCEEDED(XuiRenderGetDevice(hDC, &g_pd3dDevice));
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
        return false;

    // Setup renderer backend
    if (!ImGui_ImplDX9_Init(g_pd3dDevice))
        return false;

    return true;
}

void CleanupImGui()
{
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplXbox360_Shutdown();
    ImGui::DestroyContext();

    g_pd3dDevice = NULL;
}

void Render()
{
    // Start the Dear ImGui frame
    ImGui_ImplDX9_NewFrame();
    ImGui_ImplXbox360_NewFrame();
    ImGui::NewFrame();

    // Our state
    static bool show_demo_window = true;
    static bool show_another_window = false;
    static ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    // 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
    if (show_demo_window)
        ImGui::ShowDemoWindow(&show_demo_window);

    // 2. Show a simple window that we create ourselves. We use a Begin/End pair to created a named window.
    {
        static float f = 0.0f;
        static int counter = 0;

        ImGui::Begin("Hello, world!"); // Create a window called "Hello, world!" and append into it.

        ImGui::Text("This is some useful text.");          // Display some text (you can use a format strings too)
        ImGui::Checkbox("Demo Window", &show_demo_window); // Edit bools storing our window open/close state
        ImGui::Checkbox("Another Window", &show_another_window);

        ImGui::SliderFloat("float", &f, 0.0f, 1.0f);             // Edit 1 float using a slider from 0.0f to 1.0f
        ImGui::ColorEdit3("clear color", (float *)&clear_color); // Edit 3 floats representing a color

        if (ImGui::Button("Button")) // Buttons return true when clicked (most widgets return true when edited/activated)
            counter++;
        ImGui::SameLine();
        ImGui::Text("counter = %d", counter);

        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
        ImGui::End();
    }

    // 3. Show another simple window.
    if (show_another_window)
    {
        ImGui::Begin("Another Window", &show_another_window); // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
        ImGui::Text("Hello from another window!");
        if (ImGui::Button("Close Me"))
            show_another_window = false;
        ImGui::End();
    }

    // Render Dear ImGui
    ImGui::EndFrame();
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
}

HRESULT XuiRenderEndHook(HANDLE hDC)
{
    auto original = g_pXuiRenderEndDetour->GetOriginal<decltype(&XuiRenderEndHook)>();

    switch (g_ImGuiState)
    {
    case ImGuiState_RequiresInitialization: {
        if (!GetD3DDevice(hDC))
            break;

        if (!InitImGui())
            break;

        g_ImGuiState = ImGuiState_CanRender;

        break;
    }

    case ImGuiState_CanRender: {
        Render();
        break;
    }

    case ImGuiState_RequiresCleanup: {
        CleanupImGui();
        g_ImGuiState = ImGuiState_None;
        break;
    }
    }

    return original(hDC);
}

void InstallDashboardHook()
{
    // NOP breakpoint in xam!D3D::VALIDATE_DEVICE about the D3D version only for devkits
    if (IsDevkit())
        *(uint32_t *)0x819F4400 = 0x60000000;

    // dash.xex exports XuiRenderEnd so we get a pointer to it from there.
    // Calling GetModuleHandle("dash.xex") returns NULL so instead we use the global HANDLE
    // of the currently running title. If we're in this function, the currently running
    // title is the dashboard so this is safe to do.
    HANDLE dashHandle = *XexExecutableModuleHandle;
    void *pXuiRenderEnd = ResolveExport(dashHandle, 10405);
    if (pXuiRenderEnd == NULL)
        return;

    // Tell the render loop that ImGui needs to be initialized
    g_ImGuiState = ImGuiState_RequiresInitialization;

    // Place the hook
    g_pXuiRenderEndDetour = new Detour(pXuiRenderEnd, XuiRenderEndHook);
    g_pXuiRenderEndDetour->Install();
}

void RemoveDashboardHook()
{
    delete g_pXuiRenderEndDetour;
    g_pXuiRenderEndDetour = NULL;
}

uint32_t MonitorTitleId(void *pThreadParameter)
{
    uint32_t currentTitleId = 0;

    // Infinitely check the current game running
    while (g_Running)
    {
        uint32_t newTitleId = XamGetCurrentTitleId();

        // A new game was launched
        if (newTitleId != currentTitleId)
        {
            currentTitleId = newTitleId;

            // We explicitly don't tell the render loop that ImGui needs cleanup because,
            // when switching games, the system will automatically free the ImGui
            // allocations with the rest of the dashboard

            RemoveDashboardHook();

            // If the new running game is the dashboard, start everything
            if (newTitleId == DASH_TITLE_ID)
                InstallDashboardHook();
        }

        // Wait a little bit between each iteration to avoid hammering the system
        Sleep(500);
    }

    return 0;
}

BOOL DllMain(HINSTANCE hModule, DWORD reason, void *pReserved)
{
    static HANDLE threadHandle = INVALID_HANDLE_VALUE;

    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        // Run MonitorTitleId in separate thread
        ExCreateThread(&threadHandle, 0, nullptr, nullptr, reinterpret_cast<PTHREAD_START_ROUTINE>(MonitorTitleId), nullptr, 2);
        break;
    case DLL_PROCESS_DETACH:
        g_Running = false;

        // Tell the render loop that ImGui needs cleanup. If we're not on the dashboard
        // and the render loop is not running, this will just have no effect
        g_ImGuiState = ImGuiState_RequiresCleanup;

        // Wait for the run thread to finish
        WaitForSingleObject(threadHandle, INFINITE);
        CloseHandle(threadHandle);

        // Cleanup the previous state (does nothing if the hook was never initialized)
        RemoveDashboardHook();

        break;
    }

    return TRUE;
}
