#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include "Client.h"
#include "resource.h"
#include <commctrl.h>
#include <sstream>
#include <iomanip>
#include <Psapi.h>
#include <shellapi.h>
#include <fstream>

#pragma comment(lib, "Psapi.lib")
#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

UDPClient g_client;
HINSTANCE g_hInstance = nullptr;
HWND g_hMainWnd = nullptr;

AppConfig g_config;

std::atomic<bool> g_autoConnectRunning(false);
std::thread g_autoConnectThread;
std::wstring g_connectedProcessName;

HWND g_hStaticStatus = nullptr;
HWND g_hSliderFPS = nullptr;
HWND g_hSliderFOV = nullptr;
HWND g_hStaticFPSValue = nullptr;
HWND g_hStaticFOVValue = nullptr;
HWND g_hEditFPSValue = nullptr;
HWND g_hEditFOVValue = nullptr;
bool g_valueEditUpdating = false;
bool g_sliderSyncInProgress = false;
HWND g_hCheckFPS = nullptr;
HWND g_hCheckFOV = nullptr;
HWND g_hCheckFog = nullptr;
HWND g_hCheckPerspective = nullptr;
HWND g_hCheckSyncount = nullptr;
HWND g_hStaticProcessInfo = nullptr;

void UpdateFPSValueField(int fps);
void UpdateFOVValueField(float value);
void CommitFPSValueFromEdit(HWND hWnd);
void CommitFOVValueFromEdit(HWND hWnd);

NOTIFYICONDATA nid = { 0 };

void AddTrayIcon(HWND hWnd) {
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hWnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY_ICON;
    nid.hIcon = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDR_MAINICON));
    wcscpy_s(nid.szTip, sizeof(nid.szTip) / sizeof(wchar_t), L"Client - 最小化到托盘");

    Shell_NotifyIcon(NIM_ADD, &nid);
}

void RemoveTrayIcon() {
    Shell_NotifyIcon(NIM_DELETE, &nid);
}

void ShowTrayMenu(HWND hWnd) {
    HMENU hMenu = CreatePopupMenu();
    AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT, L"退出");

    POINT pt;
    GetCursorPos(&pt);

    SetForegroundWindow(hWnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hWnd, nullptr);
    DestroyMenu(hMenu);
}

std::vector<ProcessInfo> FindTargetProcesses() {
    std::vector<ProcessInfo> processes;

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        return processes;
    }

    PROCESSENTRY32W pe32;
    pe32.dwSize = sizeof(pe32);

    if (Process32FirstW(hSnapshot, &pe32)) {
        do {
            std::wstring processName = pe32.szExeFile;
            if (_wcsicmp(processName.c_str(), TARGET_PROCESS_NAME) == 0 ||
                _wcsicmp(processName.c_str(), TARGET_PROCESS_NAME_ALT) == 0) {
                ProcessInfo info;
                info.pid = pe32.th32ProcessID;
                info.name = processName;
                processes.push_back(info);
            }
        } while (Process32NextW(hSnapshot, &pe32));
    }

    CloseHandle(hSnapshot);
    return processes;
}

std::wstring GetConfigFilePath() {
    wchar_t documentsPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_MYDOCUMENTS, nullptr, 0, documentsPath))) {
        std::wstring configDir = std::wstring(documentsPath) + L"\\fufu";
        CreateDirectoryW(configDir.c_str(), nullptr);
        return configDir + L"\\fufu.cfg";
    }
    return L"";
}

bool SaveConfig() {
    std::wstring configPath = GetConfigFilePath();
    if (configPath.empty()) return false;

    std::ofstream file(configPath);
    if (!file.is_open()) return false;

    file << "fpsOverrideEnabled=" << (g_config.fpsOverrideEnabled ? 1 : 0) << std::endl;
    file << "fovOverrideEnabled=" << (g_config.fovOverrideEnabled ? 1 : 0) << std::endl;
    file << "fogOverrideEnabled=" << (g_config.fogOverrideEnabled ? 1 : 0) << std::endl;
    file << "perspectiveOverrideEnabled=" << (g_config.perspectiveOverrideEnabled ? 1 : 0) << std::endl;
    file << "syncountOverrideEnabled=" << (g_config.syncountOverrideEnabled ? 1 : 0) << std::endl;
    file << "fpsValue=" << g_config.fpsValue << std::endl;
    file << "fovValue=" << g_config.fovValue << std::endl;

    file.close();
    return true;
}

bool LoadConfig() {
    std::wstring configPath = GetConfigFilePath();
    if (configPath.empty()) return false;

    std::ifstream file(configPath);
    if (!file.is_open()) return false;

    std::string line;
    while (std::getline(file, line)) {
        size_t pos = line.find('=');
        if (pos == std::string::npos) continue;

        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);

        if (key == "fpsOverrideEnabled") g_config.fpsOverrideEnabled = (std::stoi(value) != 0);
        else if (key == "fovOverrideEnabled") g_config.fovOverrideEnabled = (std::stoi(value) != 0);
        else if (key == "fogOverrideEnabled") g_config.fogOverrideEnabled = (std::stoi(value) != 0);
        else if (key == "perspectiveOverrideEnabled") g_config.perspectiveOverrideEnabled = (std::stoi(value) != 0);
        else if (key == "syncountOverrideEnabled") g_config.syncountOverrideEnabled = (std::stoi(value) != 0);
        else if (key == "fpsValue") g_config.fpsValue = std::stoi(value);
        else if (key == "fovValue") g_config.fovValue = std::stoi(value);
    }

    file.close();
    return true;
}

void ApplyConfigToUI(HWND hWnd) {
    SendMessage(g_hCheckFPS, BM_SETCHECK, g_config.fpsOverrideEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(g_hCheckFOV, BM_SETCHECK, g_config.fovOverrideEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(g_hCheckFog, BM_SETCHECK, g_config.fogOverrideEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(g_hCheckPerspective, BM_SETCHECK, g_config.perspectiveOverrideEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(g_hCheckSyncount, BM_SETCHECK, g_config.syncountOverrideEnabled ? BST_CHECKED : BST_UNCHECKED, 0);

    SendMessage(g_hSliderFPS, TBM_SETPOS, TRUE, g_config.fpsValue);
    SendMessage(g_hSliderFOV, TBM_SETPOS, TRUE, g_config.fovValue);
    UpdateFPSValueField(g_config.fpsValue);
    UpdateFOVValueField((float)g_config.fovValue);
}

void UpdateFPSValueField(int fps) {
    if (!g_hEditFPSValue) return;
    g_valueEditUpdating = true;
    wchar_t text[16];
    swprintf_s(text, L"%d", fps);
    SetWindowText(g_hEditFPSValue, text);
    g_valueEditUpdating = false;
}

void UpdateFOVValueField(float value) {
    if (!g_hEditFOVValue) return;
    g_valueEditUpdating = true;
    wchar_t text[16];
    swprintf_s(text, L"%.1f", value);
    SetWindowText(g_hEditFOVValue, text);
    g_valueEditUpdating = false;
}

void CommitFPSValueFromEdit(HWND hWnd) {
    if (!g_hEditFPSValue || g_valueEditUpdating) return;
    wchar_t text[32];
    GetWindowText(g_hEditFPSValue, text, _countof(text));
    int value = _wtoi(text);
    if (value < 30) value = 30;
    else if (value > 1000) value = 1000;
    g_sliderSyncInProgress = true;
    SendMessage(g_hSliderFPS, TBM_SETPOS, TRUE, value);
    g_sliderSyncInProgress = false;
    UpdateFPSValueField(value);
}

void CommitFOVValueFromEdit(HWND hWnd) {
    if (!g_hEditFOVValue || g_valueEditUpdating) return;
    wchar_t text[32];
    GetWindowText(g_hEditFOVValue, text, _countof(text));
    float value = (float)_wtof(text);
    if (value < 30.0f) value = 30.0f;
    else if (value > 120.0f) value = 120.0f;
    int sliderValue = (int)(value + 0.5f);
    g_sliderSyncInProgress = true;
    SendMessage(g_hSliderFOV, TBM_SETPOS, TRUE, sliderValue);
    g_sliderSyncInProgress = false;
    UpdateFOVValueField(value);
}

UDPClient::UDPClient()
    : m_socket(INVALID_SOCKET)
    , m_connected(false)
    , m_heartbeatRunning(false)
    , m_connectedPID(0)
{
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        m_lastError = "WSAStartup failed with code: " + std::to_string(result);
    }
    ZeroMemory(&m_serverAddr, sizeof(m_serverAddr));
}

UDPClient::~UDPClient() {
    Disconnect();
    WSACleanup();
}

bool UDPClient::Connect() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_connected) {
        m_lastError = "已经连接";
        return false;
    }

    m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_socket == INVALID_SOCKET) {
        m_lastError = "创建socket失败";
        return false;
    }

    DWORD timeout = 3000;
    setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    m_serverAddr.sin_family = AF_INET;
    m_serverAddr.sin_addr.s_addr = inet_addr(SERVER_IP);
    m_serverAddr.sin_port = htons(SERVER_PORT);

    std::string response;
    if (!SendAndReceive("heartbeat", response)) {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
        m_lastError = "无法连接到服务器";
        return false;
    }

    m_connected = true;
    m_heartbeatRunning = true;
    m_heartbeatThread = std::thread(&UDPClient::HeartbeatThread, this);

    return true;
}

void UDPClient::Disconnect() {
    m_heartbeatRunning = false;
    m_connected = false;
    m_connectedPID = 0;

    if (m_heartbeatThread.joinable()) {
        m_heartbeatThread.join();
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_socket != INVALID_SOCKET) {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }
}

bool UDPClient::IsConnected() const {
    return m_connected;
}

bool UDPClient::SendAndReceive(const std::string& command, std::string& response) {
    if (m_socket == INVALID_SOCKET) {
        m_lastError = "未连接";
        return false;
    }

    int sendResult = sendto(m_socket, command.c_str(), (int)command.length(), 0,
        (sockaddr*)&m_serverAddr, sizeof(m_serverAddr));

    if (sendResult == SOCKET_ERROR) {
        m_lastError = "发送失败";
        return false;
    }

    char buffer[1024];
    sockaddr_in fromAddr;
    int fromLen = sizeof(fromAddr);

    int recvResult = recvfrom(m_socket, buffer, sizeof(buffer) - 1, 0,
        (sockaddr*)&fromAddr, &fromLen);

    if (recvResult == SOCKET_ERROR) {
        int error = WSAGetLastError();
        if (error == WSAETIMEDOUT) {
            m_lastError = "接收超时";
        }
        else {
            m_lastError = "接收失败";
        }
        return false;
    }

    buffer[recvResult] = '\0';
    response = buffer;
    m_lastResponse = response;
    return true;
}

bool UDPClient::SendCommand(const std::string& command) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string response;
    bool result = SendAndReceive(command, response);
    return result && (response == "OK" || response == "alive");
}

std::string UDPClient::GetLastResponse() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastResponse;
}

std::string UDPClient::GetLastError() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastError;
}

void UDPClient::HeartbeatThread() {
    while (m_heartbeatRunning) {
        // 心跳
        std::this_thread::sleep_for(std::chrono::seconds(1));

        if (!m_heartbeatRunning) break;

        std::lock_guard<std::mutex> lock(m_mutex);
        std::string response;
        if (!SendAndReceive("heartbeat", response)) {
            m_connected = false;
            m_connectedPID = 0;
            if (g_hMainWnd) {
                PostMessage(g_hMainWnd, WM_CONNECTION_LOST, 0, 0);
            }
            break;
        }
    }
}

bool UDPClient::EnableFPSOverride(bool enable) {
    return SendCommand(enable ? "enable_fps_override" : "disable_fps_override");
}

bool UDPClient::EnableFOVOverride(bool enable) {
    return SendCommand(enable ? "enable_fov_override" : "disable_fov_override");
}

bool UDPClient::EnableFogOverride(bool enable) {
    return SendCommand(enable ? "enable_display_fog_override" : "disable_display_fog_override");
}

bool UDPClient::EnablePerspectiveOverride(bool enable) {
    return SendCommand(enable ? "enable_Perspective_override" : "disable_Perspective_override");
}

bool UDPClient::EnableSyncountOverride(bool enable) {
    return SendCommand(enable ? "enable_syncount_override" : "disable_syncount_override");
}

bool UDPClient::SetFPS(int fps) {
    std::stringstream ss;
    ss << "set_fps " << fps;
    return SendCommand(ss.str());
}

bool UDPClient::SetFOV(float fov) {
    std::stringstream ss;
    ss << "set_fov " << std::fixed << std::setprecision(1) << fov;
    return SendCommand(ss.str());
}

bool UDPClient::GetStatus() {
    return SendCommand("get_status");
}




void AutoConnectThreadFunc(HWND hWnd) {
    while (g_autoConnectRunning) {
        if (!g_client.IsConnected()) {
            if (g_client.Connect()) {
                auto processes = FindTargetProcesses();
                if (!processes.empty()) {
                    g_client.SetConnectedPID(processes[0].pid);
                    g_connectedProcessName = processes[0].name;
                    PostMessage(hWnd, WM_AUTO_CONNECT_RESULT, TRUE, processes[0].pid);
                }
            }
        }


        for (int i = 0; i < 20 && g_autoConnectRunning; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void StartAutoConnect(HWND hWnd) {
    if (g_autoConnectRunning) return;

    g_autoConnectRunning = true;
    g_autoConnectThread = std::thread(AutoConnectThreadFunc, hWnd);
}

void StopAutoConnect() {
    g_autoConnectRunning = false;
    if (g_autoConnectThread.joinable()) {
        g_autoConnectThread.join();
    }
}



void CreateControls(HWND hWnd) {
    HFONT hFont = CreateFont(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");

    HFONT hFontBold = CreateFont(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");

    int y = 10;


    HWND hGroupConn = CreateWindowEx(0, L"BUTTON", L"连接状态",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        10, y, 360, 70, hWnd, (HMENU)IDC_GROUP_CONNECTION, g_hInstance, nullptr);
    SendMessage(hGroupConn, WM_SETFONT, (WPARAM)hFont, TRUE);

    y += 25;

    g_hStaticProcessInfo = CreateWindowEx(0, L"STATIC", L"等待游戏启动...",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        20, y, 340, 22, hWnd, (HMENU)IDC_STATIC_PROCESS_INFO, g_hInstance, nullptr);
    SendMessage(g_hStaticProcessInfo, WM_SETFONT, (WPARAM)hFontBold, TRUE);

    y += 24;

    g_hStaticStatus = CreateWindowEx(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        20, y, 340, 18, hWnd, (HMENU)IDC_STATIC_STATUS, g_hInstance, nullptr);
    SendMessage(g_hStaticStatus, WM_SETFONT, (WPARAM)hFont, TRUE);

    y = 90;

    HWND hGroupFPS = CreateWindowEx(0, L"BUTTON", L"FPS 设置",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        10, y, 360, 80, hWnd, (HMENU)IDC_GROUP_FPS, g_hInstance, nullptr);
    SendMessage(hGroupFPS, WM_SETFONT, (WPARAM)hFont, TRUE);

    y += 20;
    g_hCheckFPS = CreateWindowEx(0, L"BUTTON", L"启用 FPS 覆盖",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_DISABLED,
        20, y, 120, 20, hWnd, (HMENU)IDC_CHECK_FPS_OVERRIDE, g_hInstance, nullptr);
    SendMessage(g_hCheckFPS, WM_SETFONT, (WPARAM)hFont, TRUE);

    y += 25;
    CreateWindowEx(0, L"STATIC", L"FPS:",
        WS_CHILD | WS_VISIBLE,
        20, y + 3, 30, 20, hWnd, nullptr, g_hInstance, nullptr);

    g_hSliderFPS = CreateWindowEx(0, TRACKBAR_CLASS, L"",
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS | WS_DISABLED,
        55, y, 180, 25, hWnd, (HMENU)IDC_SLIDER_FPS, g_hInstance, nullptr);
    SendMessage(g_hSliderFPS, TBM_SETRANGE, TRUE, MAKELPARAM(30, 360));
    SendMessage(g_hSliderFPS, TBM_SETPOS, TRUE, 60);
    SendMessage(g_hSliderFPS, TBM_SETTICFREQ, 30, 0);

    g_hEditFPSValue = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"60",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER | ES_CENTER | WS_DISABLED,
        240, y + 3, 40, 20, hWnd, (HMENU)IDC_EDIT_FPS_VALUE, g_hInstance, nullptr);
    SendMessage(g_hEditFPSValue, WM_SETFONT, (WPARAM)hFont, TRUE);

    CreateWindowEx(0, L"BUTTON", L"应用",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_DISABLED,
        290, y, 60, 24, hWnd, (HMENU)IDC_BTN_APPLY_FPS, g_hInstance, nullptr);

    y = 180;

    HWND hGroupFOV = CreateWindowEx(0, L"BUTTON", L"FOV 设置",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        10, y, 360, 80, hWnd, (HMENU)IDC_GROUP_FOV, g_hInstance, nullptr);
    SendMessage(hGroupFOV, WM_SETFONT, (WPARAM)hFont, TRUE);

    y += 20;
    g_hCheckFOV = CreateWindowEx(0, L"BUTTON", L"启用 FOV 覆盖",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_DISABLED,
        20, y, 120, 20, hWnd, (HMENU)IDC_CHECK_FOV_OVERRIDE, g_hInstance, nullptr);
    SendMessage(g_hCheckFOV, WM_SETFONT, (WPARAM)hFont, TRUE);

    y += 25;
    CreateWindowEx(0, L"STATIC", L"FOV:",
        WS_CHILD | WS_VISIBLE,
        20, y + 3, 30, 20, hWnd, nullptr, g_hInstance, nullptr);

    g_hSliderFOV = CreateWindowEx(0, TRACKBAR_CLASS, L"",
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS | WS_DISABLED,
        55, y, 180, 25, hWnd, (HMENU)IDC_SLIDER_FOV, g_hInstance, nullptr);
    SendMessage(g_hSliderFOV, TBM_SETRANGE, TRUE, MAKELPARAM(30, 120));
    SendMessage(g_hSliderFOV, TBM_SETPOS, TRUE, 90);
    SendMessage(g_hSliderFOV, TBM_SETTICFREQ, 10, 0);

    g_hEditFOVValue = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"90.0",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_CENTER | WS_DISABLED,
        240, y + 3, 40, 20, hWnd, (HMENU)IDC_EDIT_FOV_VALUE, g_hInstance, nullptr);
    SendMessage(g_hEditFOVValue, WM_SETFONT, (WPARAM)hFont, TRUE);

    CreateWindowEx(0, L"BUTTON", L"应用",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_DISABLED,
        290, y, 60, 24, hWnd, (HMENU)IDC_BTN_APPLY_FOV, g_hInstance, nullptr);

    y = 270;

    HWND hGroupOptions = CreateWindowEx(0, L"BUTTON", L"其他选项",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        10, y, 360, 80, hWnd, (HMENU)IDC_GROUP_OPTIONS, g_hInstance, nullptr);
    SendMessage(hGroupOptions, WM_SETFONT, (WPARAM)hFont, TRUE);

    y += 20;
    g_hCheckFog = CreateWindowEx(0, L"BUTTON", L"启用雾效覆盖",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_DISABLED,
        20, y, 150, 20, hWnd, (HMENU)IDC_CHECK_FOG_OVERRIDE, g_hInstance, nullptr);
    SendMessage(g_hCheckFog, WM_SETFONT, (WPARAM)hFont, TRUE);

    g_hCheckPerspective = CreateWindowEx(0, L"BUTTON", L"启用视角覆盖",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_DISABLED,
        190, y, 150, 20, hWnd, (HMENU)IDC_CHECK_PERSPECTIVE, g_hInstance, nullptr);
    SendMessage(g_hCheckPerspective, WM_SETFONT, (WPARAM)hFont, TRUE);

    y += 25;
    g_hCheckSyncount = CreateWindowEx(0, L"BUTTON", L"启用同步计数覆盖",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_DISABLED,
        20, y, 150, 20, hWnd, (HMENU)IDC_CHECK_SYNCOUNT, g_hInstance, nullptr);
    SendMessage(g_hCheckSyncount, WM_SETFONT, (WPARAM)hFont, TRUE);


    EnumChildWindows(hWnd, [](HWND hwnd, LPARAM lParam) -> BOOL {
        wchar_t className[64];
        GetClassName(hwnd, className, 64);
        if (wcscmp(className, L"Static") == 0) {
            if (hwnd != g_hStaticProcessInfo) {
                SendMessage(hwnd, WM_SETFONT, (WPARAM)lParam, TRUE);
            }
        }
        return TRUE;
        }, (LPARAM)hFont);
}

void UpdateConnectionStatus(HWND hWnd, bool connected, DWORD pid, const wchar_t* processName) {
    EnableWindow(g_hCheckFPS, connected);
    EnableWindow(g_hCheckFOV, connected);
    EnableWindow(g_hCheckFog, connected);
    EnableWindow(g_hCheckPerspective, connected);
    EnableWindow(g_hCheckSyncount, connected);
    EnableWindow(g_hSliderFPS, connected);
    EnableWindow(g_hSliderFOV, connected);
    EnableWindow(g_hEditFPSValue, connected);
    EnableWindow(g_hEditFOVValue, connected);
    EnableWindow(GetDlgItem(hWnd, IDC_BTN_APPLY_FPS), connected);
    EnableWindow(GetDlgItem(hWnd, IDC_BTN_APPLY_FOV), connected);

    if (connected && pid > 0) {
        wchar_t info[256];
        if (processName && wcslen(processName) > 0) {
            swprintf_s(info, L"已连接: %s [PID: %d]", processName, pid);
        }
        else {
            swprintf_s(info, L"已连接: 进程 [PID: %d]", pid);
        }
        SetWindowText(g_hStaticProcessInfo, info);
        SetWindowText(g_hStaticStatus, L"● 连接正常");
    }
    else {
        SetWindowText(g_hStaticProcessInfo, L"等待游戏启动...");
    }
}

void OnCheckboxChanged(HWND hWnd, int id, bool checked) {
    bool result = false;
    const wchar_t* feature = L"";

    switch (id) {
    case IDC_CHECK_FPS_OVERRIDE:
        result = g_client.EnableFPSOverride(checked);
        feature = L"FPS覆盖";
        if (result) g_config.fpsOverrideEnabled = checked;
        break;
    case IDC_CHECK_FOV_OVERRIDE:
        result = g_client.EnableFOVOverride(checked);
        feature = L"FOV覆盖";
        if (result) g_config.fovOverrideEnabled = checked;
        break;
    case IDC_CHECK_FOG_OVERRIDE:
        result = g_client.EnableFogOverride(checked);
        feature = L"雾效覆盖";
        if (result) g_config.fogOverrideEnabled = checked;
        break;
    case IDC_CHECK_PERSPECTIVE:
        result = g_client.EnablePerspectiveOverride(checked);
        feature = L"视角覆盖";
        if (result) g_config.perspectiveOverrideEnabled = checked;
        break;
    case IDC_CHECK_SYNCOUNT:
        result = g_client.EnableSyncountOverride(checked);
        feature = L"同步计数覆盖";
        if (result) g_config.syncountOverrideEnabled = checked;
        break;
    }

    if (!result) {
        wchar_t msg[256];
        swprintf_s(msg, L"设置%s失败", feature);
        MessageBox(hWnd, msg, L"错误", MB_OK | MB_ICONERROR);
        HWND hCheck = GetDlgItem(hWnd, id);
        SendMessage(hCheck, BM_SETCHECK, checked ? BST_UNCHECKED : BST_CHECKED, 0);
    }
    else {
        SaveConfig();
    }
}

void OnSliderChanged(HWND hWnd, int id) {
    if (g_valueEditUpdating || g_sliderSyncInProgress) {
        return;
    }
    if (id == IDC_SLIDER_FPS) {
        int pos = (int)SendMessage(g_hSliderFPS, TBM_GETPOS, 0, 0);
        UpdateFPSValueField(pos);
    }
    else if (id == IDC_SLIDER_FOV) {
        int pos = (int)SendMessage(g_hSliderFOV, TBM_GETPOS, 0, 0);
        UpdateFOVValueField((float)pos);
    }
}

void OnApplyFPS(HWND hWnd) {
    int fps = (int)SendMessage(g_hSliderFPS, TBM_GETPOS, 0, 0);
    if (!g_client.SetFPS(fps)) {
        MessageBox(hWnd, L"设置FPS失败", L"错误", MB_OK | MB_ICONERROR);
    }
    else {
        g_config.fpsValue = fps;
        SaveConfig();
    }
}

void OnApplyFOV(HWND hWnd) {
    int fov = (int)SendMessage(g_hSliderFOV, TBM_GETPOS, 0, 0);
    if (!g_client.SetFOV((float)fov)) {
        MessageBox(hWnd, L"设置FOV失败", L"错误", MB_OK | MB_ICONERROR);
    }
    else {
        g_config.fovValue = fov;
        SaveConfig();
    }
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        CreateControls(hWnd);
        LoadConfig();
        ApplyConfigToUI(hWnd);
        StartAutoConnect(hWnd);
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_CHECK_FPS_OVERRIDE:
        case IDC_CHECK_FOV_OVERRIDE:
        case IDC_CHECK_FOG_OVERRIDE:
        case IDC_CHECK_PERSPECTIVE:
        case IDC_CHECK_SYNCOUNT:
            if (HIWORD(wParam) == BN_CLICKED) {
                bool checked = SendMessage((HWND)lParam, BM_GETCHECK, 0, 0) == BST_CHECKED;
                OnCheckboxChanged(hWnd, LOWORD(wParam), checked);
            }
            break;
        case IDC_EDIT_FPS_VALUE:
            if (HIWORD(wParam) == EN_KILLFOCUS) {
                CommitFPSValueFromEdit(hWnd);
            }
            break;
        case IDC_EDIT_FOV_VALUE:
            if (HIWORD(wParam) == EN_KILLFOCUS) {
                CommitFOVValueFromEdit(hWnd);
            }
            break;
        case IDC_BTN_APPLY_FPS:
            OnApplyFPS(hWnd);
            break;
        case IDC_BTN_APPLY_FOV:
            OnApplyFOV(hWnd);
            break;
        case ID_TRAY_RESTORE:
            ShowWindow(hWnd, SW_RESTORE);
            RemoveTrayIcon();
            break;
        case ID_TRAY_EXIT:
            DestroyWindow(hWnd);
            break;
        }
        break;

    case WM_HSCROLL:
        if ((HWND)lParam == g_hSliderFPS) {
            OnSliderChanged(hWnd, IDC_SLIDER_FPS);
        }
        else if ((HWND)lParam == g_hSliderFOV) {
            OnSliderChanged(hWnd, IDC_SLIDER_FOV);
        }
        break;

    case WM_CLOSE:
        ShowWindow(hWnd, SW_HIDE);
        AddTrayIcon(hWnd);
        return 0;

    case WM_TRAY_ICON:
        if (lParam == WM_RBUTTONUP) {
            ShowTrayMenu(hWnd);
        }
        else if (lParam == WM_LBUTTONDBLCLK) {
            ShowWindow(hWnd, SW_RESTORE);
            RemoveTrayIcon();
        }
        break;

    case WM_CONNECTION_LOST:
        g_connectedProcessName.clear();
        UpdateConnectionStatus(hWnd, false, 0, nullptr);
        // 连接断开后自动关闭程序
        DestroyWindow(hWnd);
        break;

    case WM_AUTO_CONNECT_RESULT:
        if (wParam) {
            DWORD pid = (DWORD)lParam;
            UpdateConnectionStatus(hWnd, true, pid, g_connectedProcessName.c_str());

            // 连接成功后应用保存的配置到服务器
            if (g_config.fpsOverrideEnabled) {
                g_client.EnableFPSOverride(true);
                g_client.SetFPS(g_config.fpsValue);
            }
            if (g_config.fovOverrideEnabled) {
                g_client.EnableFOVOverride(true);
                g_client.SetFOV((float)g_config.fovValue);
            }
            if (g_config.fogOverrideEnabled) {
                g_client.EnableFogOverride(true);
            }
            if (g_config.perspectiveOverrideEnabled) {
                g_client.EnablePerspectiveOverride(true);
            }
            if (g_config.syncountOverrideEnabled) {
                g_client.EnableSyncountOverride(true);
            }
        }
        break;

    case WM_DESTROY:
        RemoveTrayIcon();
        StopAutoConnect();
        g_client.Disconnect();
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow) {
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    g_hInstance = hInstance;

    // 初始化
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icex);

    // 注册窗口
    WNDCLASSEX wcex = { 0 };
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = (HICON)LoadIcon(hInstance, MAKEINTRESOURCE(IDR_MAINICON));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = nullptr;
    wcex.lpszClassName = WINDOW_CLASS_NAME;
    wcex.hIconSm = (HICON)LoadIcon(hInstance, MAKEINTRESOURCE(IDR_MAINICON));

    if (!RegisterClassEx(&wcex)) {
        MessageBox(nullptr, L"窗口类注册失败", L"错误", MB_OK | MB_ICONERROR);
        return 1;
    }

    // 创建窗口
    g_hMainWnd = CreateWindowEx(
        0,
        WINDOW_CLASS_NAME,
        WINDOW_TITLE,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        395, 400,
        nullptr, nullptr, hInstance, nullptr);


    ShowWindow(g_hMainWnd, nCmdShow);
    UpdateWindow(g_hMainWnd);

    AddTrayIcon(g_hMainWnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}
