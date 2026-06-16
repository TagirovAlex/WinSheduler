#pragma once
#include "config.h"
#include "database.h"
#include <functional>
#include <thread>
#include <atomic>

class IpcServer {
public:
    using RequestHandler = std::function<IpcResponse(const IpcRequest&)>;

    IpcServer(Database& db, RequestHandler scheduler_handler);
    ~IpcServer();
    void start();
    void stop();

private:
    void worker_thread();
    void handle_client(HANDLE pipe);
    void handle_client_core(HANDLE pipe);
    IpcResponse process_request(const IpcRequest& req);
    IpcResponse handle_list_tasks();
    IpcResponse handle_get_task(const std::wstring& payload);
    IpcResponse handle_get_time_windows(const std::wstring& payload);
    IpcResponse handle_get_history(const std::wstring& payload);
    IpcResponse handle_create_task(const std::wstring& payload);
    IpcResponse handle_update_task(const std::wstring& payload);
    IpcResponse handle_delete_task(const std::wstring& payload);
    IpcResponse handle_toggle_task(const std::wstring& payload, bool enabled);

    Database& db_;
    RequestHandler scheduler_handler_;
    std::thread thread_;
    std::atomic<bool> running_{ false };
    HANDLE stop_event_ = nullptr;
};

// JSON serialization helpers used by both IPC and Scheduler
namespace json_helpers {
    std::string task_to_json(const TaskDefinition& t);
    std::string task_list_to_json(const std::vector<TaskDefinition>& tasks);
    std::string time_window_to_json(const TimeWindow& w);
    std::string time_window_list_to_json(const std::vector<TimeWindow>& windows);
    std::string history_to_json(const RunHistory& h);
    std::string history_list_to_json(const std::vector<RunHistory>& history);
    std::string status_to_json(const ServiceStatus& s);

    std::optional<TaskDefinition> task_from_json(const std::string& json_str);
    std::vector<TimeWindow> time_windows_from_json(const std::string& json_str);
    std::optional<IpcRequest> request_from_json(const std::string& json_str);
    std::string response_to_json(const IpcResponse& resp);
}
