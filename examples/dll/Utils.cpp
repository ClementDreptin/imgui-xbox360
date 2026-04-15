#include "Utils.h"

void *ResolveExport(const char *moduleName, uint32_t ordinal)
{
    HMODULE moduleHandle = GetModuleHandle(moduleName);
    if (moduleHandle == NULL)
        return NULL;

    return ResolveExport(moduleHandle, ordinal);
}

void *ResolveExport(HANDLE moduleHandle, uint32_t ordinal)
{
    return GetProcAddress(static_cast<HMODULE>(moduleHandle), reinterpret_cast<const char *>(ordinal));
}

bool IsDevkit()
{
    return !(*reinterpret_cast<uint32_t *>(0x8E038610) & (1 << 15));
}

// https://gist.github.com/iMoD1998/4aa48d5c990535767a3fc3251efc0348
// clang-format off
BYTE   Detour::TrampolineBuffer[ 200 * 20 ] = {};
SIZE_T Detour::TrampolineSize = 0;
// clang-format on
