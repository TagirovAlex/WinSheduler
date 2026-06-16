#include "database.h"
#include "scheduler.h"
#include "ipc.h"
#include "service.h"
#include <shellapi.h>

// For standalone test without service registration
static void run_standalone(const std::wstring& db_path, const std::wstring& log_dir) {
    CreateDirectoryW((log_dir + L"\\output").c_str(), nullptr);

    Database db;
    if (!db.open(db_path)) {
        MessageBoxW(nullptr, L"Failed to open database", L"WinSheduler", MB_ICONERROR);
        return;
    }

    Scheduler sched(db, log_dir);
    IpcServer ipc(db, [&](const IpcRequest& req) -> IpcResponse {
        if (req.action == L"GetStatus") {
            auto status = sched.get_status();
            return IpcResponse{ true, L"", u2ws(json_helpers::status_to_json(status)) };
        }
        if (req.action == L"RunNow") {
            auto task_opt = db.get_task(req.payload);
            if (!task_opt.has_value())
                return IpcResponse{ false, L"Task not found", L"" };
            sched.launch_task(task_opt.value());
            return IpcResponse{ true, L"", L"{}" };
        }
        return IpcResponse{ false, L"Unhandled", L"" };
    });

    sched.start();
    ipc.start();

    wprintf(L"WinSheduler running in standalone mode. Stop with: taskkill /PID %d\n", GetCurrentProcessId());
    // Use console control to listen for Ctrl+C, or wait forever (kill process externally)
    HANDLE hEvent = CreateEventW(nullptr, TRUE, FALSE, L"Global\\WinShedulerStopEvent");
    if (hEvent) {
        WaitForSingleObject(hEvent, INFINITE);
        CloseHandle(hEvent);
    } else {
        // Fallback: wait forever until terminated
        Sleep(INFINITE);
    }

    ipc.stop();
    sched.stop();
    db.close();
}

int wmain(int argc, wchar_t* argv[]) {
    // Determine paths
    wchar_t module_path[MAX_PATH];
    GetModuleFileNameW(nullptr, module_path, MAX_PATH);
    std::wstring exe_dir = module_path;
    auto pos = exe_dir.find_last_of(L"\\");
    if (pos != std::wstring::npos) exe_dir = exe_dir.substr(0, pos);

    std::wstring db_path = exe_dir + L"\\data\\scheduler.db";
    std::wstring log_dir = exe_dir + L"\\logs";

    // Create directories
    CreateDirectoryW((exe_dir + L"\\data").c_str(), nullptr);
    CreateDirectoryW(log_dir.c_str(), nullptr);

    // Parse command line
    bool standalone = false;
    for (int i = 1; i < argc; ++i) {
        if (wcscmp(argv[i], L"--standalone") == 0 || wcscmp(argv[i], L"-s") == 0) {
            standalone = true;
        }
        if (wcscmp(argv[i], L"--dbpath") == 0 && i + 1 < argc) {
            db_path = argv[++i];
        }
        if (wcscmp(argv[i], L"--logdir") == 0 && i + 1 < argc) {
            log_dir = argv[++i];
        }
    }

    if (standalone) {
        run_standalone(db_path, log_dir);
        return 0;
    }

    // Run as Windows Service
    Service::instance().run(L"WinSheduler", db_path, log_dir);
    return 0;
}
