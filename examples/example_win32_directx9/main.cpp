// Dear ImGui: standalone example application for Windows API + DirectX 9

// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

#include "../../imgui.h"
#include "../../imgui_internal.h"
#include "../../backends/imgui_impl_dx9.h"
#include "../../backends/imgui_impl_win32.h"
#include <d3d9.h>
#include <tchar.h>
#include <cmath>
#include <unordered_map>

// Smooth scrolling settings
static const float g_ScrollMultiplier = 15.0f;     // Scroll speed multiplier (higher = faster)
static const float g_ScrollSmoothing = 6.0f;       // Scroll decay speed (lower = longer glide)
static const float g_BounceStrength = 0.3f;        // Bounce elasticity (0-1)
static const float g_BounceDecay = 10.0f;          // Bounce return speed
static const float g_MaxOverscroll = 80.0f;        // Max overscroll pixels

// Per-window scroll state
struct SmoothScrollState {
    float velocity = 0.0f;          // Current scroll velocity
    float overscroll_target = 0.0f; // Target overscroll amount
    float overscroll_visual = 0.0f; // Animated visual overscroll (smooth)
    float grab_anim = 0.0f;         // Animated scrollbar grab position
    float alpha = 0.0f;             // Scrollbar fade
};

static std::unordered_map<ImGuiID, SmoothScrollState> g_ScrollStates;
static float g_ScrollWheelAccum = 0.0f;
static ImGuiID g_ActiveScrollWindow = 0;

// Easing helpers
inline float EaseOut(float current, float target, float speed, float dt) {
    return current + (target - current) * (1.0f - std::exp(-speed * dt));
}

// Apply smooth scroll with bounce to a window
// overscroll_target > 0 means content pushed DOWN (at top boundary)
// overscroll_target < 0 means content pushed UP (at bottom boundary)
void ApplySmoothScroll(ImGuiWindow* window, float wheel_delta, float dt)
{
    if (!window || window->ScrollMax.y <= 0.0f) return;
    
    ImGuiID id = window->ID;
    SmoothScrollState& state = g_ScrollStates[id];
    
    bool at_top = window->Scroll.y <= 0.0f;
    bool at_bottom = window->Scroll.y >= window->ScrollMax.y;
    
    // Handle wheel input
    if (wheel_delta != 0.0f) {
        bool trying_scroll_up = wheel_delta > 0.0f;   // wheel up = want to see content above
        bool trying_scroll_down = wheel_delta < 0.0f; // wheel down = want to see content below
        
        // At top and trying to scroll up more - bounce down (positive)
        if (at_top && trying_scroll_up) {
            state.overscroll_target += wheel_delta * g_ScrollMultiplier * 3.0f;
            state.overscroll_target = ImMin(state.overscroll_target, g_MaxOverscroll);
            state.velocity = 0.0f;
        }
        // At bottom and trying to scroll down more - bounce up (negative)
        else if (at_bottom && trying_scroll_down) {
            state.overscroll_target += wheel_delta * g_ScrollMultiplier * 3.0f;
            state.overscroll_target = ImMax(state.overscroll_target, -g_MaxOverscroll);
            state.velocity = 0.0f;
        }
        // Normal scrolling
        else {
            float input = wheel_delta * g_ScrollMultiplier * 50.0f;
            if (state.velocity * input < 0.0f)
                state.velocity = 0.0f;
            state.velocity += input;
            state.overscroll_target = 0.0f;
        }
    }
    
    // Apply velocity to scroll
    if (std::abs(state.velocity) > 0.5f) {
        float scroll_delta = state.velocity * dt;
        float new_scroll = window->Scroll.y - scroll_delta;
        
        // Check if hitting boundary with momentum
        bool will_hit_top = new_scroll < 0.0f;
        bool will_hit_bottom = new_scroll > window->ScrollMax.y;
        
        if (will_hit_top && state.velocity > 100.0f) {
            // Hit top with upward momentum - bounce down (positive)
            state.overscroll_target = ImMin(state.velocity * 0.15f, g_MaxOverscroll);
            state.velocity = 0.0f;
        } else if (will_hit_bottom && state.velocity < -100.0f) {
            // Hit bottom with downward momentum - bounce up (negative)
            state.overscroll_target = ImMax(state.velocity * 0.15f, -g_MaxOverscroll);
            state.velocity = 0.0f;
        }
        
        new_scroll = ImClamp(new_scroll, 0.0f, window->ScrollMax.y);
        window->Scroll.y = new_scroll;
        
        state.velocity = EaseOut(state.velocity, 0.0f, g_ScrollSmoothing, dt);
        if (std::abs(state.velocity) < 1.0f)
            state.velocity = 0.0f;
    }
    
    // Bounce back - target goes to 0
    if (std::abs(state.overscroll_target) > 0.1f) {
        state.overscroll_target = EaseOut(state.overscroll_target, 0.0f, g_BounceDecay, dt);
    } else {
        state.overscroll_target = 0.0f;
    }
    
    // Smooth visual overscroll (follows target smoothly)
    state.overscroll_visual = EaseOut(state.overscroll_visual, state.overscroll_target, 15.0f, dt);
}

// Custom smooth scrollbar renderer with bounce effect
void RenderSmoothScrollbar(ImGuiWindow* window)
{
    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    
    if (window->ScrollMax.y <= 0.0f) return;
    
    SmoothScrollState& state = g_ScrollStates[window->ID];
    
    // Scrollbar rect - use InnerRect to stay inside window content area
    float scrollbar_width = style.ScrollbarSize;
    float padding = 4.0f;
    ImRect bb(
        window->InnerRect.Max.x + padding,
        window->InnerRect.Min.y,
        window->InnerRect.Max.x + padding + scrollbar_width - padding * 2,
        window->InnerRect.Max.y
    );
    float scrollbar_height = bb.GetHeight();
    
    if (scrollbar_height <= 0.0f) return;
    
    // Calculate grab size - shrink when overscrolling for bounce effect
    float win_size = window->InnerRect.GetHeight();
    float content_size = window->ContentSize.y + style.WindowPadding.y * 2.0f;
    float grab_size_norm = ImClamp(win_size / content_size, 0.05f, 1.0f);
    float grab_size_pixels = ImMax(scrollbar_height * grab_size_norm, style.GrabMinSize);
    
    // Shrink grab when overscrolling (bounce visual effect)
    float overscroll_shrink = std::abs(state.overscroll_visual) * 0.5f;
    grab_size_pixels = ImMax(grab_size_pixels - overscroll_shrink, style.GrabMinSize * 0.5f);
    
    // Calculate grab position
    float scroll_ratio = ImSaturate(window->Scroll.y / window->ScrollMax.y);
    float grab_pos_target = scroll_ratio * (scrollbar_height - grab_size_pixels);
    
    // Push grab towards edge when overscrolling
    if (state.overscroll_visual < 0.0f) {
        // Overscroll at bottom - push grab down
        grab_pos_target = ImMin(scrollbar_height - grab_size_pixels, grab_pos_target - state.overscroll_visual * 0.5f);
    } else if (state.overscroll_visual > 0.0f) {
        // Overscroll at top - push grab up
        grab_pos_target = ImMax(0.0f, grab_pos_target - state.overscroll_visual * 0.5f);
    }
    
    // Animate grab position
    state.grab_anim = EaseOut(state.grab_anim, grab_pos_target, 15.0f, g.IO.DeltaTime);
    state.alpha = EaseOut(state.alpha, 1.0f, 8.0f, g.IO.DeltaTime);
    
    // Grab rect with padding
    float grab_padding = 2.0f;
    ImRect grab_rect(
        bb.Min.x + grab_padding,
        bb.Min.y + state.grab_anim,
        bb.Max.x - grab_padding,
        bb.Min.y + state.grab_anim + grab_size_pixels
    );
    
    // Clamp grab rect to scrollbar bounds
    grab_rect.Min.y = ImMax(grab_rect.Min.y, bb.Min.y);
    grab_rect.Max.y = ImMin(grab_rect.Max.y, bb.Max.y);
    
    // Colors
    bool hovered = bb.Contains(g.IO.MousePos);
    float hover_alpha = hovered ? 1.0f : 0.7f;
    ImU32 bg_col = ImGui::GetColorU32(ImGuiCol_ScrollbarBg, state.alpha * 0.3f);
    ImU32 grab_col = ImGui::GetColorU32(ImGuiCol_ScrollbarGrab, state.alpha * hover_alpha);
    
    // Draw
    ImDrawList* draw_list = window->DrawList;
    draw_list->AddRectFilled(bb.Min, bb.Max, bg_col, style.ScrollbarRounding);
    draw_list->AddRectFilled(grab_rect.Min, grab_rect.Max, grab_col, 4.0f);
}

// Data
static LPDIRECT3D9              g_pD3D = nullptr;
static LPDIRECT3DDEVICE9        g_pd3dDevice = nullptr;
static bool                     g_DeviceLost = false;
static UINT                     g_ResizeWidth = 0, g_ResizeHeight = 0;
static D3DPRESENT_PARAMETERS    g_d3dpp = {};

// Forward declarations of helper functions
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void ResetDevice();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Main code
int main(int, char**)
{
    // Make process DPI aware and obtain main monitor scale
    ImGui_ImplWin32_EnableDpiAwareness();
    float main_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

    // Create application window (fullscreen borderless)
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"ImGui Example", nullptr };
    ::RegisterClassExW(&wc);
    int screen_width = ::GetSystemMetrics(SM_CXSCREEN);
    int screen_height = ::GetSystemMetrics(SM_CYSCREEN);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"Dear ImGui DirectX9 Example", WS_POPUP, 0, 0, screen_width, screen_height, nullptr, nullptr, wc.hInstance, nullptr);

    // Initialize Direct3D
    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // Show the window
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // Setup scaling
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
    style.FontScaleDpi = main_scale;        // Set initial font scale. (using io.ConfigDpiScaleFonts=true makes this unnecessary. We leave both here for documentation purpose)

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX9_Init(g_pd3dDevice);

    // Load Fonts
    // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
    // - If the file cannot be loaded, the function will return a nullptr. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
    // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use Freetype for higher quality font rendering.
    // - Read 'docs/FONTS.md' for more instructions and details. If you like the default font but want it to scale better, consider using the 'ProggyVector' from the same author!
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    //style.FontSizeBase = 20.0f;
    //io.Fonts->AddFontDefault();
    //io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf");
    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf");
    //IM_ASSERT(font != nullptr);

    // Our state
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    // Main loop
    bool done = false;
    while (!done)
    {
        // Poll and handle messages (inputs, window resize, etc.)
        // See the WndProc() function below for our to dispatch events to the Win32 backend.
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

        // Handle lost D3D9 device
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

        // Handle window resize (we don't resize directly in the WM_SIZE handler)
        if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
        {
            g_d3dpp.BackBufferWidth = g_ResizeWidth;
            g_d3dpp.BackBufferHeight = g_ResizeHeight;
            g_ResizeWidth = g_ResizeHeight = 0;
            ResetDevice();
        }

        // Start the Dear ImGui frame
        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();

        ImGui::NewFrame();

        // Debug Info Window (movable)
        static ImGuiID scroll_window_id = 0;
        {
            ImGui::Begin("Scroll Debug Info");
            if (scroll_window_id != 0) {
                SmoothScrollState& scroll_state = g_ScrollStates[scroll_window_id];
                ImGuiWindow* scroll_win = ImGui::FindWindowByID(scroll_window_id);
                if (scroll_win) {
                    ImGui::Text("Velocity: %.2f", scroll_state.velocity);
                    ImGui::Text("Overscroll: %.2f (visual: %.2f)", scroll_state.overscroll_target, scroll_state.overscroll_visual);
                    ImGui::Text("Scroll: %.1f / %.1f", scroll_win->Scroll.y, scroll_win->ScrollMax.y);
                }
            }
            ImGui::Text("FPS: %.1f", io.Framerate);
            ImGui::End();
        }

        // Scroll Test Window
        {
            ImVec2 window_size(1200, 1050);
            ImVec2 window_pos((io.DisplaySize.x - window_size.x) * 0.5f, (io.DisplaySize.y - window_size.y) * 0.5f);
            ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(window_size, ImGuiCond_Always);
            // Keep standard scrollbar for proper ScrollMax calculation, we draw custom one on top
            ImGui::Begin("Scroll Tester", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
            
            ImGuiWindow* window = ImGui::GetCurrentWindow();
            scroll_window_id = window->ID; // Store for debug window
            SmoothScrollState& scroll_state = g_ScrollStates[window->ID];
            
            // Apply visual bounce offset to content
            // Positive = at top, push content down
            // Negative = at bottom, push content up  
            float bounce_offset = scroll_state.overscroll_visual;
            if (bounce_offset > 0.1f) {
                // At top - offset content down
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + bounce_offset);
            }
            
            for (int i = 1; i <= 100; i++)
                ImGui::Text("Tester %d", i);
            
            // For bottom bounce - add extra space at end that gets "compressed"
            if (bounce_offset < -0.1f) {
                ImGui::Dummy(ImVec2(0, -bounce_offset));
            }
            
            // Get wheel input from ImGui (not intercepted)
            float wheel = 0.0f;
            if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
                wheel = io.MouseWheel;
            }
            
            // Apply smooth scroll with bounce
            ApplySmoothScroll(window, wheel, io.DeltaTime);
            
            // Render custom scrollbar on top
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

    // Cleanup
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

    // Create the D3DDevice
    ZeroMemory(&g_d3dpp, sizeof(g_d3dpp));
    g_d3dpp.Windowed = TRUE;
    g_d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    g_d3dpp.BackBufferFormat = D3DFMT_UNKNOWN; // Need to use an explicit format with alpha if needing per-pixel alpha composition.
    g_d3dpp.EnableAutoDepthStencil = TRUE;
    g_d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
    g_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;           // Present with vsync
    //g_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;   // Present without vsync, maximum unthrottled framerate
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

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 message handler
// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam); // Queue resize
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
