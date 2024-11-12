/*
  MinGW-w64
  pacman -Syu
  pacman -S mingw-w64-x86_64-gcc
  pacman -S mingw-w64-x86_64-make

  
  g++ -o win_tra.exe .\win_tra.cc -mwindows -lpsapi -lgdi32 -luser32 -lshell32
  author bokumin  
*/
#include <iostream>

#include <algorithm>
#include <vector>
#include <set>
#include <tlhelp32.h>
#include <psapi.h>
#include <windows.h>

// リンク時にpsapiライブラリを追加
#pragma comment(lib, "psapi.lib")

HHOOK mouseHook;
HWND hwnd = NULL;
bool isShiftPressed = false;
std::set<HWND> modifiedWindows;
char currentProcessPath[MAX_PATH];

void GetCurrentProcessFileName() {
    GetModuleFileNameA(NULL, currentProcessPath, MAX_PATH);
    char* fileName = strrchr(currentProcessPath, '\\');
    if (fileName) {
        strcpy(currentProcessPath, fileName + 1);
    }
}

// 指定されたプロセスIDが現在のプロセスと同じ実行ファイルかチェックする関数
bool IsSameExecutable(DWORD processId) {
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processId);
    if (hProcess) {
        char processPath[MAX_PATH];
        if (GetModuleFileNameExA(hProcess, NULL, processPath, MAX_PATH)) {
            char* fileName = strrchr(processPath, '\\');
            if (fileName) {
                fileName++; 
                bool result = _stricmp(fileName, currentProcessPath) == 0;
                CloseHandle(hProcess);
                return result;
            }
        }
        CloseHandle(hProcess);
    }
    return false;
}

void TerminateCurrentProcess() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe32;
        pe32.dwSize = sizeof(pe32);
        
        if (Process32FirstW(snapshot, &pe32)) {
            do {
                DWORD processId = pe32.th32ProcessID;
                if (IsSameExecutable(processId)) {
                    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, processId);
                    if (hProcess != NULL) {
                        TerminateProcess(hProcess, 0);
                        CloseHandle(hProcess);
                    }
                }
            } while (Process32NextW(snapshot, &pe32));
        }
        CloseHandle(snapshot);
    }
}

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    if (style & WS_EX_LAYERED) {
        SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
        SetWindowLongPtr(hwnd, GWL_EXSTYLE, style & ~WS_EX_LAYERED);
        modifiedWindows.erase(hwnd);
    }
    return TRUE;
}

void ResetAllWindows() {
    EnumWindows(EnumWindowsProc, 0);
}

bool IsSystemWindow(HWND hwnd) {
    char className[256];
    GetClassNameA(hwnd, className, sizeof(className));
    
    const char* systemClasses[] = {
        "Shell_TrayWnd",
        "DV2ControlHost",
        "Windows.UI.Core.CoreWindow",
        "NotifyIconOverflowWindow",
        "WorkerW",
        "Progman",
        "NotifyIconOverflowWindow",
        "#32769",
        "Shell_SecondaryTrayWnd"
    };
    
    for (const char* sysClass : systemClasses) {
        if (strcmp(className, sysClass) == 0) {
            return true;
        }
    }
    
    if (strstr(className, "Windows") != nullptr) {
        char title[256];
        GetWindowTextA(hwnd, title, sizeof(title));
        if (strstr(title, "File Explorer") != nullptr ||
            strstr(title, "エクスプローラー") != nullptr) {
            return true;
        }
    }
    
    return false;
}

void AdjustWindowTransparency(HWND hwnd, BYTE alpha) {
    if (!hwnd || IsSystemWindow(hwnd)) return;

    HWND mainWindow = GetAncestor(hwnd, GA_ROOT);
    if (!mainWindow || IsSystemWindow(mainWindow)) return;

    if (IsWindowVisible(mainWindow) && !IsIconic(mainWindow)) {
        LONG_PTR style = GetWindowLongPtr(mainWindow, GWL_EXSTYLE);
        
        if (!(style & WS_EX_LAYERED)) {
            SetWindowLongPtr(mainWindow, GWL_EXSTYLE, style | WS_EX_LAYERED);
        }
        
        if (!SetLayeredWindowAttributes(mainWindow, 0, alpha, LWA_ALPHA)) {
            DWORD error = GetLastError();
            char errorMsg[256];
            sprintf(errorMsg, "SetLayeredWindowAttributes failed with error: %lu", error);
            OutputDebugStringA(errorMsg);
        }
        
        modifiedWindows.insert(mainWindow);
    }
}

bool IsAlreadyRunning() {
    HANDLE mutex = CreateMutexA(NULL, TRUE, "WindowTransparencyController_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        return true;
    }
    return false;
}

LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        MSLLHOOKSTRUCT* mouseStruct = (MSLLHOOKSTRUCT*)lParam;
        
        isShiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        
        if (wParam == WM_MOUSEWHEEL && isShiftPressed) {
            POINT pt = mouseStruct->pt;
            HWND windowUnderCursor = WindowFromPoint(pt);
            
            if (windowUnderCursor && !IsSystemWindow(windowUnderCursor)) {
                RECT windowRect;
                GetWindowRect(windowUnderCursor, &windowRect);
                
                if (pt.y <= windowRect.top + 30) {
                    HWND mainWindow = GetAncestor(windowUnderCursor, GA_ROOT);
                    if (mainWindow && !IsSystemWindow(mainWindow)) {
                        BYTE alpha = 255;
                        DWORD flags;
                        
                        LONG_PTR style = GetWindowLongPtr(mainWindow, GWL_EXSTYLE);
                        if (style & WS_EX_LAYERED) {
                            GetLayeredWindowAttributes(mainWindow, NULL, &alpha, &flags);
                        }
                        
                        int wheelDelta = GET_WHEEL_DELTA_WPARAM(mouseStruct->mouseData);
                        if (wheelDelta > 0) {
                            alpha = static_cast<BYTE>(std::min(255, static_cast<int>(alpha) + 25));
                        } else {
                            alpha = static_cast<BYTE>(std::max(1, static_cast<int>(alpha) - 25)); // 0にすると押せなくなるので1が最小
                        }
                        
                        AdjustWindowTransparency(mainWindow, alpha);
                    }
                }
            }
        }
    }
    return CallNextHookEx(mouseHook, nCode, wParam, lParam);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_DESTROY:
            if (mouseHook) {
                UnhookWindowsHookEx(mouseHook);
            }
            
            NOTIFYICONDATAA nid = { sizeof(NOTIFYICONDATAA) };
            nid.hWnd = hwnd;
            Shell_NotifyIconA(NIM_DELETE, &nid);
            
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    GetCurrentProcessFileName();
    
    if (IsAlreadyRunning()) {
        ResetAllWindows();
	//"全てのウィンドウの透明度をリセットしました。\n終了しますか？", 
	//"リセット完了"
	int result = MessageBoxA(NULL, "All window transparency has been reset.\nDo you want to exit?", 
				 "Reset Complete", MB_OKCANCEL | MB_ICONINFORMATION);
        if (result == IDOK) {
            TerminateCurrentProcess();

            if (mouseHook) {
                UnhookWindowsHookEx(mouseHook);
            }

            NOTIFYICONDATAA nid = { sizeof(NOTIFYICONDATAA) };
            nid.hWnd = hwnd;
            Shell_NotifyIconA(NIM_DELETE, &nid);

            ExitProcess(0);
        }
        return 0;
    }
    
    WNDCLASSEXA wc = { sizeof(WNDCLASSEXA) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "TransparencyControllerClass";
    RegisterClassExA(&wc);
    
    hwnd = CreateWindowA("TransparencyControllerClass", "Transparency Controller", 
                        WS_OVERLAPPEDWINDOW, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);
    
    mouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseProc, hInstance, 0);
    if (!mouseHook) {
      //フックの設定に失敗しました。
      //エラー
        MessageBoxA(NULL, "Failed to set up hook", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    
    NOTIFYICONDATAA nid = { sizeof(NOTIFYICONDATAA) };
    nid.hWnd = hwnd;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    nid.uCallbackMessage = WM_USER + 1;
    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    //ウインドウ透明化(実行中)
    strcpy(nid.szTip, "Window Trans Controller (Running)");
    Shell_NotifyIconA(NIM_ADD, &nid);
    
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    Shell_NotifyIconA(NIM_DELETE, &nid);
    UnhookWindowsHookEx(mouseHook);
    DestroyWindow(hwnd);
    UnregisterClassA("TransparencyControllerClass", hInstance);
    return 0;
}
