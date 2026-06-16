#pragma once
#include "config.h"
#include "database.h"
#include <atomic>
#include <thread>
#include <unordered_map>
#include <queue>

class Scheduler {
public:
    Scheduler(Database& db, const std::wstring& log_dir);
    ~Scheduler();

    void start();
    void stop();
    ServiceStatus get_status();
    void launch_task(const TaskDefinition& task);

    // Called by IPC server for status
    int task_count() const;
    int running_task_count() const;

private:
    void worker_thread();
    void tick();
    bool is_in_window(const TimeWindow& w, const std::chrono::system_clock::time_point& now);
    DWORD run_process(const TaskDefinition& task, const std::wstring& output_path);
    void check_timeouts();
    void process_pending_queue();
    void trim_history(const std::wstring& task_id, int max_records);

    struct RunningProcess {
        HANDLE process_handle = nullptr;
        HANDLE thread_handle = nullptr;
        std::wstring history_id;
        std::wstring task_id;
        std::chrono::system_clock::time_point start_time;
    };

    Database& db_;
    std::wstring log_dir_;
    std::atomic<bool> running_{ false };
    std::thread thread_;
    HANDLE tick_timer_ = nullptr;
    HANDLE stop_event_ = nullptr;

    mutable CRITICAL_SECTION lock_;
    std::unordered_map<std::wstring, RunningProcess> running_map_; // task_id -> process
    std::unordered_map<std::wstring, std::queue<std::chrono::system_clock::time_point>> pending_queue_;
    std::unordered_map<std::wstring, int> last_run_minute_;
    std::chrono::system_clock::time_point started_at_;
};

// Forward declare for process launcher helper
bool decrypt_password(const std::wstring& encrypted_base64, std::wstring& password_out);
