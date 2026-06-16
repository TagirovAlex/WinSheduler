#pragma once
#include "config.h"

class Service {
public:
    static Service& instance();

    bool run(const std::wstring& service_name,
             const std::wstring& db_path,
             const std::wstring& log_dir);

    void stop();

    static DWORD WINAPI service_ctrl(DWORD ctrl, DWORD type, void* context, void* raw);
    static void WINAPI service_main(DWORD argc, wchar_t** argv);

private:
    Service() = default;
    ~Service() = default;
    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;

    std::wstring db_path_;
    std::wstring log_dir_;
    SERVICE_STATUS_HANDLE status_handle_ = nullptr;
    SERVICE_STATUS status_ = {};
};

// Global callback wrappers
extern "C" {
    void __stdcall ServiceMainWrapper(DWORD argc, wchar_t** argv);
    DWORD __stdcall ServiceCtrlHandlerEx(DWORD ctrl, DWORD type, void* raw1, void* raw2);
}
