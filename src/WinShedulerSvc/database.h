#pragma once
#include "config.h"
#include <functional>
#include "sqlite3.h"

class Database {
public:
    Database();
    ~Database();

    bool open(const std::wstring& path);
    void close();

    // Tasks
    std::vector<TaskDefinition> list_tasks();
    std::optional<TaskDefinition> get_task(const std::wstring& id);
    void insert_task(const TaskDefinition& task);
    void update_task(const TaskDefinition& task);
    void delete_task(const std::wstring& id);
    void set_task_enabled(const std::wstring& id, bool enabled);

    // TimeWindows
    std::vector<TimeWindow> get_time_windows(const std::wstring& task_id);
    void delete_time_windows(const std::wstring& task_id);
    void insert_time_window(const TimeWindow& tw);

    // RunHistory
    void insert_history(const RunHistory& h);
    void update_history(const RunHistory& h);
    std::vector<RunHistory> get_history(const std::wstring& task_id, int limit = 100);
    void trim_history(const std::wstring& task_id, int max_records);

    sqlite3* get_db() const { return db_; }

private:
    sqlite3* db_ = nullptr;
    void exec(const std::string& sql);
    void ensure_schema();
    std::wstring generate_guid();
    std::string ws2s(const std::wstring& ws);
    std::wstring s2ws(const std::string& s);
};
