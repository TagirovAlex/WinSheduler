#pragma once
#include <windows.h>
#include <wincrypt.h>
#include <string>
#include <vector>
#include <optional>
#include <chrono>
#include <algorithm>

// UTF-8 <-> UTF-16 conversion helpers (inline to avoid LNK2005)
inline std::string ws2u(const std::wstring& ws) {
    if (ws.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.data(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    std::string s(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, ws.data(), (int)ws.size(), s.data(), len, nullptr, nullptr);
    return s;
}

inline std::wstring u2ws(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring ws(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), ws.data(), len);
    return ws;
}

// Pipe name must match C# UI: "WinSheduler"
inline constexpr const wchar_t* PIPE_NAME = L"\\\\.\\pipe\\WinSheduler";
inline constexpr int PIPE_BUFFER_SIZE = 65536;
inline constexpr int SCHEDULER_TICK_MS = 1000;
inline constexpr int CACHE_INVALIDATE_SEC = 5;
inline constexpr int MAX_HISTORY_FETCH = 100;
inline constexpr int MAX_RETRY_COUNT = 10;
inline constexpr int RETRY_DELAY_MS = 5000;

// Enums matching C# WinSheduler.Shared
enum class WindowStyle : int { Normal = 0, Hidden = 1, Minimized = 2, Maximized = 3 };
enum class OnErrorAction : int { SkipNext = 0, Retry = 1, Fail = 2 };
enum class OnOverlapAction : int { WaitComplete = 0, WaitNext = 1, StartAnother = 2 };
enum class TimeWindowType : int { ExactTimes = 0, Interval = 1 };
enum class RunStatus : int { Running = 0, Completed = 1, Failed = 2, Killed = 3, Timeout = 4 };

inline const wchar_t* ToWide(RunStatus s) {
    switch (s) {
    case RunStatus::Running: return L"Running";
    case RunStatus::Completed: return L"Completed";
    case RunStatus::Failed: return L"Failed";
    case RunStatus::Killed: return L"Killed";
    case RunStatus::Timeout: return L"Timeout";
    default: return L"Unknown";
    }
}

struct RunAsCredentials {
    std::wstring user_name;
    std::wstring domain;
    std::wstring encrypted_password;
};

struct TimeWindow {
    std::wstring id;
    std::wstring task_id;
    TimeWindowType type = TimeWindowType::ExactTimes;
    std::optional<std::wstring> start_date;
    std::optional<std::wstring> end_date;
    std::vector<int> days_of_week;      // 0=Sun, 1=Mon...
    std::vector<int> days_of_month;
    std::vector<std::wstring> specific_dates;
    std::vector<std::wstring> exact_times;
    std::optional<std::wstring> start_time;
    std::optional<std::wstring> end_time;
    std::optional<int> repeat_interval_minutes;
    std::optional<std::wstring> repeat_until;
};

struct TaskDefinition {
    std::wstring id;
    std::wstring name;
    std::wstring description;
    bool enabled = true;
    std::wstring program_path;
    std::wstring arguments;
    std::wstring working_directory;
    WindowStyle window_style = WindowStyle::Normal;
    std::optional<RunAsCredentials> run_as_user;
    OnErrorAction on_error = OnErrorAction::SkipNext;
    int retry_count = 0;
    OnOverlapAction on_overlap = OnOverlapAction::WaitComplete;
    std::optional<int> timeout_minutes;
    bool kill_on_timeout = false;
    bool log_output = false;
    int max_history_records = 1000;
    std::wstring created_at;
    std::wstring modified_at;
};

struct RunHistory {
    std::wstring id;
    std::wstring task_id;
    std::wstring start_time;
    std::optional<std::wstring> end_time;
    std::optional<int> exit_code;
    int pid = 0;
    RunStatus status = RunStatus::Running;
    std::wstring error_message;
    std::optional<std::wstring> output_path;
};

struct ServiceStatus {
    bool running = true;
    int task_count = 0;
    int running_task_count = 0;
    std::wstring started_at;
};

// IPC messages (mirrors WinSheduler.Shared)
struct IpcRequest {
    std::wstring action;
    std::wstring payload;
};

struct IpcResponse {
    bool success = false;
    std::wstring error;
    std::wstring payload;
};
