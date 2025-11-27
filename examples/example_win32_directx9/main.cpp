// Dear ImGui: standalone example application for Windows API + DirectX 9

#include "../../imgui.h"
#include "../../imgui_internal.h"
#include "../../backends/imgui_impl_dx9.h"
#include "../../backends/imgui_impl_win32.h"
#include <d3d9.h>
#include <tchar.h>
#include <cmath>
#include <unordered_map>

// Smooth scrolling settings
static const float g_ScrollMultiplier = 50.0f;     // Scroll speed multiplier
static const float g_ScrollSmoothing = 8.0f;       // Scroll decay speed
static const float g_BounceStrength = 0.15f;       // Bounce elasticity
static const float g_BounceDecay = 12.0f;          // Bounce return speed
static const float g_MaxOverscroll = 60.0f;        // Max overscroll pixels

// Per-window scroll state
struct SmoothScrollState {
    float velocity = 0.0f;
    float overscroll = 0.0f;
    float target_scroll = 0.0f;
    float current_scroll = 0.0f;
    float grab_anim = 0.0f;
    float alpha = 0.0f;
};

static std::unordered_map<ImGuiID, SmoothScrollState> g_ScrollStates;

// Easing helper
inline float EaseOut(float current, float target, float speed, float dt) {
    return current + (target - current) * (1.0f - std::exp(-speed * dt));
}

// Apply smooth scroll with bounce
void ApplySmoothScroll(ImGuiWindow* window, float wheel_delta, float dt)
{
    if (!window || window->ScrollMax.y <= 0.0f) return;
    
    ImGuiID id = window->ID;
    SmoothScrollState& state = g_ScrollStates[id];
    
    // Initialize on first use
    if (state.current_scroll == 0.0f && state.target_scroll == 0.0f) {
        state.current_scroll = window->Scroll.y;
        state.target_scroll = window->Scroll.y;
    }
    
    // Add wheel input to velocity
    if (wheel_delta != 0.0f) {
        state.velocity += wheel_delta * g_ScrollMultiplier;
    }
    
    // Apply velocity to target scroll
    if (std::abs(state.velocity) > 0.1f) {
        state.target_scroll -= state.velocity * dt;
        
        // Check bounds
        bool hit_top = state.target_scroll < 0.0f;
        bool hit_bottom = state.target_scroll > window->ScrollMax.y;
        
        if (hit_top || hit_bottom) {
            if (std::abs(state.velocity) > 50.0f) {
                if (hit_top) {
                    state.overscroll = ImClamp(state.target_scroll, -g_MaxOverscroll, 0.0f);
                } else {
                    state.overscroll = ImClamp(state.target_scroll - window->ScrollMax.y, 0.0f, g_MaxOverscroll);
                }
                state.velocity *= -g_BounceStrength;
            } else {
                state.velocity = 0.0f;
            }
            state.target_scroll = ImClamp(state.target_scroll, 0.0f, window->ScrollMax.y);
        }
        
        // Decay velocity
        state.velocity = EaseOut(state.velocity, 0.0f, g_ScrollSmoothing, dt);
        
        if (std::abs(state.velocity) < 0.5f)
            state.velocity = 0.0f;
    }
    
    // Return from overscroll
    if (std::abs(state.overscroll) > 0.1f) {
        state.overscroll = EaseOut(state.overscroll, 0.0f, g_BounceDecay, dt);
    } else {
        state.overscroll = 0.0f;
    }
    
    // Smooth interpolation to target
    state.current_scroll = EaseOut(state.current_scroll, state.target_scroll, 15.0f, dt);
    
    // Apply to window
    window->Scroll.y = state.current_scroll;
}

// Custom scrollbar renderer
void RenderSmoothScrollbar(ImGuiWindow* window)
{
    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    
    if (window->ScrollMax.y <= 0.0f) return;
    
    SmoothScrollState& state = g_ScrollStates[window->ID];
    
    // Scrollbar dimensions
    float scrollbar_width = style.ScrollbarSize;
    float padding = 2.0f;
    ImRect bb(
        window->Pos.x + window->Size.x - scrollbar_width,
        window->Pos.y + window->TitleBarHeight(),
        window->Pos.x + window->Size.x,
        window->Pos.y + window->Size.y
    );
    
    float scrollbar_height = bb.GetHeight();
    if (scrollbar_height <= 0.0f) return;
    
    // Calculate grab size
    float win_size = window->InnerRect.GetHeight();
    float content_size = window->ContentSize.y + style.WindowPadding.y * 2.0f;
    float grab_size_norm = ImClamp(win_size / content_size, 0.05f, 1.0f);
    float grab_size_pixels = ImMax(scrollbar_height * grab_size_norm, style.GrabMinSize);
    
    // Calculate grab position
    float scroll_ratio = ImSaturate(window->Scroll.y / window->ScrollMax.y);
    float grab_pos_target = scroll_ratio * (scrollbar_height - grab_size_pixels);
    
    // Apply overscroll offset
    float overscroll_offset = state.overscroll * 0.2f;
    grab_pos_target = ImClamp(grab_pos_target - overscroll_offset, 0.0f, scrollbar_height - grab_size_pixels);
    
    // Animate
    state.grab_anim = EaseOut(state.grab_anim, grab_pos_target, 15.0f, g.IO.DeltaTime);
    state.alpha = EaseOut(state.alpha, 1.0f, 8.0f, g.IO.DeltaTime);
    
    // Grab rect
    ImRect grab_rect(
        bb.Min.x + padding,
        bb.Min.y + state.grab_anim,
        bb.Max.x - padding,
        bb.Min.y + state.grab_anim + grab_size_pixels
    );
    
    grab_rect.Min.y = ImMax(grab_rect.Min.y, bb.Min.y);
    grab_rect.Max.y = ImMin(grab_rect.Max.y, bb.Max.y);
    
    // Colors
    bool hovered = bb.Contains(g.IO.MousePos);
    float hover_alpha = hovered ? 1.0f : 0.6f;
    ImU32 bg_col = ImGui::GetColorU32(ImGuiCol_ScrollbarBg, state.alpha * 0.4f);
    ImU32 grab_col = ImGui::GetColorU32(ImGuiCol_ScrollbarGrab, state.alpha * hover_alpha);
    
    // Draw
    ImDrawList* draw_list = ImGui::GetForegroundDrawList();
    draw_list->AddRectFilled(bb.Min, bb.Max, bg_col, style.ScrollbarRounding);
    draw_list->AddRectFilled(grab_rect.Min, grab_rect.Max, grab_col, 3.0f);
}

// Data
static LPDIRECT3D9              g_pD3D = nullptr;
static LPDIRECT3DDEVICE9        g_pd3dDevice = nullptr;
static bool                     g_DeviceLost = false;
static UINT                     g_ResizeWidth = 0, g_ResizeHeight = 0;
static D3DPRESENT_PARAMETERS    g_d3dpp = {};

// Forward declarations
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void ResetDevice();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Main code
int main(int, char**)
{
    ImGui_ImplWin32_EnableDpiAwareness();
    float main_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"ImGui Example", nullptr };
    ::RegisterClassExW(&wc);
    int screen_width = ::GetSystemMetrics(SM_CXSCREEN);
    int screen_height = ::GetSystemMetrics(SM_CYSCREEN);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"Dear ImGui DirectX9 Example", WS_POPUP, 0, 0, screen_width, screen_height, nullptr, nullptr, wc.hInstance, nullptr);

    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);
    style.FontScaleDpi = main_scale;

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX9_Init(g_pd3dDevice);

    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    // Main loop
    bool done = false;
    while (!done)
    {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        if (g_DeviceLost)
        {
            HRESULT hr = g_pd3dDevice->TestCooperativeLevel();
            if (hr == D3DERR_DEVICELOST)
            {
                ::Sleep(10);
                continue;
            }
            if (hr == D3DERR_DEVICENOTRESET)
                ResetDevice();
            g_DeviceLost = false;
        }

        if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
        {
            g_d3dpp.BackBufferWidth = g_ResizeWidth;
            g_d3dpp.BackBufferHeight = g_ResizeHeight;
            g_ResizeWidth = g_ResizeHeight = 0;
            ResetDevice();
        }

        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Scroll Test Window
        {
            ImVec2 window_size(1200, 1050);
            ImVec2 window_pos((io.DisplaySize.x - window_size.x) * 0.5f, (io.DisplaySize.y - window_size.y) * 0.5f);
            ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(window_size, ImGuiCond_Always);
            
            // ВАЖНО: убрали ImGuiWindowFlags_NoScrollbar, используем стандартный скроллинг
            ImGui::Begin("Scroll Tester", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
            
            ImGuiWindow* window = ImGui::GetCurrentWindow();
            SmoothScrollState& scroll_state = g_ScrollStates[window->ID];
            
            ImGui::Text("Velocity: %.2f", scroll_state.velocity);
            ImGui::Text("Overscroll: %.2f", scroll_state.overscroll);
            ImGui::Text("Current Scroll: %.2f", scroll_state.current_scroll);
            ImGui::Text("Scroll Max: %.2f", window->ScrollMax.y);
            ImGui::Text("FPS: %.1f", io.Framerate);
            ImGui::Separator();
            
            // Контент для прокрутки
            for (int i = 1; i <= 200; i++)
                ImGui::Text("Tester %d", i);
            
            // Применяем плавную прокрутку
            ApplySmoothScroll(window, io.MouseWheel, io.DeltaTime);
            
            // Рендерим кастомный скроллбар поверх стандартного
            RenderSmoothScrollbar(window);
            
            ImGui::End();
        }

        // Rendering
        ImGui::EndFrame();
        g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        D3DCOLOR clear_col_dx = D3DCOLOR_RGBA((int)(clear_color.x*clear_color.w*255.0f), (int)(clear_color.y*clear_color.w*255.0f), (int)(clear_color.z*clear_color.w*255.0f), (int)(clear_color.w*255.0f));
        g_pd3dDevice->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, clear_col_dx, 1.0f, 0);
        if (g_pd3dDevice->BeginScene() >= 0)
        {
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
            g_pd3dDevice->EndScene();
        }
        HRESULT result = g_pd3dDevice->Present(nullptr, nullptr, nullptr, nullptr);
        if (result == D3DERR_DEVICELOST)
            g_DeviceLost = true;
    }

    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

// Helper functions
bool CreateDeviceD3D(HWND hWnd)
{
    if ((g_pD3D = Direct3DCreate9(D3D_SDK_VERSION)) == nullptr)
        return false;

    ZeroMemory(&g_d3dpp, sizeof(g_d3dpp));
    g_d3dpp.Windowed = TRUE;
    g_d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    g_d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;
    g_d3dpp.EnableAutoDepthStencil = TRUE;
    g_d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
    g_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    if (g_pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &g_d3dpp, &g_pd3dDevice) < 0)
        return false;

    return true;
}

void CleanupDeviceD3D()
{
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
    if (g_pD3D) { g_pD3D->Release(); g_pD3D = nullptr; }
}

void ResetDevice()
{
    ImGui_ImplDX9_InvalidateDeviceObjects();
    HRESULT hr = g_pd3dDevice->Reset(&g_d3dpp);
    if (hr == D3DERR_INVALIDCALL)
        IM_ASSERT(0);
    ImGui_ImplDX9_CreateDeviceObjects();
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam);
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}