
#include "windowsappplatform.h"
#include "windowsrendererfactory.h"
#include "res/res.h"

#include "../SharedLib/gbcapp/gbcapp.h"
#include "../SharedLib/messagedefs.h"

// Global Variables:
constexpr wchar_t appName[] = L"Shining Emulator";
constexpr wchar_t windowClassName[] = L"ShEmu";

#ifdef _WIN32
#include "../SharedLib/gbc/debugwindowmodule.h"
extern DebugWindowModule debugger;
#endif

static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
HINSTANCE hInst;
HWND hWnd = NULL;
HMENU hMenu = NULL;
HDC hDC = NULL;

HMENU makeMenu(Menu& menuItem);
ATOM registerWindowClass();
MSG msg;

// App instance (if any)
App* runningApp = nullptr;

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow) {

    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    hInst = hInstance;

    // Register window class
    ATOM windowClass = registerWindowClass();
    if (windowClass == 0) {
        MessageBoxW(NULL, L"Windows registration failed", L"Error", MB_OK);
        return 0;
    }

    // Create the window using the registered class
    hWnd = CreateWindowW(
            windowClassName,
            appName,
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            0,
            CW_USEDEFAULT,
            0,
            nullptr,
            nullptr,
            hInst,
            nullptr);
    if (!hWnd) {
        MessageBoxW(NULL, L"Windows creation failed", L"Error", MB_OK);
        return 0;
    }

    // Get client drawing area
    RECT clientRect;
    BOOL gotRect = GetClientRect(hWnd, &clientRect);
    if (gotRect == FALSE) {
        MessageBoxW(NULL, L"Failed to find drawing area size", L"Error", MB_OK);
        return 0;
    }

    // Create the renderer factory
    hDC = GetDC(hWnd);
    if (hDC == NULL) {
        MessageBoxW(NULL, L"Failed to retrieve the device context", L"Error", MB_OK);
        return 0;
    }

    // Show the window
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    // Create the dependencies for the app
    WindowsAppPlatform appPlatform(hInst, hWnd);
    WindowsRendererFactory rendererFactory(hDC, clientRect.right - clientRect.left, clientRect.bottom - clientRect.top);

    // Construct the app using Windows dependencies
    runningApp = new GbcApp(appPlatform, rendererFactory);
    runningApp->startThread();

    // Create the menu
    hMenu = makeMenu(runningApp->menu);
    SetMenu(hWnd, hMenu);

    // Run standard Windows message loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0) && runningApp->running) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    runningApp->stopThread();
    delete runningApp;
    runningApp = nullptr;

    ReleaseDC(hWnd, hDC);
    DestroyMenu(hMenu);

    return (int)msg.wParam;
}

ATOM registerWindowClass() {
    WNDCLASSEXW wcex;
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInst;
    wcex.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(IDI_ICON_MAIN));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = L"";
    wcex.lpszClassName = windowClassName;
    wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_ICON_SMALL));
    return RegisterClassExW(&wcex);
}

bool mouseButtonDown = false;
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
		case LAUNCH_DEBUG_MSG:
			if (runningApp) {
				auto actualApp = (GbcApp*)runningApp;
				auto gbc = actualApp->getGbc();
				debugger.showWindow(hInst, hWnd, gbc);
			}
			break;
        case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            switch (wmId) {
                case (int)Action::MSG_OPEN_FILE:
                    if (runningApp) {
                        runningApp->postMessage({ Action::MSG_OPEN_FILE, NULL });
                    }
                    break;
                case (int)Action::MSG_EXIT:
                    PostQuitMessage(0);
                    break;
                default:
                    if (runningApp) {
                        runningApp->postMessage({ (Action)wmId, 0 });
                    }
            }
        }
            break;
        case WM_KEYDOWN:
        {
            if (runningApp) {
                unsigned int code = (int)wParam;
                runningApp->platform.signalKey(code, true);
            }
        }
        break;
        case WM_KEYUP:
        {
            if (runningApp) {
                unsigned int code = (int)wParam;
                runningApp->platform.signalKey(code, false);
            }
        }
        break;
        case WM_LBUTTONDOWN:
        {
            if (runningApp) {
                POINTS point = MAKEPOINTS(lParam);
                runningApp->addCursor(0, (float)point.x, (float)point.y);
                mouseButtonDown = true;
            }
        }
            break;
        case WM_MOUSEMOVE:
        {
            if (mouseButtonDown && runningApp) {
                POINTS point = MAKEPOINTS(lParam);
                runningApp->updateCursor(0, (float)point.x, (float)point.y);
            }
        }
            break;
        case WM_LBUTTONUP:
        {
            if (runningApp) {
                runningApp->removeCursor(0);
                mouseButtonDown = false;
            }
        }
            break;
        case WM_KILLFOCUS:
        {
            if (runningApp) {
                runningApp->platform.releaseAllInputs();
                mouseButtonDown = false;
            }
        }
            break;
        case WM_SIZE:
            if (runningApp) {
                runningApp->requestWindowResize(LOWORD(lParam), HIWORD(lParam));
            }
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            break;
            default:
            return DefWindowProcW(hWnd, message, wParam, lParam);
    }
    return 0;
}

HMENU makeMenu(Menu& menuItem) {
    if (menuItem.subMenus.size() > 0) {
        // Submenu
        HMENU menu = CreateMenu();
        for (auto& item : menuItem.subMenus) {
            HMENU subItem = makeMenu(item);
            if (subItem == NULL) {
                AppendMenuW(menu, MF_ENABLED | MF_STRING, (int)item.action, item.text.c_str());
            } else {
                InsertMenuW(menu, 0 - 1, MF_BYPOSITION | MF_ENABLED | MF_STRING | MF_POPUP, (UINT_PTR)subItem, item.text.c_str());
            }
        }
        return menu;
    } else {
        return NULL;
    }
}
