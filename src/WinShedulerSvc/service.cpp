#include "service.h"
#include "database.h"
#include "scheduler.h"
#include "ipc.h"
#include <sstream>
#include <iomanip>

static Service* g_service = nullptr;
static Database* g_db = nullptr;
static Scheduler* g_sched = nullptr;
static IpcServer* g_ipc = nullptr;

Service& Service::instance() {
    static Service s;
    return s;
}

void Service::stop() {
    if (g_sched) g_sched->stop();
    if (g_ipc) g_ipc->stop();
    if (status_handle_) {
        status_.dwCurrentState = SERVICE_STOPPED;
        SetServiceStatus(status_handle_, &status_);
    }
}

bool Service::run(const std::wstring& service_name,
                  const std::wstring& db_path,
                  const std::wstring& log_dir) {
    db_path_ = db_path;
    log_dir_ = log_dir;

    SERVICE_TABLE_ENTRYW table[] = {
        { const_cast<wchar_t*>(service_name.c_str()), ServiceMainWrapper },
        { nullptr, nullptr }
    };

    return StartServiceCtrlDispatcherW(table) != FALSE;
}

void WINAPI Service::service_main(DWORD argc, wchar_t** argv) {
    auto& svc = Service::instance();
    svc.status_handle_ = RegisterServiceCtrlHandlerExW(
        L"WinSheduler", ServiceCtrlHandlerEx, nullptr);

    if (!svc.status_handle_) return;

    svc.status_.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    svc.status_.dwCurrentState = SERVICE_START_PENDING;
    svc.status_.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    svc.status_.dwWin32ExitCode = NO_ERROR;
    svc.status_.dwServiceSpecificExitCode = 0;
    svc.status_.dwCheckPoint = 0;
    svc.status_.dwWaitHint = 5000;
    SetServiceStatus(svc.status_handle_, &svc.status_);

    // Initialize
    try {
        CreateDirectoryW((svc.log_dir_ + L"\\output").c_str(), nullptr);

        g_db = new Database();
        if (!g_db->open(svc.db_path_)) {
            svc.status_.dwCurrentState = SERVICE_STOPPED;
            svc.status_.dwWin32ExitCode = ERROR_SERVICE_SPECIFIC_ERROR;
            SetServiceStatus(svc.status_handle_, &svc.status_);
            return;
        }

        g_sched = new Scheduler(*g_db, svc.log_dir_);

        g_ipc = new IpcServer(*g_db, [](const IpcRequest& req) -> IpcResponse {
            if (req.action == L"GetStatus") {
                auto status = g_sched->get_status();
                auto json_str = json_helpers::status_to_json(status);
                return IpcResponse{ true, L"", u2ws(json_str) };
            }
            if (req.action == L"RunNow") {
                auto task_opt = g_db->get_task(req.payload);
                if (!task_opt.has_value())
                    return IpcResponse{ false, L"Task not found", L"" };
                g_sched->launch_task(task_opt.value());
                return IpcResponse{ true, L"", L"{}" };
            }
            return IpcResponse{ false, L"Unhandled by scheduler", L"" };
        });

        g_sched->start();
        g_ipc->start();

        svc.status_.dwCurrentState = SERVICE_RUNNING;
        SetServiceStatus(svc.status_handle_, &svc.status_);

        svc.stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        WaitForSingleObject(svc.stop_event_, INFINITE);

    } catch (...) {
        svc.stop();
        ExitProcess(0);
    }

    svc.stop();
    ExitProcess(0);
}

DWORD WINAPI Service::service_ctrl(DWORD ctrl, DWORD type, void* context, void* raw) {
    auto& svc = Service::instance();
    switch (ctrl) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        svc.status_.dwCurrentState = SERVICE_STOP_PENDING;
        svc.status_.dwWaitHint = 10000;
        SetServiceStatus(svc.status_handle_, &svc.status_);
        if (svc.stop_event_) SetEvent(svc.stop_event_);
        break;
    case SERVICE_CONTROL_INTERROGATE:
        SetServiceStatus(svc.status_handle_, &svc.status_);
        break;
    }
    return NO_ERROR;
}

// Global wrappers
void __stdcall ServiceMainWrapper(DWORD argc, wchar_t** argv) {
    Service::service_main(argc, argv);
}

DWORD __stdcall ServiceCtrlHandlerEx(DWORD ctrl, DWORD type, void* raw1, void* raw2) {
    return Service::service_ctrl(ctrl, type, raw1, raw2);
}
