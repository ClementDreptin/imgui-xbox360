#include <xtl.h>
#include <imgui.h>

#include <cstdint>
#include <string>

#include "imgui_impl_xbox360.h"
#include "imgui_impl_dx9.h"

// Get the address of a function from a module by its ordinal
void *ResolveFunction(const std::string &moduleName, uint32_t ordinal)
{
    HMODULE moduleHandle = GetModuleHandle(moduleName.c_str());
    if (moduleHandle == nullptr)
        return nullptr;

    return GetProcAddress(moduleHandle, reinterpret_cast<const char *>(ordinal));
}

// Create a pointer to XNotifyQueueUI in xam.xex
typedef void (*XNOTIFYQUEUEUI)(uint32_t type, uint32_t userIndex, uint64_t areas, const wchar_t *displayText, void *pContextData);
XNOTIFYQUEUEUI XNotifyQueueUI = static_cast<XNOTIFYQUEUEUI>(ResolveFunction("xam.xex", 656));

// Enum for game title IDs
typedef enum _TitleId
{
    Title_Dashboard = 0xFFFE07D1,
    Title_MW2 = 0x41560817,
} TitleId;

// Imports from the Xbox libraries
extern "C"
{
    uint32_t XamGetCurrentTitleId();

    uint32_t ExCreateThread(
        HANDLE *pHandle,
        uint32_t stackSize,
        uint32_t *pThreadId,
        void *pApiThreadStartup,
        PTHREAD_START_ROUTINE pStartAddress,
        void *pParameter,
        uint32_t creationFlags
    );

    bool MmIsAddressValid(void *pAddress);
}

void InitMW2();

bool g_Running = true;

// Infinitely check the current game running
uint32_t MonitorTitleId(void *pThreadParameter)
{
    uint32_t currentTitleId = 0;

    while (g_Running)
    {
        uint32_t newTitleId = XamGetCurrentTitleId();

        if (newTitleId == currentTitleId)
            continue;

        currentTitleId = newTitleId;

        switch (newTitleId)
        {
        case Title_Dashboard:
            XNotifyQueueUI(0, 0, XNOTIFY_SYSTEM, L"Dashboard", nullptr);
            break;
        case Title_MW2:
            if (!strcmp(reinterpret_cast<char *>(0x82001D38), "multiplayer"))
                InitMW2();
            break;
        }
    }

    return 0;
}

#define MAX_HOOK_COUNT 100
#define NUM_INSTRUCTIONS_IN_JUMP 4
#define POWERPC_B 0x48
#define POWERPC_BL 0x4B

class Detour
{
public:
    Detour(void *pSource, const void *pDestination)
        : m_pSource(pSource), m_pDestination(pDestination), m_HookIndex(static_cast<size_t>(-1))
    {
    }

    Detour(uintptr_t sourceAddress, const void *pDestination)
        : m_pSource(reinterpret_cast<void *>(sourceAddress)), m_pDestination(pDestination), m_HookIndex(static_cast<size_t>(-1))
    {
    }

    ~Detour()
    {
        Remove();
    }

    HRESULT Install()
    {
        if (s_HookCount >= MAX_HOOK_COUNT || m_pSource == nullptr || m_pDestination == nullptr)
            return E_FAIL;

        if (s_CriticalSection.Synchronization.RawEvent[0] == 0)
            InitializeCriticalSection(&s_CriticalSection);

        EnterCriticalSection(&s_CriticalSection);

        // Keep track of where the stub of the current instance is in s_StubSection
        m_HookIndex = s_HookCount;

        // Copy the original instructions at m_pSource before hooking to be able to
        // restore them later
        memcpy(&m_Original, m_pSource, sizeof(m_Original));

        DetourFunctionStart();

        s_HookCount++;

        LeaveCriticalSection(&s_CriticalSection);

        return S_OK;
    }

    void Remove()
    {
        // Restore the original instructions if needed
        if (m_HookIndex != -1 && m_pSource != nullptr && MmIsAddressValid(m_pSource))
            memcpy(m_pSource, &m_Original, sizeof(m_Original));

        m_pSource = nullptr;
        m_pDestination = nullptr;
        m_HookIndex = 0;
        m_Original = Jump();
    }

    template<typename T>
    inline T GetOriginal() const
    {
        return reinterpret_cast<T>(&s_StubSection[m_HookIndex]);
    }

private:
    typedef uint32_t POWERPC_INSTRUCTION;
    typedef uint8_t POWERPC_INSTRUCTION_TYPE;

    struct Stub
    {
        POWERPC_INSTRUCTION Instructions[20]; // DetourFunctionStart can copy up to 20 instructions

        Stub() { ZeroMemory(&Instructions, sizeof(Instructions)); }
    };

    struct Jump
    {
        POWERPC_INSTRUCTION Instructions[NUM_INSTRUCTIONS_IN_JUMP];

        Jump() { ZeroMemory(&Instructions, sizeof(Instructions)); }
    };

    void *m_pSource;
    const void *m_pDestination;
    size_t m_HookIndex;
    Jump m_Original;

    static Stub s_StubSection[MAX_HOOK_COUNT];
    static size_t s_HookCount;
    static CRITICAL_SECTION s_CriticalSection;

    void DetourFunctionStart()
    {
        POWERPC_INSTRUCTION *pSource = static_cast<POWERPC_INSTRUCTION *>(m_pSource);
        POWERPC_INSTRUCTION *pStub = reinterpret_cast<POWERPC_INSTRUCTION *>(&s_StubSection[m_HookIndex]);
        size_t instructionCount = 0;

        for (size_t i = 0; i < NUM_INSTRUCTIONS_IN_JUMP; i++)
        {
            POWERPC_INSTRUCTION instruction = pSource[i];
            POWERPC_INSTRUCTION_TYPE instructionType = *reinterpret_cast<POWERPC_INSTRUCTION_TYPE *>(&pSource[i]);

            // If the function op code is null, it's invalid
            if (instruction == 0)
                break;

            // If the instruction is a branch
            if (instructionType == POWERPC_B || instructionType == POWERPC_BL)
            {
                // Get a pointer to where the branch goes
                void *pBranchDestination = ResolveBranch(instruction, &pSource[i]);
                bool linked = (instruction & 1) != 0;

                // Jump from the stub to where the branch goes
                PatchInJump(&pStub[instructionCount], pBranchDestination, linked);
                instructionCount += NUM_INSTRUCTIONS_IN_JUMP;

                // If it was a branch to a different section of the same function (b loc_),
                // we won't need to add anything else to the stub
                if (!linked)
                {
                    PatchInJump(pSource, m_pDestination, false);
                    return;
                }
            }
            // Otherwise, just copy the instruction to the stub
            else
            {
                pStub[instructionCount] = instruction;
                instructionCount++;
            }
        }

        // Make the stub call the original function
        PatchInJump(&pStub[instructionCount], &pSource[NUM_INSTRUCTIONS_IN_JUMP], false);

        // Make the original function call the stub
        PatchInJump(pSource, m_pDestination, false);
    }

    void PatchInJump(void *pSource, const void *pDestination, bool linked)
    {
        Jump jump;
        uintptr_t destinationAddress = reinterpret_cast<uintptr_t>(pDestination);

        jump.Instructions[0] = 0x3C000000 + (destinationAddress >> 16);    // lis    %r0, dest>>16
        jump.Instructions[1] = 0x60000000 + (destinationAddress & 0xFFFF); // ori    %r0, %r0, dest&0xFFFF
        jump.Instructions[2] = 0x7C0903A6;                                 // mtctr  %r0
        jump.Instructions[3] = 0x4E800420 + (linked ? 1 : 0);              // bctr/bctrl

        memcpy(pSource, &jump, sizeof(jump));

        __dcbst(0, pSource);
        __sync();
        __emit(0x4C00012C);
    }

    void *ResolveBranch(POWERPC_INSTRUCTION instruction, const void *pBranch)
    {
        // Taken from here
        // https://github.com/skiff/libpsutil/blob/master/libpsutil/system/memory.cpp#L90

        uintptr_t offset = instruction & 0x3FFFFFC;

        if (offset & (1 << 25))
            offset |= 0xFC000000;

        return reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(pBranch) + offset);
    }
};

// This will hold all the instructions for all hooks. This needs to be a static buffer because,
// if it was a class member, it would be allocated on the stack and stack memory isn't executable
Detour::Stub Detour::s_StubSection[MAX_HOOK_COUNT];
size_t Detour::s_HookCount = 0;
CRITICAL_SECTION Detour::s_CriticalSection = {};

Direct3D *g_pD3D = NULL;
D3DDevice *g_pd3dDevice = NULL;

void CreateD3DDevice()
{
    // Create the D3D object
    // That's what Direct3DCreate9 does under the hood
    g_pD3D = (Direct3D *)1;

    // Get a pointer to the device from the game
    g_pd3dDevice = *(D3DDevice **)(0x83A23218 + 0x8);
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
    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    // g_pd3dDevice->Present(NULL, NULL, NULL, NULL);
}

Detour *pSCR_DrawScreenFieldDetour = nullptr;

void SCR_DrawScreenFieldHook(const int localClientNum, int refreshedUI)
{
    // Calling the original SCR_DrawScreenField function
    pSCR_DrawScreenFieldDetour->GetOriginal<decltype(&SCR_DrawScreenFieldHook)>()(localClientNum, refreshedUI);

    // Render ImGui
    Render();
}

// Sets up the hook
void InitMW2()
{
    // Waiting a little bit for the game to be fully loaded in memory
    Sleep(200);

    CreateD3DDevice();

    if (!InitImGui())
        return;

    XNotifyQueueUI(0, 0, XNOTIFY_SYSTEM, L"Alpha MW2", nullptr);

    const uintptr_t SCR_DrawScreenFieldAddr = 0x8218B5F0;

    // Hooking SV_ExecuteClientCommand and SCR_DrawScreenField
    pSCR_DrawScreenFieldDetour = new Detour(SCR_DrawScreenFieldAddr, SCR_DrawScreenFieldHook);
    pSCR_DrawScreenFieldDetour->Install();
}

HANDLE g_ThreadHandle = INVALID_HANDLE_VALUE;

BOOL DllMain(HINSTANCE hModule, DWORD reason, void *pReserved)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        // Run MonitorTitleId in separate thread
        ExCreateThread(&g_ThreadHandle, 0, nullptr, nullptr, reinterpret_cast<PTHREAD_START_ROUTINE>(MonitorTitleId), nullptr, 2);
        break;
    case DLL_PROCESS_DETACH:
        g_Running = false;

        if (pSCR_DrawScreenFieldDetour)
            delete pSCR_DrawScreenFieldDetour;

        // Wait for the run thread to finish
        WaitForSingleObject(g_ThreadHandle, INFINITE);
        CloseHandle(g_ThreadHandle);
        break;
    }

    return TRUE;
}
