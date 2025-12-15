#pragma once

#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <winsock2.h>
#include <windows.h>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <TlHelp32.h>
#include <ShlObj.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "comctl32.lib")

#define SERVER_IP   "127.0.0.1"
#define SERVER_PORT 12345

// 控件ID定义
#define IDC_STATIC_STATUS       1005
#define IDC_GROUP_CONNECTION    1006
#define IDC_GROUP_FPS           1007
#define IDC_GROUP_FOV           1008
#define IDC_GROUP_OPTIONS       1009
#define IDC_CHECK_FPS_OVERRIDE  1010
#define IDC_CHECK_FOV_OVERRIDE  1011
#define IDC_CHECK_FOG_OVERRIDE  1012
#define IDC_CHECK_PERSPECTIVE   1013
#define IDC_CHECK_SYNCOUNT      1014
#define IDC_SLIDER_FPS          1015
#define IDC_SLIDER_FOV          1016
#define IDC_STATIC_FPS_VALUE    1017
#define IDC_STATIC_FOV_VALUE    1018
#define IDC_BTN_APPLY_FPS       1019
#define IDC_BTN_APPLY_FOV       1020
#define IDC_STATIC_PID          1021
#define IDC_STATIC_PROCESS_INFO 1025
#define IDI_ICON1 1026
#define IDC_EDIT_FPS_VALUE      1027
#define IDC_EDIT_FOV_VALUE      1028

#define WM_CONNECTION_LOST      (WM_USER + 1)
#define WM_AUTO_CONNECT_RESULT  (WM_USER + 2)
#define WM_TRAY_ICON            (WM_USER + 3)

#define ID_TRAY_RESTORE         2001
#define ID_TRAY_EXIT            2002

#define WINDOW_CLASS_NAME L"Client"
#define WINDOW_TITLE      L"芙芙"

#define TARGET_PROCESS_NAME L"yuanshen.exe"
#define TARGET_PROCESS_NAME_ALT L"GenshinImpact.exe"

// 配置结构
struct AppConfig {
    bool fpsOverrideEnabled = false;
    bool fovOverrideEnabled = false;
    bool fogOverrideEnabled = false;
    bool perspectiveOverrideEnabled = false;
    bool syncountOverrideEnabled = false;
    int fpsValue = 60;
    int fovValue = 90;
};

extern AppConfig g_config;

// 配置管理函数
std::wstring GetConfigFilePath();
bool SaveConfig();
bool LoadConfig();
void ApplyConfigToUI(HWND hWnd);

// 进程信息结构
struct ProcessInfo {
    DWORD pid = 0;
    std::wstring name;
};

// 客户端
class UDPClient {
public:
    UDPClient();
    ~UDPClient();

    bool Connect();
    void Disconnect();
    bool IsConnected() const;
    
    bool SendCommand(const std::string& command);
    std::string GetLastResponse() const;
    std::string GetLastError() const;

    // 命令封装
    bool EnableFPSOverride(bool enable);
    bool EnableFOVOverride(bool enable);
    bool EnableFogOverride(bool enable);
    bool EnablePerspectiveOverride(bool enable);
    bool EnableSyncountOverride(bool enable);
    bool SetFPS(int fps);
    bool SetFOV(float fov);
    bool GetStatus();

    // 获取连接的进程PID
    DWORD GetConnectedPID() const { return m_connectedPID; }
    void SetConnectedPID(DWORD pid) { m_connectedPID = pid; }

private:
    void HeartbeatThread();
    bool SendAndReceive(const std::string& command, std::string& response);

    SOCKET m_socket;
    sockaddr_in m_serverAddr;
    std::atomic<bool> m_connected;
    std::thread m_heartbeatThread;
    std::atomic<bool> m_heartbeatRunning;
    std::string m_lastResponse;
    std::string m_lastError;
    mutable std::mutex m_mutex;
    DWORD m_connectedPID;
};


extern UDPClient g_client;


std::vector<ProcessInfo> FindTargetProcesses();


LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
void CreateControls(HWND hWnd);
void UpdateConnectionStatus(HWND hWnd, bool connected, DWORD pid = 0, const wchar_t* processName = nullptr);
void OnCheckboxChanged(HWND hWnd, int id, bool checked);
void OnSliderChanged(HWND hWnd, int id);
void OnApplyFPS(HWND hWnd);
void OnApplyFOV(HWND hWnd);
void StartAutoConnect(HWND hWnd);
void StopAutoConnect();
