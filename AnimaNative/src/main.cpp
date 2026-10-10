#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <filesystem>
#include <string>
#include <sstream>
#include <stdexcept>
#include <memory>

#include "NativeAnima.h"
#include "NativeRenderer.h"

namespace {
std::unique_ptr<anima::native::NativeRenderer> gRenderer;
std::wstring gBaseTitle = L"ANIMA Native";
HWND gWindow = nullptr;

void UpdateTitle() {
    if (!gWindow || !gRenderer) return;
    std::wstring title = gBaseTitle + (gRenderer->RayTracingEnabled() ? L"  |  GPU tracing: ON" : L"  |  GPU tracing: OFF");
    title += gRenderer->DxrSupported() ? L"  |  DXR support detected" : L"  |  DXR support not detected";
    title += L"  |  R or click top-left toggle · Esc exits";
    SetWindowTextW(gWindow, title.c_str());
}
LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) { DestroyWindow(hwnd); return 0; }
        if (wParam == 'R' && gRenderer) { gRenderer->ToggleRayTracing(); UpdateTitle(); return 0; }
        if (wParam == '1' && gRenderer) { gRenderer->SetRayTracing(true); UpdateTitle(); return 0; }
        if (wParam == '0' && gRenderer) { gRenderer->SetRayTracing(false); UpdateTitle(); return 0; }
        break;
    case WM_LBUTTONDOWN: {
        // The shader-drawn ON/OFF segmented toggle is rendered at 800x450,
        // while the fixed native window client is 1280x720.
        const int x = GET_X_LPARAM(lParam) * 800 / 1280;
        const int y = GET_Y_LPARAM(lParam) * 450 / 720;
        if (gRenderer && y >= 16 && y < 48) {
            if (x >= 16 && x < 108) { gRenderer->SetRayTracing(true); UpdateTitle(); return 0; }
            if (x >= 116 && x < 224) { gRenderer->SetRayTracing(false); UpdateTitle(); return 0; }
        }
        break;
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_ERASEBKGND: return 1;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}
std::filesystem::path ExeDirectory() {
    wchar_t buffer[MAX_PATH]{}; GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return std::filesystem::path(buffer).parent_path();
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    try {
        // Parse the existing ANIMA room program natively before creating the renderer.
        std::filesystem::path programPath = ExeDirectory() / L"Examples" / L"raytrace-room.anima";
        int argCount = 0;
        LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &argCount);
        if (args && argCount > 1) programPath = args[1];
        if (args) LocalFree(args);
        const auto summary = anima::native::ParseFile(programPath);
        std::wstringstream title;
        title << L"ANIMA Native  |  " << std::wstring(summary.appName.begin(), summary.appName.end())
              << L"  |  " << summary.sceneCount << L" scene(s), " << summary.nodeCount << L" node(s)";
        gBaseTitle = title.str();

        WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WindowProc; wc.hInstance = instance; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION); wc.hIconSm = wc.hIcon; wc.lpszClassName = L"AnimaNativeWindow";
        if (!RegisterClassExW(&wc)) throw std::runtime_error("Could not register the native window class.");
        RECT rect{ 0,0,1280,720 };
        AdjustWindowRect(&rect, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
        gWindow = CreateWindowExW(0, wc.lpszClassName, gBaseTitle.c_str(), WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
            CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, instance, nullptr);
        if (!gWindow) throw std::runtime_error("Could not create the native application window.");
        gRenderer = std::make_unique<anima::native::NativeRenderer>();
        if (!gRenderer->Initialize(gWindow)) {
            const auto status = gRenderer->StatusText();
            MessageBoxW(gWindow, status.c_str(), L"ANIMA Native — initialization failed", MB_ICONERROR | MB_OK);
            return 1;
        }
        gBaseTitle += L"  |  GPU: " + gRenderer->AdapterName();
        UpdateTitle();
        ShowWindow(gWindow, showCommand); UpdateWindow(gWindow);

        LARGE_INTEGER frequency{}, start{}; QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&start);
        MSG msg{};
        bool running = true;
        while (running) {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) { running = false; break; }
                TranslateMessage(&msg); DispatchMessageW(&msg);
            }
            if (!running) break;
            LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
            const float seconds = static_cast<float>(static_cast<double>(now.QuadPart - start.QuadPart) / static_cast<double>(frequency.QuadPart));
            gRenderer->Render(seconds);
        }
        gRenderer->Shutdown(); gRenderer.reset();
        return static_cast<int>(msg.wParam);
    } catch (const std::exception& e) {
        const std::string narrow = e.what();
        const std::wstring message(narrow.begin(), narrow.end());
        MessageBoxW(nullptr, message.c_str(), L"ANIMA Native — fatal error", MB_ICONERROR | MB_OK);
        return 1;
    }
}
