#include "imgui_impl_dx9.h"

#include <xtl.h>
#include <imgui.h>

#include <cstdint>
#include <string>

// DirectX data
struct ImGui_ImplDX9_Data
{
    D3DDevice *pd3dDevice;
    D3DVertexDeclaration *pVD;
    D3DVertexBuffer *pVB;
    D3DIndexBuffer *pIB;
    D3DVertexShader *pVS;
    D3DPixelShader *pPS;
    D3DTexture *FontTexture;
    int VertexBufferSize;
    int IndexBufferSize;

    ImGui_ImplDX9_Data()
    {
        memset(this, 0, sizeof(*this));
        VertexBufferSize = 5000;
        IndexBufferSize = 10000;
    }
};

struct CUSTOMVERTEX
{
    float pos[3];
    D3DCOLOR col;
    float uv[2];
};

#ifdef IMGUI_USE_BGRA_PACKED_COLOR
    #define IMGUI_COL_TO_DX9_ARGB(_COL) (_COL)
#else
    #define IMGUI_COL_TO_DX9_ARGB(_COL) (((_COL) & 0xFF00FF00) | (((_COL) & 0xFF0000) >> 16) | (((_COL) & 0xFF) << 16))
#endif

void *ResolveFunction(const std::string &moduleName, uint32_t ordinal);

decltype(D3DDevice_CreateVertexShader) *pfnD3DDevice_CreateVertexShader = (decltype(D3DDevice_CreateVertexShader) *)0x820D7680;
decltype(D3DDevice_CreatePixelShader) *pfnD3DDevice_CreatePixelShader = (decltype(D3DDevice_CreatePixelShader) *)0x820D72A0;
decltype(D3DDevice_CreateVertexDeclaration) *pfnD3DDevice_CreateVertexDeclaration = (decltype(D3DDevice_CreateVertexDeclaration) *)0x820D7AB8;
decltype(D3DDevice_CreateTexture) *pfnD3DDevice_CreateTexture = (decltype(D3DDevice_CreateTexture) *)0x820E8B28;
decltype(D3DDevice_SetRenderState) *pfnD3DDevice_SetRenderState = (decltype(D3DDevice_SetRenderState) *)0x823F11A0;
decltype(D3DDevice_SetSamplerState) *pfnD3DDevice_SetSamplerState = (decltype(D3DDevice_SetSamplerState) *)0x823F11B8;
decltype(D3DDevice_SetVertexShader) *pfnD3DDevice_SetVertexShader = (decltype(D3DDevice_SetVertexShader) *)0x820D7770;
decltype(D3DDevice_SetPixelShader) *pfnD3DDevice_SetPixelShader = (decltype(D3DDevice_SetPixelShader) *)0x820D7390;
decltype(D3DDevice_SetVertexShaderConstantF) *pfnD3DDevice_SetVertexShaderConstantF = (decltype(D3DDevice_SetVertexShaderConstantF) *)0x823D19B8;
decltype(D3DDevice_SetVertexDeclaration) *pfnD3DDevice_SetVertexDeclaration = (decltype(D3DDevice_SetVertexDeclaration) *)0x820D7978;
decltype(D3DResource_Release) *pfnD3DResource_Release = (decltype(D3DResource_Release) *)0x820E5458;
decltype(D3DDevice_SetStreamSource_Inline) *pfnD3DDevice_SetStreamSource_Inline = (decltype(D3DDevice_SetStreamSource_Inline) *)0x823D1930;
decltype(D3DDevice_SetIndices) *pfnD3DDevice_SetIndices = (decltype(D3DDevice_SetIndices) *)0x820DD8D0;
decltype(D3DDevice_SetTexture_Inline) *pfnD3DDevice_SetTexture_Inline = (decltype(D3DDevice_SetTexture_Inline) *)0x823EDA30;
decltype(D3DDevice_SetScissorRect) *pfnD3DDevice_SetScissorRect = (decltype(D3DDevice_SetScissorRect) *)0x820DD5B0;
decltype(D3DDevice_DrawIndexedVertices) *pfnD3DDevice_DrawIndexedVertices = (decltype(D3DDevice_DrawIndexedVertices) *)0x820E6C88;
decltype(D3DTexture_LockRect) *pfnD3DTexture_LockRect = NULL;
decltype(D3DTexture_UnlockRect) *pfnD3DTexture_UnlockRect = NULL;
decltype(D3DDevice_CreateVertexBuffer) *pfnD3DDevice_CreateVertexBuffer = NULL;
decltype(D3DDevice_CreateIndexBuffer) *pfnD3DDevice_CreateIndexBuffer = NULL;
decltype(D3DVertexBuffer_Lock) *pfnD3DVertexBuffer_Lock = NULL;
decltype(D3DIndexBuffer_Lock) *pfnD3DIndexBuffer_Lock = NULL;
decltype(D3DVertexBuffer_Unlock) *pfnD3DVertexBuffer_Unlock = NULL;
decltype(D3DIndexBuffer_Unlock) *pfnD3DIndexBuffer_Unlock = NULL;

static void *LoadShaderCode(const char *shaderFileName)
{
    // Open the file for reading
    HANDLE fileHandle = CreateFile(shaderFileName, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (fileHandle == INVALID_HANDLE_VALUE)
        return NULL;

    // Get the file size
    size_t fileSize = GetFileSize(fileHandle, NULL);
    void *pFileData = ImGui::MemAlloc(fileSize);

    // Read the file
    size_t readBytes = 0;
    if (!ReadFile(fileHandle, pFileData, fileSize, (DWORD *)&readBytes, NULL))
    {
        CloseHandle(fileHandle);
        ImGui::MemFree(pFileData);
        return NULL;
    }

    // Finished reading file
    CloseHandle(fileHandle);

    if (readBytes != fileSize)
    {
        ImGui::MemFree(pFileData);
        return NULL;
    }

    return pFileData;
}

static ImGui_ImplDX9_Data *ImGui_ImplDX9_GetBackendData()
{
    return ImGui::GetCurrentContext() ? (ImGui_ImplDX9_Data *)ImGui::GetIO().BackendRendererUserData : NULL;
}

static bool ImGui_ImplDX9_LoadShaders()
{
    ImGui_ImplDX9_Data *bd = ImGui_ImplDX9_GetBackendData();

    // Load the vertex shader code
    void *pVertexShaderCode = LoadShaderCode("game:\\vertex.xvu");
    if (pVertexShaderCode == NULL)
    {
        OutputDebugStringA("Couldn't load vertex shader code.\n");
        return false;
    }

    // Create vertex shader
    bd->pVS = pfnD3DDevice_CreateVertexShader((DWORD *)pVertexShaderCode);
    ImGui::MemFree(pVertexShaderCode);

    // Load the pixel shader code
    void *pPixelShaderCode = LoadShaderCode("game:\\pixel.xpu");
    if (pPixelShaderCode == NULL)
    {
        OutputDebugStringA("Couldn't load pixel shader code.\n");
        return false;
    }

    // Create pixel shader
    bd->pPS = pfnD3DDevice_CreatePixelShader((DWORD *)pPixelShaderCode);
    ImGui::MemFree(pPixelShaderCode);

    return true;
}

static bool ImGui_ImplDX9_CreateVertexElements()
{
    ImGui_ImplDX9_Data *bd = ImGui_ImplDX9_GetBackendData();

    // Define the vertex elements
    static const D3DVERTEXELEMENT9 VertexElements[] = {
        { 0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
        { 0, sizeof(float) * 3, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0 },
        { 0, sizeof(float) * 3 + sizeof(D3DCOLOR), D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
        D3DDECL_END()
    };

    // Create a vertex declaration from the element descriptions
    bd->pVD = pfnD3DDevice_CreateVertexDeclaration(VertexElements);

    return true;
}

bool ImGui_ImplDX9_Init(IDirect3DDevice9 *device)
{
    ImGuiIO &io = ImGui::GetIO();
    IM_ASSERT(io.BackendRendererUserData == NULL && "Already initialized a renderer backend!");

    // Setup backend capabilities flags
    ImGui_ImplDX9_Data *bd = IM_NEW(ImGui_ImplDX9_Data)();
    io.BackendRendererUserData = (void *)bd;
    io.BackendRendererName = "imgui_impl_dx9";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset; // We can honor the ImDrawCmd::VtxOffset field, allowing for large meshes.

    bd->pd3dDevice = device;
    // bd->pd3dDevice->AddRef();

    pfnD3DTexture_LockRect = (decltype(pfnD3DTexture_LockRect))ResolveFunction("xam.xex", 2211);
    pfnD3DTexture_UnlockRect = (decltype(pfnD3DTexture_UnlockRect))ResolveFunction("xam.xex", 2210);
    pfnD3DDevice_CreateVertexBuffer = (decltype(pfnD3DDevice_CreateVertexBuffer))ResolveFunction("xam.xex", 2203);
    pfnD3DDevice_CreateIndexBuffer = (decltype(pfnD3DDevice_CreateIndexBuffer))ResolveFunction("xam.xex", 2204);
    pfnD3DVertexBuffer_Lock = (decltype(pfnD3DVertexBuffer_Lock))ResolveFunction("xam.xex", 2207);
    pfnD3DIndexBuffer_Lock = (decltype(pfnD3DIndexBuffer_Lock))ResolveFunction("xam.xex", 2209);
    pfnD3DVertexBuffer_Unlock = (decltype(pfnD3DVertexBuffer_Unlock))ResolveFunction("xam.xex", 2206);
    pfnD3DIndexBuffer_Unlock = (decltype(pfnD3DIndexBuffer_Unlock))ResolveFunction("xam.xex", 2208);

    // Load shaders
    if (!ImGui_ImplDX9_LoadShaders())
        return false;

    // Setup vertex layout
    if (!ImGui_ImplDX9_CreateVertexElements())
        return false;

    return true;
}

static bool ImGui_ImplDX9_CreateFontsTexture()
{
    // Build texture atlas
    ImGuiIO &io = ImGui::GetIO();
    ImGui_ImplDX9_Data *bd = ImGui_ImplDX9_GetBackendData();
    unsigned char *pixels;
    int width, height, bytes_per_pixel;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height, &bytes_per_pixel);

    // Convert RGBA32 to BGRA32 (because RGBA32 is not well supported by DX9 devices)
#ifndef IMGUI_USE_BGRA_PACKED_COLOR
    if (io.Fonts->TexPixelsUseColors)
    {
        ImU32 *dst_start = (ImU32 *)ImGui::MemAlloc((size_t)width * height * bytes_per_pixel);
        for (ImU32 *src = (ImU32 *)pixels, *dst = dst_start, *dst_end = dst_start + (size_t)width * height; dst < dst_end; src++, dst++)
            *dst = IMGUI_COL_TO_DX9_ARGB(*src);
        pixels = (unsigned char *)dst_start;
    }
#endif

    // Upload texture to graphics system
    bd->FontTexture = NULL;
    bd->FontTexture = (D3DTexture *)pfnD3DDevice_CreateTexture(width, height, 1, 1, 0, D3DFMT_LIN_A8R8G8B8, D3DPOOL_DEFAULT, D3DRTYPE_TEXTURE);
    if (bd->FontTexture == NULL)
        return false;
    D3DLOCKED_RECT tex_locked_rect;
    pfnD3DTexture_LockRect(bd->FontTexture, 0, &tex_locked_rect, NULL, 0);
    for (int y = 0; y < height; y++)
        memcpy((unsigned char *)tex_locked_rect.pBits + (size_t)tex_locked_rect.Pitch * y, pixels + (size_t)width * bytes_per_pixel * y, (size_t)width * bytes_per_pixel);
    pfnD3DTexture_UnlockRect(bd->FontTexture, 0);

    // Store our identifier
    io.Fonts->SetTexID((ImTextureID)bd->FontTexture);

#ifndef IMGUI_USE_BGRA_PACKED_COLOR
    if (io.Fonts->TexPixelsUseColors)
        ImGui::MemFree(pixels);
#endif

    return true;
}

static bool ImGui_ImplDX9_CreateDeviceObjects()
{
    ImGui_ImplDX9_Data *bd = ImGui_ImplDX9_GetBackendData();
    if (!bd || !bd->pd3dDevice)
        return false;
    if (!ImGui_ImplDX9_CreateFontsTexture())
        return false;
    return true;
}

void ImGui_ImplDX9_NewFrame()
{
    ImGui_ImplDX9_Data *bd = ImGui_ImplDX9_GetBackendData();
    IM_ASSERT(bd != NULL && "Did you call ImGui_ImplDX9_Init()?");

    if (!bd->FontTexture)
        ImGui_ImplDX9_CreateDeviceObjects();
}

static void ImGui_ImplDX9_SetupRenderState(ImDrawData *draw_data)
{
    ImGui_ImplDX9_Data *bd = ImGui_ImplDX9_GetBackendData();

    // Setup render state: alpha-blending, no face culling, no depth testing
    pfnD3DDevice_SetRenderState(bd->pd3dDevice, D3DRS_FILLMODE, D3DFILL_SOLID);
    pfnD3DDevice_SetRenderState(bd->pd3dDevice, D3DRS_ZWRITEENABLE, FALSE);
    pfnD3DDevice_SetRenderState(bd->pd3dDevice, D3DRS_ALPHATESTENABLE, FALSE);
    pfnD3DDevice_SetRenderState(bd->pd3dDevice, D3DRS_CULLMODE, D3DCULL_NONE);
    pfnD3DDevice_SetRenderState(bd->pd3dDevice, D3DRS_ZENABLE, FALSE);
    pfnD3DDevice_SetRenderState(bd->pd3dDevice, D3DRS_ALPHABLENDENABLE, TRUE);
    pfnD3DDevice_SetRenderState(bd->pd3dDevice, D3DRS_BLENDOP, D3DBLENDOP_ADD);
    pfnD3DDevice_SetRenderState(bd->pd3dDevice, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    pfnD3DDevice_SetRenderState(bd->pd3dDevice, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    pfnD3DDevice_SetRenderState(bd->pd3dDevice, D3DRS_SEPARATEALPHABLENDENABLE, TRUE);
    pfnD3DDevice_SetRenderState(bd->pd3dDevice, D3DRS_SRCBLENDALPHA, D3DBLEND_ONE);
    pfnD3DDevice_SetRenderState(bd->pd3dDevice, D3DRS_DESTBLENDALPHA, D3DBLEND_INVSRCALPHA);
    pfnD3DDevice_SetRenderState(bd->pd3dDevice, D3DRS_SCISSORTESTENABLE, TRUE);
    pfnD3DDevice_SetRenderState(bd->pd3dDevice, D3DRS_STENCILENABLE, FALSE);
    pfnD3DDevice_SetSamplerState(bd->pd3dDevice, 0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    pfnD3DDevice_SetSamplerState(bd->pd3dDevice, 0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);

    float L = draw_data->DisplayPos.x + 0.5f;
    float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x + 0.5f;
    float T = draw_data->DisplayPos.y + 0.5f;
    float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y + 0.5f;
    XMMATRIX mat_world = XMMatrixIdentity();
    XMMATRIX mat_view = XMMatrixIdentity();
    XMMATRIX mat_proj = XMMatrixOrthographicOffCenterLH(L, R, B, T, -1.0f, 1.0f);
    XMMATRIX mat_wvp = mat_world * mat_view * mat_proj;

    pfnD3DDevice_SetVertexShader(bd->pd3dDevice, bd->pVS);
    pfnD3DDevice_SetPixelShader(bd->pd3dDevice, bd->pPS);
    pfnD3DDevice_SetVertexShaderConstantF(bd->pd3dDevice, 0, (float *)&mat_wvp, 4);
    pfnD3DDevice_SetVertexDeclaration(bd->pd3dDevice, bd->pVD);
}

// Render function
void ImGui_ImplDX9_RenderDrawData(ImDrawData *draw_data)
{
    // Create and grow buffers if needed
    ImGui_ImplDX9_Data *bd = ImGui_ImplDX9_GetBackendData();
    if (!bd->pVB || bd->VertexBufferSize < draw_data->TotalVtxCount)
    {
        if (bd->pVB)
        {
            pfnD3DResource_Release(bd->pVB);
            bd->pVB = NULL;
        }
        bd->VertexBufferSize = draw_data->TotalVtxCount + 5000;
        bd->pVB = pfnD3DDevice_CreateVertexBuffer(bd->VertexBufferSize * sizeof(CUSTOMVERTEX), D3DUSAGE_WRITEONLY, D3DPOOL_DEFAULT);
        if (bd->pVB == NULL)
            return;
    }
    if (!bd->pIB || bd->IndexBufferSize < draw_data->TotalIdxCount)
    {
        if (bd->pIB)
        {
            pfnD3DResource_Release(bd->pIB);
            bd->pIB = NULL;
        }
        bd->IndexBufferSize = draw_data->TotalIdxCount + 10000;
        bd->pIB = pfnD3DDevice_CreateIndexBuffer(bd->IndexBufferSize * sizeof(ImDrawIdx), D3DUSAGE_WRITEONLY, sizeof(ImDrawIdx) == 2 ? D3DFMT_INDEX16 : D3DFMT_INDEX32, D3DPOOL_DEFAULT);
        if (bd->pIB == NULL)
            return;
    }

    // Backup the DX9 state
    IDirect3DStateBlock9 *d3d9_state_block = NULL;
    // TODO:
    if (bd->pd3dDevice->CreateStateBlock(D3DSBT_ALL, &d3d9_state_block) < 0)
        return;
    // TODO:
    if (d3d9_state_block->Capture() < 0)
    {
        // TODO:
        d3d9_state_block->Release();
        return;
    }

    // Allocate buffers
    CUSTOMVERTEX *vtx_dst;
    ImDrawIdx *idx_dst;
    vtx_dst = (CUSTOMVERTEX *)pfnD3DVertexBuffer_Lock(bd->pVB, 0, (UINT)(draw_data->TotalVtxCount * sizeof(CUSTOMVERTEX)), 0);
    if (vtx_dst == NULL)
    {
        // TODO:
        d3d9_state_block->Release();
        return;
    }
    idx_dst = (ImDrawIdx *)pfnD3DIndexBuffer_Lock(bd->pIB, 0, (UINT)(draw_data->TotalIdxCount * sizeof(ImDrawIdx)), 0);
    if (idx_dst == NULL)
    {
        pfnD3DVertexBuffer_Unlock(bd->pVB);
        // TODO:
        d3d9_state_block->Release();
        return;
    }

    // Copy and convert all vertices into a single contiguous buffer, convert colors to DX9 default format.
    // FIXME-OPT: This is a minor waste of resource, the ideal is to use imconfig.h and
    //  1) to avoid repacking colors:   #define IMGUI_USE_BGRA_PACKED_COLOR
    //  2) to avoid repacking vertices: #define IMGUI_OVERRIDE_DRAWVERT_STRUCT_LAYOUT struct ImDrawVert { ImVec2 pos; float z; ImU32 col; ImVec2 uv; }
    for (int n = 0; n < draw_data->CmdListsCount; n++)
    {
        const ImDrawList *cmd_list = draw_data->CmdLists[n];
        const ImDrawVert *vtx_src = cmd_list->VtxBuffer.Data;
        for (int i = 0; i < cmd_list->VtxBuffer.Size; i++)
        {
            vtx_dst->pos[0] = vtx_src->pos.x;
            vtx_dst->pos[1] = vtx_src->pos.y;
            vtx_dst->pos[2] = 0.0f;
            vtx_dst->col = IMGUI_COL_TO_DX9_ARGB(vtx_src->col);
            vtx_dst->uv[0] = vtx_src->uv.x;
            vtx_dst->uv[1] = vtx_src->uv.y;
            vtx_dst++;
            vtx_src++;
        }
        memcpy(idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
        idx_dst += cmd_list->IdxBuffer.Size;
    }
    pfnD3DVertexBuffer_Unlock(bd->pVB);
    pfnD3DIndexBuffer_Unlock(bd->pIB);
    pfnD3DDevice_SetStreamSource_Inline(bd->pd3dDevice, 0, bd->pVB, 0, sizeof(CUSTOMVERTEX));
    pfnD3DDevice_SetIndices(bd->pd3dDevice, bd->pIB);

    // Setup desired DX state
    ImGui_ImplDX9_SetupRenderState(draw_data);

    // Render command lists
    // (Because we merged all buffers into a single one, we maintain our own offset into them)
    int global_vtx_offset = 0;
    int global_idx_offset = 0;
    ImVec2 clip_off = draw_data->DisplayPos;
    for (int n = 0; n < draw_data->CmdListsCount; n++)
    {
        const ImDrawList *cmd_list = draw_data->CmdLists[n];
        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
        {
            const ImDrawCmd *pcmd = &cmd_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback != NULL)
            {
                // User callback, registered via ImDrawList::AddCallback()
                // (ImDrawCallback_ResetRenderState is a special callback value used by the user to request the renderer to reset render state.)
                if (pcmd->UserCallback == ImDrawCallback_ResetRenderState)
                    ImGui_ImplDX9_SetupRenderState(draw_data);
                else
                    pcmd->UserCallback(cmd_list, pcmd);
            }
            else
            {
                // Project scissor/clipping rectangles into framebuffer space
                ImVec2 clip_min(pcmd->ClipRect.x - clip_off.x, pcmd->ClipRect.y - clip_off.y);
                ImVec2 clip_max(pcmd->ClipRect.z - clip_off.x, pcmd->ClipRect.w - clip_off.y);
                if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
                    continue;

                // Apply Scissor/clipping rectangle, Bind texture, Draw
                const RECT r = { (LONG)clip_min.x, (LONG)clip_min.y, (LONG)clip_max.x, (LONG)clip_max.y };
                const LPDIRECT3DTEXTURE9 texture = (LPDIRECT3DTEXTURE9)pcmd->GetTexID();
                pfnD3DDevice_SetTexture_Inline(bd->pd3dDevice, 0, texture);
                pfnD3DDevice_SetScissorRect(bd->pd3dDevice, &r);
                pfnD3DDevice_DrawIndexedVertices(bd->pd3dDevice, D3DPT_TRIANGLELIST, pcmd->VtxOffset + global_vtx_offset, pcmd->IdxOffset + global_idx_offset, pcmd->ElemCount / 3);
            }
        }
        global_idx_offset += cmd_list->IdxBuffer.Size;
        global_vtx_offset += cmd_list->VtxBuffer.Size;
    }

    // Restore the DX9 state
    // TODO:
    d3d9_state_block->Apply();
    d3d9_state_block->Release();
}
