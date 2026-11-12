#pragma once
#include "config.h"
#include "database.h"
#include "json.h"
#include "auth_policy.h"
#include "audit_log.h"
#include <functional>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>
#include <memory>
#include <string>

// Общая функция логирования (определена в scheduler.cpp).
void log_msg(const std::wstring& log_dir, const std::string& msg);

// Один запрос — один ответ. Внутренний результат обработчика:
// код ошибки отделён от русского текста, чтобы UI локализовал по коду.
struct IpcReply {
    bool ok = false;
    std::string code;      // AUTH_DENIED, VALIDATION, NOT_FOUND, ...
    std::wstring message;  // текст для fallback
    std::string payload;   // JSON-текст payload (для ok=true)

    static IpcReply Ok(std::string payload_json) {
        IpcReply r;
        r.ok = true;
        r.payload = std::move(payload_json);
        return r;
    }
    static IpcReply Fail(std::string code, std::wstring message) {
        IpcReply r;
        r.ok = false;
        r.code = std::move(code);
        r.message = std::move(message);
        return r;
    }
};

class IpcServer {
public:
    using RequestHandler = std::function<IpcResponse(const IpcRequest&)>;

    IpcServer(Database& db, RequestHandler scheduler_handler, const std::wstring& log_dir);
    ~IpcServer();

    void start();
    // Останавливает сервер: прекращает accept, дожидается завершения потоков
    // (без detach), закрывает канал. Идемпотентна.
    void stop();

    bool is_running() const { return running_.load(); }

    // Лимиты контракта (IPC.md раздел 1, SECURITY.md раздел 3).
    static constexpr int MAX_INSTANCES = 64;
    static constexpr DWORD READ_TIMEOUT_MS = 30000;

private:
    struct ClientWorker {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> done;
    };

    // Разобранный запрос: конверт v2 или legacy {Action, Payload}.
    struct ParsedRequest {
        std::wstring action;
        std::wstring request_id;
        json::Value payload;
        bool legacy = false;
    };

    void worker_thread();
    void reap_finished_workers();
    HANDLE create_pipe_instance();
    void handle_client(HANDLE pipe);

    bool parse_request(const std::string& body, ParsedRequest& req, IpcReply& error);

    IpcReply dispatch(const ParsedRequest& req, const auth::Identity& identity);
    IpcReply handle_ping();
    IpcReply handle_status(const auth::Identity& identity, bool legacy);
    IpcReply handle_dashboard(const auth::Identity& identity);
    IpcReply handle_list_tasks(const ParsedRequest& req, const auth::Identity& identity);
    IpcReply handle_get_task(const ParsedRequest& req, const auth::Identity& identity);
    IpcReply handle_get_time_windows(const ParsedRequest& req, const auth::Identity& identity);
    IpcReply handle_get_history(const ParsedRequest& req, const auth::Identity& identity);
    IpcReply handle_create_task(const ParsedRequest& req, const auth::Identity& identity);
    IpcReply handle_update_task(const ParsedRequest& req, const auth::Identity& identity);
    IpcReply handle_delete_task(const ParsedRequest& req, const auth::Identity& identity);
    IpcReply handle_set_enabled(const ParsedRequest& req, const auth::Identity& identity, bool enabled);
    IpcReply handle_run_now(const ParsedRequest& req, const auth::Identity& identity);
    IpcReply handle_stop_task(const ParsedRequest& req, const auth::Identity& identity);
    IpcReply handle_get_audit(const ParsedRequest& req);
    IpcReply handle_get_settings();
    IpcReply handle_set_settings(const ParsedRequest& req, const auth::Identity& identity);

    // Сообщает планировщику, что задача создана/изменена/удалена/включена.
    // Без этого кэш расписания не увидит импортированные задачи до рестарта.
    void notify_scheduler_task_changed(const std::wstring& task_id);

    // Вспомогательные: доступ к задаче с фильтрацией по владельцу.
    std::optional<TaskDefinition> load_task_checked(const std::wstring& task_id,
                                                    const auth::Identity& identity,
                                                    IpcReply& denial);

    void write_audit(const auth::Identity& identity, const wchar_t* action,
               const wchar_t* object_type, const std::wstring& object_id,
               audit::Result result, const std::wstring& details);

    Database& db_;
    RequestHandler scheduler_handler_;
    std::wstring log_dir_;

    std::thread thread_;
    std::atomic<bool> running_{ false };
    HANDLE stop_event_ = nullptr;
    std::atomic<HANDLE> listener_pipe_{ INVALID_HANDLE_VALUE };

    std::mutex workers_mutex_;
    std::vector<ClientWorker> workers_;
};

// JSON-сериализация моделей. Legacy-варианты (PascalCase) сохранены для
// переходного режима, v2-варианты — по контракту IPC.md (camelCase).
namespace json_helpers {
    std::string status_to_json(const ServiceStatus& s);

    std::string task_to_json(const TaskDefinition& t);              // legacy
    std::string task_list_to_json(const std::vector<TaskDefinition>& tasks);
    std::string time_window_to_json(const TimeWindow& w);
    std::string time_window_list_to_json(const std::vector<TimeWindow>& windows);
    std::string history_to_json(const RunHistory& h);
    std::string history_list_to_json(const std::vector<RunHistory>& history);

    std::string task_to_json_v2(const TaskDefinition& t, const std::vector<TimeWindow>& windows,
                                const auth::Identity& identity, bool with_windows);
    std::string time_window_to_json_v2(const TimeWindow& w);
    std::string history_to_json_v2(const RunHistory& h);

    std::optional<TaskDefinition> task_from_json(const std::string& json_str);
    std::vector<TimeWindow> time_windows_from_json(const std::string& json_str);

    std::optional<IpcRequest> request_from_json(const std::string& json_str);
    std::string response_to_json(const IpcResponse& resp);
}
