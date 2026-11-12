#pragma once
#include "config.h"
#include "database.h"
#include "process_runner.h"
#include "schedule_cache.h"
#include "trigger_engine.h"
#include "trigger_state.h"
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

// Планировщик: тик по расписанию, идемпотентность слотов, запуск процессов,
// контроль таймаутов, восстановление после перезапуска службы.
class Scheduler {
public:
    Scheduler(Database& db, const std::wstring& log_dir);
    // Инъекция зависимостей для тестов: часы, раннер, хранилище состояния.
    Scheduler(Database& db, const std::wstring& log_dir,
        std::unique_ptr<IProcessRunner> runner,
        std::unique_ptr<ITriggerStateStore> state,
        std::unique_ptr<IClock> clock = nullptr);
    ~Scheduler();

    void start();
    void stop();
    ServiceStatus get_status();

    // Один тик планировщика. Используется тестами и диагностикой.
    void tick_once();

    // Ручной запуск (RunNow) через общий путь с учётом onOverlap.
    // Возвращает history_id; пустая строка — запуск отклонён (см. error).
    std::wstring run_now(const std::wstring& task_id, std::wstring& error);
    // Вариант для IPC: токен вызывающего и признак администратора.
    std::wstring run_now(const std::wstring& task_id, HANDLE client_token, bool caller_is_admin, std::wstring& error);

    // Принудительная остановка всех запущенных экземпляров задачи.
    // Возвращает false, если задача не выполняется.
    bool stop_task(const std::wstring& task_id, std::wstring& error);

    // Совместимость с service.cpp: запуск уже загруженной задачи.
    void launch_task(const TaskDefinition& task);

    // Инвалидация кэша расписания после CreateTask/UpdateTask/DeleteTask/SetTaskEnabled.
    void invalidate_schedule();
    // Изменение одной задачи: инвалидация кэша и сброс состояния её слотов.
    void on_task_changed(const std::wstring& task_id);

    int task_count() const;
    int running_task_count() const;

private:
    // Ожидающий запуск: коалесинг — не более одного на задачу.
    struct PendingRun {
        std::wstring task_id;
        std::wstring window_id;
        std::wstring slot;
        TriggeredBy triggered_by = TriggeredBy::Schedule;
        int attempt = 0;
        std::chrono::system_clock::time_point not_before;
    };

    struct RunningProcess {
        RunningProcessHandle handle;
        std::wstring history_id;
        std::wstring task_id;
        std::wstring window_id;
        std::wstring slot;
        TriggeredBy triggered_by = TriggeredBy::Schedule;
        std::optional<std::wstring> output_path;
        std::chrono::system_clock::time_point start_time;
        int timeout_minutes = 0;
        bool kill_on_timeout = false;
        bool timed_out = false; // таймаут уже зафиксирован, процесс продолжает работу
        int attempt = 0;
    };

    void worker_thread();
    void tick();
    void dispatch(const std::vector<ScheduleCache::Entry>& entries, const LocalDateTime& now_local);
    std::wstring request_start(const TaskDefinition& task, const std::wstring& window_id,
        const std::wstring& slot, TriggeredBy triggered_by, int attempt,
        HANDLE client_token, bool caller_is_admin);
    void process_pending();
    void check_running();
    void finish_run(const RunningProcess& run, RunStatus status,
        std::optional<int> exit_code, const std::wstring& message);
    void mark_timeout(const RunningProcess& run);
    void recover_orphaned_runs();
    void trim_history(const std::wstring& task_id, int max_records);
    void enqueue_retry(const RunningProcess& run);
    bool task_is_running(const std::wstring& task_id) const;
    const TaskDefinition* find_task(const std::vector<ScheduleCache::Entry>& entries, const std::wstring& task_id) const;
    // Номер суток UTC: для ежедневной чистки файлов вывода.
    int64_t current_day() const;
    // Удаляет устаревшие слоты trigger_state (retention состояния).
    void prune_trigger_state(const LocalDateTime& now_local);

    Database& db_;
    std::wstring log_dir_;
    std::unique_ptr<IClock> clock_;
    std::unique_ptr<IProcessRunner> runner_;
    std::unique_ptr<ITriggerStateStore> state_;
    std::unique_ptr<ScheduleCache> cache_;

    std::atomic<bool> running_{ false };
    std::thread thread_;
    HANDLE tick_timer_ = nullptr;
    HANDLE stop_event_ = nullptr;
    std::chrono::system_clock::time_point started_at_;
    int64_t last_purge_day_ = -1;

    mutable std::mutex mutex_;
    std::map<std::wstring, RunningProcess> running_processes_; // history_id -> процесс
    std::map<std::wstring, PendingRun> pending_;     // task_id -> ожидающий запуск
};
