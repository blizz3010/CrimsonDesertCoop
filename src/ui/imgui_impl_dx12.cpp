// DX12 Present Hook for ImGui overlay in Crimson Desert
//
// Hooks IDXGISwapChain::Present (vtable index 8) to inject ImGui rendering.
// Uses a dummy DXGI device at startup to steal the vtable, then installs
// an inline hook via SafetyHook.
//
// The BlackSpace Engine uses DX12 with a flip-model swap chain.

#include <cdcoop/ui/overlay.h>
#include <cdcoop/core/hooks.h>
#include <cdcoop/core/game_structures.h>
#include <cdcoop/network/session.h>
#include <cdcoop/sync/player_sync.h>
#include <cdcoop/sync/enemy_sync.h>
#include <cdcoop/sync/world_sync.h>
#include <cdcoop/sync/mount_sync.h>
#include <cdcoop/player/player_manager.h>
#include <spdlog/spdlog.h>

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <safetyhook.hpp>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

// ImGui headers
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx12.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace cdcoop {
namespace dx12_hook {

// DX12 Present vtable index
static constexpr int PRESENT_VTABLE_INDEX = 8;
static constexpr int RESIZE_BUFFERS_VTABLE_INDEX = 13;
static constexpr int EXECUTE_CMD_LISTS_VTABLE_INDEX = 54;
static constexpr int NUM_BACK_BUFFERS = 3;

// Hook state
static SafetyHookInline present_hook;
static SafetyHookInline resize_hook;

// DX12 state for ImGui rendering
static ID3D12Device* device = nullptr;
static ID3D12DescriptorHeap* srv_heap = nullptr;
static ID3D12CommandAllocator* cmd_allocators[NUM_BACK_BUFFERS] = {};
static ID3D12GraphicsCommandList* cmd_list = nullptr;
static ID3D12Resource* back_buffers[NUM_BACK_BUFFERS] = {};
static D3D12_CPU_DESCRIPTOR_HANDLE rtv_handles[NUM_BACK_BUFFERS] = {};
static ID3D12DescriptorHeap* rtv_heap = nullptr;

static HWND game_hwnd = nullptr;
static WNDPROC original_wndproc = nullptr;
static bool imgui_initialized = false;
static bool need_resize = false;
static UINT back_buffer_count = 0;

// Frame timing for delta_time
static ID3D12CommandQueue* imgui_cmd_queue = nullptr;
static LARGE_INTEGER perf_freq = {};
static LARGE_INTEGER last_frame_time = {};

// WndProc hook for ImGui input handling
static LRESULT CALLBACK wndproc_hook(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;
    return CallWindowProc(original_wndproc, hWnd, msg, wParam, lParam);
}

static void cleanup_render_targets() {
    for (UINT i = 0; i < NUM_BACK_BUFFERS; i++) {
        if (back_buffers[i]) {
            back_buffers[i]->Release();
            back_buffers[i] = nullptr;
        }
    }
}

static void create_render_targets(IDXGISwapChain* swap_chain) {
    for (UINT i = 0; i < back_buffer_count; i++) {
        ID3D12Resource* buffer = nullptr;
        swap_chain->GetBuffer(i, IID_PPV_ARGS(&buffer));
        if (buffer) {
            device->CreateRenderTargetView(buffer, nullptr, rtv_handles[i]);
            back_buffers[i] = buffer;
        }
    }
}

static bool init_imgui(IDXGISwapChain* swap_chain) {
    // Get device from swap chain
    if (FAILED(swap_chain->GetDevice(IID_PPV_ARGS(&device)))) {
        spdlog::error("DX12 Hook: Failed to get device from swap chain");
        return false;
    }

    // Get swap chain desc for HWND and buffer info
    DXGI_SWAP_CHAIN_DESC sc_desc;
    swap_chain->GetDesc(&sc_desc);
    game_hwnd = sc_desc.OutputWindow;
    back_buffer_count = sc_desc.BufferCount;
    if (back_buffer_count > NUM_BACK_BUFFERS) back_buffer_count = NUM_BACK_BUFFERS;

    // Create SRV descriptor heap for ImGui fonts
    D3D12_DESCRIPTOR_HEAP_DESC srv_desc = {};
    srv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srv_desc.NumDescriptors = 1;
    srv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device->CreateDescriptorHeap(&srv_desc, IID_PPV_ARGS(&srv_heap)))) {
        spdlog::error("DX12 Hook: Failed to create SRV heap");
        return false;
    }

    // Create RTV descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC rtv_desc = {};
    rtv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtv_desc.NumDescriptors = back_buffer_count;
    if (FAILED(device->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&rtv_heap)))) {
        spdlog::error("DX12 Hook: Failed to create RTV heap");
        return false;
    }

    // Set up RTV handles
    SIZE_T rtv_increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = rtv_heap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < back_buffer_count; i++) {
        rtv_handles[i] = rtv_handle;
        rtv_handle.ptr += rtv_increment;
    }

    // Create a persistent command queue for ImGui rendering
    {
        D3D12_COMMAND_QUEUE_DESC qd = {};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&imgui_cmd_queue)))) {
            spdlog::error("DX12 Hook: Failed to create ImGui command queue");
            return false;
        }
    }

    // Create command allocators and list
    for (UINT i = 0; i < back_buffer_count; i++) {
        if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                   IID_PPV_ARGS(&cmd_allocators[i])))) {
            spdlog::error("DX12 Hook: Failed to create command allocator {}", i);
            return false;
        }
    }

    if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
               cmd_allocators[0], nullptr, IID_PPV_ARGS(&cmd_list)))) {
        spdlog::error("DX12 Hook: Failed to create command list");
        return false;
    }
    cmd_list->Close();

    // Get back buffers
    create_render_targets(swap_chain);

    // Initialize ImGui
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Style
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding = 2.0f;
    style.Alpha = 0.9f;

    // Platform/renderer backends
    ImGui_ImplWin32_Init(game_hwnd);
    ImGui_ImplDX12_Init(device, back_buffer_count,
                        sc_desc.BufferDesc.Format,
                        srv_heap,
                        srv_heap->GetCPUDescriptorHandleForHeapStart(),
                        srv_heap->GetGPUDescriptorHandleForHeapStart());

    // Hook WndProc for input
    original_wndproc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtr(game_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(wndproc_hook)));

    // Init frame timing
    QueryPerformanceFrequency(&perf_freq);
    QueryPerformanceCounter(&last_frame_time);

    spdlog::info("DX12 Hook: ImGui initialized ({}x{}, {} buffers)",
                  sc_desc.BufferDesc.Width, sc_desc.BufferDesc.Height, back_buffer_count);
    return true;
}

// Present detour - this fires every frame
static HRESULT __stdcall present_detour(IDXGISwapChain* swap_chain, UINT sync_interval, UINT flags) {
    if (!imgui_initialized) {
        if (init_imgui(swap_chain)) {
            imgui_initialized = true;
        } else {
            return present_hook.stdcall<HRESULT>(swap_chain, sync_interval, flags);
        }
    }

    if (need_resize) {
        cleanup_render_targets();
        create_render_targets(swap_chain);
        need_resize = false;
    }

    // Calculate delta_time
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    float delta_time = static_cast<float>(now.QuadPart - last_frame_time.QuadPart) /
                       static_cast<float>(perf_freq.QuadPart);
    last_frame_time = now;

    // Clamp delta to avoid large jumps (e.g. during loading)
    if (delta_time > 0.1f) delta_time = 0.1f;

    // =====================================================================
    // Co-op update loop - runs every frame via DX12 Present hook
    // This is the main tick for all networking, sync, and input processing.
    // =====================================================================
    {
        auto& session = cdcoop::Session::instance();
        if (session.is_active()) {
            session.update(delta_time);
            cdcoop::PlayerSync::instance().update(delta_time);
            cdcoop::EnemySync::instance().update(delta_time);
            cdcoop::WorldSync::instance().update(delta_time);
        }
        // MountSync polls the local mount pointer even outside a session
        // so the debug overlay can show local mount HP for verification.
        cdcoop::MountSync::instance().update(delta_time);
        cdcoop::PlayerManager::instance().update(delta_time);

        // Hotkey handling lives in the dedicated input thread (see
        // dllmain.cpp::input_poll_loop). That path runs even when this
        // Present hook failed to install, so F7/F8 are never silent.
    }

    // Start ImGui frame
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // Render our overlay
    Overlay::instance().render();

    // Finish ImGui frame
    ImGui::EndFrame();
    ImGui::Render();

    // Render ImGui draw data to the current back buffer
    auto* swap_chain3 = static_cast<IDXGISwapChain3*>(swap_chain);
    UINT buffer_index = swap_chain3->GetCurrentBackBufferIndex();
    if (buffer_index < back_buffer_count) {
        auto* allocator = cmd_allocators[buffer_index];
        allocator->Reset();
        cmd_list->Reset(allocator, nullptr);

        // Transition back buffer to render target
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = back_buffers[buffer_index];
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd_list->ResourceBarrier(1, &barrier);

        cmd_list->OMSetRenderTargets(1, &rtv_handles[buffer_index], FALSE, nullptr);
        cmd_list->SetDescriptorHeaps(1, &srv_heap);

        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmd_list);

        // Transition back to present
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        cmd_list->ResourceBarrier(1, &barrier);

        cmd_list->Close();

        // Execute on our persistent command queue
        if (imgui_cmd_queue) {
            ID3D12CommandList* lists[] = { cmd_list };
            imgui_cmd_queue->ExecuteCommandLists(1, lists);
        }
    }

    return present_hook.stdcall<HRESULT>(swap_chain, sync_interval, flags);
}

// ResizeBuffers detour - handle window resize
static HRESULT __stdcall resize_detour(IDXGISwapChain* swap_chain, UINT buffer_count,
                                        UINT width, UINT height, DXGI_FORMAT format, UINT flags) {
    cleanup_render_targets();
    need_resize = true;

    if (imgui_initialized) {
        ImGui_ImplDX12_InvalidateDeviceObjects();
    }

    HRESULT hr = resize_hook.stdcall<HRESULT>(swap_chain, buffer_count, width, height, format, flags);

    if (imgui_initialized && SUCCEEDED(hr)) {
        if (buffer_count > 0) back_buffer_count = (buffer_count > NUM_BACK_BUFFERS) ? NUM_BACK_BUFFERS : buffer_count;
        ImGui_ImplDX12_CreateDeviceObjects();
    }

    return hr;
}

bool install_present_hook() {
    // Create a temporary DXGI factory + device + swap chain to get vtable
    IDXGIFactory4* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        spdlog::error("DX12 Hook: Failed to create DXGI factory");
        return false;
    }

    ID3D12Device* tmp_device = nullptr;
    if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&tmp_device)))) {
        factory->Release();
        spdlog::error("DX12 Hook: Failed to create temporary D3D12 device");
        return false;
    }

    D3D12_COMMAND_QUEUE_DESC qd = {};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue* tmp_queue = nullptr;
    HRESULT hr = tmp_device->CreateCommandQueue(&qd, IID_PPV_ARGS(&tmp_queue));
    if (FAILED(hr) || !tmp_queue) {
        // Reported on some Intel Arc drivers when the WARP fallback is in
        // a weird state. We were previously calling tmp_queue->Release()
        // on the failure path of the swap-chain create below, which ate
        // a null deref if we'd reached that path with tmp_queue still
        // null — crashing the game during mod init.
        tmp_device->Release();
        factory->Release();
        spdlog::error("DX12 Hook: Failed to create temporary command queue (0x{:X})",
                      static_cast<uint32_t>(hr));
        return false;
    }

    HWND output_window = GetForegroundWindow();
    if (!output_window) {
        // Some launchers steal foreground briefly during init. Fall back
        // to the desktop window so CreateSwapChain has a valid HWND;
        // we never present to it, only need it to derive the vtable.
        output_window = GetDesktopWindow();
    }

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 100;
    sd.BufferDesc.Height = 100;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = output_window;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    IDXGISwapChain* tmp_swap = nullptr;
    hr = factory->CreateSwapChain(tmp_queue, &sd, &tmp_swap);
    if (FAILED(hr)) {
        tmp_queue->Release();
        tmp_device->Release();
        factory->Release();
        spdlog::error("DX12 Hook: Failed to create temporary swap chain (0x{:X})", static_cast<uint32_t>(hr));
        return false;
    }

    // Extract vtable
    void** vtable = *reinterpret_cast<void***>(tmp_swap);
    void* present_addr = vtable[PRESENT_VTABLE_INDEX];
    void* resize_addr = vtable[RESIZE_BUFFERS_VTABLE_INDEX];

    spdlog::info("DX12 Hook: Present at 0x{:X}, ResizeBuffers at 0x{:X}",
                  reinterpret_cast<uintptr_t>(present_addr),
                  reinterpret_cast<uintptr_t>(resize_addr));

    // Clean up temporaries
    tmp_swap->Release();
    tmp_queue->Release();
    tmp_device->Release();
    factory->Release();

    // Install hooks
    present_hook = safetyhook::create_inline(present_addr, reinterpret_cast<void*>(present_detour));
    if (!present_hook) {
        spdlog::error("DX12 Hook: Failed to hook Present");
        return false;
    }

    resize_hook = safetyhook::create_inline(resize_addr, reinterpret_cast<void*>(resize_detour));
    if (!resize_hook) {
        spdlog::warn("DX12 Hook: Failed to hook ResizeBuffers (resize may cause issues)");
    }

    spdlog::info("DX12 Hook: Present hook installed successfully");
    return true;
}

void remove_present_hook() {
    present_hook = {};
    resize_hook = {};

    if (imgui_initialized) {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();

        if (original_wndproc && game_hwnd) {
            SetWindowLongPtr(game_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original_wndproc));
        }
    }

    cleanup_render_targets();
    if (cmd_list) { cmd_list->Release(); cmd_list = nullptr; }
    for (auto& alloc : cmd_allocators) { if (alloc) { alloc->Release(); alloc = nullptr; } }
    if (imgui_cmd_queue) { imgui_cmd_queue->Release(); imgui_cmd_queue = nullptr; }
    if (srv_heap) { srv_heap->Release(); srv_heap = nullptr; }
    if (rtv_heap) { rtv_heap->Release(); rtv_heap = nullptr; }
    if (device) { device->Release(); device = nullptr; }

    imgui_initialized = false;
}

float get_delta_time() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    float dt = static_cast<float>(now.QuadPart - last_frame_time.QuadPart) /
               static_cast<float>(perf_freq.QuadPart);
    return (dt > 0.1f) ? 0.1f : dt;
}

} // namespace dx12_hook
} // namespace cdcoop
