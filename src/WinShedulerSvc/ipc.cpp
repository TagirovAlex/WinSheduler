#include "ipc.h"
#include "ipc_frame.h"
#include "auth_policy.h"
#include "auth_token.h"
#include "auth_validate.h"
#include "auth_secret.h"
#include "json.h"
#include <sddl.h>
#include <rpc.h>
#include <rpcdce.h>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <cwctype>
#include <algorithm>
#include <ctime>
#include <initializer_list>
#include <type_traits>
#include <utility>

#pragma comment(lib, "rpcrt4.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "crypt32.lib")

// ---------------------------------------------------------------------------
// Общие помощники
// ---------------------------------------------------------------------------

namespace {

constexpr const wchar_t* SERVICE_VERSION = L"1.2.0";
constexpr int MAX_HISTORY_LIMIT = 1000;
constexpr int MAX_AUDIT_FETCH = 1000;
// Legacy-клиент не сообщает границу сообщения: считаем его законченным,
// если новых байт нет дольше этого интервала.
constexpr ULONGLONG LEGACY_IDLE_MS = 300;

// --- Совместимость с API трека C (database.h) ---
// До слияния I-2 используются старые методы Database; как только появляются
// list_tasks_by_owner/replace_time_windows, автоматически выбирается
// реализация трека C (фильтрация в SQL и замена триггеров в транзакции).

template <typename Db, typename = void>
struct has_list_by_owner : std::false_type {};
template <typename Db>
struct has_list_by_owner<Db, std::void_t<decltype(std::declval<Db&>().list_tasks_by_owner(
    std::declval<const std::wstring&>()))>> : std::true_type {};

template <typename Db, typename = void>
struct has_replace_windows : std::false_type {};
template <typename Db>
struct has_replace_windows<Db, std::void_t<decltype(std::declval<Db&>().replace_time_windows(
    std::declval<const std::wstring&>(),
    std::declval<const std::vector<TimeWindow>&>()))>> : std::true_type {};

template <typename Db, typename = void>
struct has_task_count : std::false_type {};
template <typename Db>
struct has_task_count<Db, std::void_t<decltype(std::declval<Db&>().task_count())>> : std::true_type {};

template <typename Db>
std::vector<TaskDefinition> list_tasks_for(Db& db, const auth::Identity& identity) {
    if constexpr (has_list_by_owner<Db>::value) {
        // Пустой SID у трека C означает «все задачи» — режим администратора.
        return db.list_tasks_by_owner(identity.is_admin ? std::wstring() : identity.sid);
    } else {
        auto tasks = db.list_tasks();
        std::vector<TaskDefinition> visible;
        visible.reserve(tasks.size());
        for (auto& task : tasks) {
            if (auth::Policy::can_access_task(identity, task.owner_sid)) visible.push_back(task);
        }
        return visible;
    }
}

template <typename Db>
void replace_windows(Db& db, const std::wstring& task_id, const std::vector<TimeWindow>& windows) {
    if constexpr (has_replace_windows<Db>::value) {
        db.replace_time_windows(task_id, windows);
    } else {
        // Транзакция появится вместе с треком C; из IPC BEGIN не выполняется.
        db.delete_time_windows(task_id);
        for (const auto& window : windows) db.insert_time_window(window);
    }
}

template <typename Db>
int total_task_count(Db& db) {
    if constexpr (has_task_count<Db>::value) return db.task_count();
    else return static_cast<int>(db.list_tasks().size());
}

const json::Value* field(const json::Value& obj, std::initializer_list<const char*> names) {
    for (const char* name : names) {
        const json::Value* value = obj.get(name);
        if (value != nullptr) return value;
    }
    return nullptr;
}

std::wstring str_field(const json::Value& obj, std::initializer_list<const char*> names) {
    const json::Value* value = field(obj, names);
    if (value != nullptr && value->is_string()) return u2ws(value->as_string());
    return {};
}

bool bool_field(const json::Value& obj, std::initializer_list<const char*> names, bool fallback) {
    const json::Value* value = field(obj, names);
    if (value != nullptr && value->is_bool()) return value->as_bool();
    return fallback;
}

int int_field(const json::Value& obj, std::initializer_list<const char*> names, int fallback) {
    const json::Value* value = field(obj, names);
    if (value != nullptr && value->is_number()) return value->as_int();
    return fallback;
}

std::optional<int> opt_int_field(const json::Value& obj, std::initializer_list<const char*> names) {
    const json::Value* value = field(obj, names);
    if (value != nullptr && value->is_number()) return value->as_int();
    return std::nullopt;
}

std::string utc_now_iso() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_s(&tm, &t);
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

std::wstring new_guid() {
    GUID guid;
    if (UuidCreate(&guid) != RPC_S_OK) return {};
    wchar_t buf[64] = {};
    if (StringFromGUID2(guid, buf, 64) == 0) return {};
    std::wstring s(buf + 1);
    if (!s.empty()) s.pop_back();
    for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c));
    return s;
}

bool is_empty_or_zero_guid(const std::wstring& id) {
    return id.empty() || id == L"00000000-0000-0000-0000-000000000000";
}

// Идентификатор задачи: объект {taskId|TaskId|id} или legacy-строка.
std::wstring task_id_from(const json::Value& payload) {
    if (payload.is_object()) {
        return str_field(payload, { "taskId", "TaskId", "id", "Id" });
    }
    if (payload.is_string()) return u2ws(payload.as_string());
    return {};
}

std::vector<TimeWindow> windows_from_object(const json::Value& obj) {
    const json::Value* tw = field(obj, { "timeWindows", "TimeWindows" });
    if (tw == nullptr || !tw->is_array()) return {};
    return json_helpers::time_windows_from_json(json::serialize(*tw));
}

std::string windows_to_json_v2(const std::vector<TimeWindow>& windows) {
    json::Array arr;
    for (const auto& w : windows) {
        arr.push_back(json::parse(json_helpers::time_window_to_json_v2(w)));
    }
    return json::serialize(arr);
}

const wchar_t* window_style_name(WindowStyle style) {
    switch (style) {
    case WindowStyle::Normal: return L"Normal";
    case WindowStyle::Hidden: return L"Hidden";
    case WindowStyle::Minimized: return L"Minimized";
    case WindowStyle::Maximized: return L"Maximized";
    }
    return L"Normal";
}

const wchar_t* on_error_name(OnErrorAction action) {
    switch (action) {
    case OnErrorAction::SkipNext: return L"SkipNext";
    case OnErrorAction::Retry: return L"Retry";
    case OnErrorAction::Fail: return L"Fail";
    }
    return L"SkipNext";
}

const wchar_t* on_overlap_name(OnOverlapAction action) {
    switch (action) {
    case OnOverlapAction::WaitComplete: return L"WaitComplete";
    case OnOverlapAction::WaitNext: return L"WaitNext";
    case OnOverlapAction::StartAnother: return L"StartAnother";
    }
    return L"WaitComplete";
}

const wchar_t* trigger_kind_name(TriggerKind kind) {
    switch (kind) {
    case TriggerKind::Once: return L"Once";
    case TriggerKind::Daily: return L"Daily";
    case TriggerKind::Weekly: return L"Weekly";
    case TriggerKind::Monthly: return L"Monthly";
    case TriggerKind::Interval: return L"Interval";
    }
    return L"Daily";
}

const wchar_t* day_of_week_name(int day) {
    static const wchar_t* names[] = { L"Sunday", L"Monday", L"Tuesday", L"Wednesday",
                                      L"Thursday", L"Friday", L"Saturday" };
    if (day < 0 || day > 6) return L"Sunday";
    return names[day];
}

// Краткое описание расписания для GetDashboard.
std::wstring schedule_summary(const std::vector<TimeWindow>& windows) {
    if (windows.empty()) return L"Нет расписания";
    const TimeWindow& w = windows.front();
    std::wstring time = (!w.exact_times.empty()) ? w.exact_times.front() : L"";
    switch (w.kind) {
    case TriggerKind::Once:
        return L"Однократно " + (!w.specific_dates.empty() ? w.specific_dates.front() + L" " : L"") + time;
    case TriggerKind::Daily:
        return L"Ежедневно " + time;
    case TriggerKind::Weekly:
        return L"Еженедельно " + std::wstring(day_of_week_name(w.days_of_week.empty() ? 0 : w.days_of_week.front())) + L" " + time;
    case TriggerKind::Monthly:
        return L"Ежемесячно " + (w.days_of_month.empty() ? L"" : std::to_wstring(w.days_of_month.front())) + L" " + time;
    case TriggerKind::Interval:
        return L"Каждые " + std::to_wstring(w.repeat_interval_minutes.value_or(0)) + L" мин";
    }
    return L"Нет расписания";
}

std::string status_to_json_v2(const ServiceStatus& s) {
    json::Object obj;
    obj["running"] = json::Value(s.running);
    obj["isAdmin"] = json::Value(s.is_admin);
    obj["isOperator"] = json::Value(s.is_operator);
    obj["canManageSystemTasks"] = json::Value(s.can_manage_system_tasks);
    obj["serviceVersion"] = json::Value(ws2u(s.service_version));
    obj["startedAt"] = json::Value(ws2u(s.started_at));
    obj["taskCount"] = json::Value(s.task_count);
    obj["runningTaskCount"] = json::Value(s.running_task_count);
    obj["dbReady"] = json::Value(s.db_ready);
    return json::serialize(obj);
}

std::string build_v2_response(const IpcReply& reply, const std::wstring& request_id) {
    json::Object obj;
    obj["v"] = json::Value(1);
    obj["id"] = json::Value(ws2u(request_id));
    obj["ok"] = json::Value(reply.ok);
    if (reply.ok) {
        obj["error"] = json::Value(nullptr);
        obj["payload"] = json::parse(reply.payload);
    } else {
        json::Object err;
        err["code"] = json::Value(reply.code);
        err["message"] = json::Value(ws2u(reply.message));
        obj["error"] = err;
        obj["payload"] = json::Value(nullptr);
    }
    return json::serialize(obj);
}

std::string build_legacy_response(const IpcReply& reply) {
    json::Object obj;
    obj["Success"] = json::Value(reply.ok);
    obj["Error"] = json::Value(reply.ok ? std::string() : ws2u(reply.message));
    obj["Payload"] = json::Value(reply.ok ? reply.payload : std::string());
    return json::serialize(obj);
}

enum class ReadResult {
    Ok,
    Closed,
    Timeout,
    TooLarge,
    Error
};

struct Frame {
    std::string body;
    bool legacy = false;
};

// Читает ровно want байт, опрашивая канал. PeekNamedPipe позволяет
// не блокироваться навсегда и уложиться в таймаут контракта.
ReadResult read_exact(HANDLE pipe, char* dst, std::size_t want, ULONGLONG deadline,
                      const std::atomic<bool>& running) {
    std::size_t got = 0;
    while (got < want) {
        DWORD available = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) return ReadResult::Closed;
        if (available == 0) {
            if (!running.load()) return ReadResult::Closed;
            if (GetTickCount64() >= deadline) return ReadResult::Timeout;
            Sleep(5);
            continue;
        }
        DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(want - got, available));
        DWORD read = 0;
        if (!ReadFile(pipe, dst + got, chunk, &read, nullptr)) return ReadResult::Closed;
        if (read == 0) return ReadResult::Closed;
        got += read;
    }
    return ReadResult::Ok;
}

// Читает одно сообщение: кадр v2 (4 байта длины + тело) или legacy-сообщение
// без префикса. Граница legacy-сообщения определяется по паузе в потоке.
ReadResult read_frame(HANDLE pipe, const std::atomic<bool>& running, Frame& frame) {
    frame = Frame{};
    unsigned char header[ipc_frame::HEADER_BYTES] = {};
    const ULONGLONG deadline = GetTickCount64() + IpcServer::READ_TIMEOUT_MS;
    ReadResult rc = read_exact(pipe, reinterpret_cast<char*>(header), sizeof(header), deadline, running);
    if (rc != ReadResult::Ok) return rc;

    std::uint32_t frame_len = 0;
    const ipc_frame::Prefix prefix = ipc_frame::classify_prefix(header, sizeof(header), frame_len);
    if (prefix == ipc_frame::Prefix::Frame) {
        frame.legacy = false;
        frame.body.resize(frame_len);
        if (frame_len == 0) return ReadResult::Ok;
        return read_exact(pipe, frame.body.data(), frame_len, deadline, running);
    }

    // Legacy: префикса нет, дочитываем по паузе.
    frame.legacy = true;
    frame.body.assign(reinterpret_cast<const char*>(header), sizeof(header));
    ULONGLONG last_data = GetTickCount64();
    while (true) {
        DWORD available = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) break;
        if (available == 0) {
            if (!running.load()) break;
            const ULONGLONG now = GetTickCount64();
            if (now >= deadline) return ReadResult::Timeout;
            if (now - last_data >= LEGACY_IDLE_MS) break;
            Sleep(5);
            continue;
        }
        if (frame.body.size() + available > ipc_frame::MAX_FRAME_BYTES) return ReadResult::TooLarge;
        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!ReadFile(pipe, chunk.data(), available, &read, nullptr) || read == 0) break;
        frame.body.append(chunk.data(), read);
        last_data = GetTickCount64();
    }
    return ReadResult::Ok;
}

bool write_all(HANDLE pipe, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(data.size() - sent, 64 * 1024));
        DWORD written = 0;
        if (!WriteFile(pipe, data.data() + sent, chunk, &written, nullptr)) return false;
        if (written == 0) return false;
        sent += written;
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// JSON-сериализация моделей
// ---------------------------------------------------------------------------

namespace json_helpers {

static json::Value opt_str(const std::optional<std::wstring>& v) {
    if (v.has_value()) return json::Value(ws2u(v.value()));
    return json::Value(nullptr);
}

static json::Value opt_int(const std::optional<int>& v) {
    if (v.has_value()) return json::Value(v.value());
    return json::Value(nullptr);
}

// ---- Legacy-формат (PascalCase), переходный режим IPC.md раздел 7 ----

std::string task_to_json(const TaskDefinition& t) {
    json::Object obj;
    obj["Id"] = json::Value(ws2u(t.id));
    obj["Name"] = json::Value(ws2u(t.name));
    obj["Description"] = json::Value(ws2u(t.description));
    obj["Enabled"] = json::Value(t.enabled);
    obj["ProgramPath"] = json::Value(ws2u(t.program_path));
    obj["Arguments"] = json::Value(ws2u(t.arguments));
    obj["WorkingDirectory"] = json::Value(ws2u(t.working_directory));
    obj["WindowStyle"] = json::Value(ws2u(window_style_name(t.window_style)));
    obj["OnError"] = json::Value(ws2u(on_error_name(t.on_error)));
    obj["RetryCount"] = json::Value(t.retry_count);
    obj["OnOverlap"] = json::Value(ws2u(on_overlap_name(t.on_overlap)));
    obj["TimeoutMinutes"] = opt_int(t.timeout_minutes);
    obj["KillOnTimeout"] = json::Value(t.kill_on_timeout);
    obj["LogOutput"] = json::Value(t.log_output);
    obj["MaxHistoryRecords"] = json::Value(t.max_history_records);
    obj["CreatedAt"] = json::Value(ws2u(t.created_at));
    obj["ModifiedAt"] = json::Value(ws2u(t.modified_at));
    obj["SessionMode"] = json::Value(t.session_mode == SessionMode::ActiveUser ? "ActiveUser" : "Headless");

    if (t.run_as_user.has_value()) {
        obj["RunAsUser"] = json::Value(ws2u(t.run_as_user->user_name));
        obj["RunAsDomain"] = json::Value(ws2u(t.run_as_user->domain));
    }
    // Секрет никогда не отдаётся наружу — только признак наличия.
    obj["HasPassword"] = json::Value(t.run_as_user.has_value() &&
                                     !t.run_as_user->encrypted_password.empty());
    return json::serialize(obj);
}

std::string task_list_to_json(const std::vector<TaskDefinition>& tasks) {
    json::Array arr;
    for (auto& t : tasks) arr.push_back(json::parse(task_to_json(t)));
    return json::serialize(arr);
}

std::string time_window_to_json(const TimeWindow& w) {
    json::Object obj;
    obj["Id"] = json::Value(ws2u(w.id));
    obj["TaskId"] = json::Value(ws2u(w.task_id));
    obj["Type"] = json::Value(ws2u(w.type == TimeWindowType::ExactTimes ? L"ExactTimes" : L"Interval"));
    obj["Kind"] = json::Value(ws2u(trigger_kind_name(w.kind)));
    obj["StartDate"] = opt_str(w.start_date);
    obj["EndDate"] = opt_str(w.end_date);
    obj["StartTime"] = opt_str(w.start_time);
    obj["EndTime"] = opt_str(w.end_time);
    obj["RepeatIntervalMinutes"] = opt_int(w.repeat_interval_minutes);
    obj["RepeatUntil"] = opt_str(w.repeat_until);
    obj["DayInterval"] = json::Value(w.day_interval);
    obj["WeekInterval"] = json::Value(w.week_interval);
    obj["MonthInterval"] = json::Value(w.month_interval);

    json::Array dow;
    for (int d : w.days_of_week) dow.push_back(json::Value(ws2u(day_of_week_name(d))));
    obj["DaysOfWeek"] = dow;

    json::Array dom;
    for (int d : w.days_of_month) dom.push_back(json::Value(d));
    obj["DaysOfMonth"] = dom;

    json::Array sd;
    for (auto& d : w.specific_dates) sd.push_back(json::Value(ws2u(d)));
    obj["SpecificDates"] = sd;

    json::Array et;
    for (auto& e : w.exact_times) et.push_back(json::Value(ws2u(e)));
    obj["ExactTimes"] = et;

    return json::serialize(obj);
}

std::string time_window_list_to_json(const std::vector<TimeWindow>& windows) {
    json::Array arr;
    for (auto& w : windows) arr.push_back(json::parse(time_window_to_json(w)));
    return json::serialize(arr);
}

std::string history_to_json(const RunHistory& h) {
    json::Object obj;
    obj["Id"] = json::Value(ws2u(h.id));
    obj["TaskId"] = json::Value(ws2u(h.task_id));
    obj["StartTime"] = json::Value(ws2u(h.start_time));
    obj["EndTime"] = opt_str(h.end_time);
    obj["ExitCode"] = opt_int(h.exit_code);
    obj["Pid"] = json::Value(h.pid);
    obj["Status"] = json::Value(ws2u(ToWide(h.status)));
    obj["ErrorMessage"] = json::Value(ws2u(h.error_message));
    obj["OutputPath"] = opt_str(h.output_path);
    obj["TriggeredBy"] = json::Value(h.triggered_by == TriggeredBy::Manual ? "Manual" : "Schedule");
    return json::serialize(obj);
}

std::string history_list_to_json(const std::vector<RunHistory>& history) {
    json::Array arr;
    for (auto& h : history) arr.push_back(json::parse(history_to_json(h)));
    return json::serialize(arr);
}

std::string status_to_json(const ServiceStatus& s) {
    json::Object obj;
    obj["Running"] = json::Value(s.running);
    obj["IsAdmin"] = json::Value(s.is_admin);
    obj["IsOperator"] = json::Value(s.is_operator);
    obj["CanManageSystemTasks"] = json::Value(s.can_manage_system_tasks);
    obj["ServiceVersion"] = json::Value(ws2u(s.service_version));
    obj["TaskCount"] = json::Value(s.task_count);
    obj["RunningTaskCount"] = json::Value(s.running_task_count);
    obj["DbReady"] = json::Value(s.db_ready);
    obj["StartedAt"] = json::Value(ws2u(s.started_at));
    return json::serialize(obj);
}

// ---- Формат v2 (camelCase), контракт IPC.md раздел 5 ----

std::string time_window_to_json_v2(const TimeWindow& w) {
    json::Object obj;
    obj["id"] = json::Value(ws2u(w.id));
    obj["kind"] = json::Value(ws2u(trigger_kind_name(w.kind)));
    obj["startDate"] = opt_str(w.start_date);
    obj["endDate"] = opt_str(w.end_date);
    obj["startTime"] = opt_str(w.start_time);
    obj["endTime"] = opt_str(w.end_time);
    obj["repeatIntervalMinutes"] = opt_int(w.repeat_interval_minutes);
    obj["repeatUntil"] = opt_str(w.repeat_until);
    obj["dayInterval"] = json::Value(w.day_interval);
    obj["weekInterval"] = json::Value(w.week_interval);
    obj["monthInterval"] = json::Value(w.month_interval);

    json::Array dow;
    for (int d : w.days_of_week) dow.push_back(json::Value(ws2u(day_of_week_name(d))));
    obj["daysOfWeek"] = dow;

    json::Array dom;
    for (int d : w.days_of_month) dom.push_back(json::Value(d));
    obj["daysOfMonth"] = dom;

    json::Array sd;
    for (auto& d : w.specific_dates) sd.push_back(json::Value(ws2u(d)));
    obj["specificDates"] = sd;

    json::Array et;
    for (auto& e : w.exact_times) et.push_back(json::Value(ws2u(e)));
    obj["exactTimes"] = et;

    return json::serialize(obj);
}

std::string history_to_json_v2(const RunHistory& h) {
    json::Object obj;
    obj["id"] = json::Value(ws2u(h.id));
    obj["taskId"] = json::Value(ws2u(h.task_id));
    obj["startTime"] = json::Value(ws2u(h.start_time));
    obj["endTime"] = opt_str(h.end_time);
    obj["exitCode"] = opt_int(h.exit_code);
    obj["pid"] = json::Value(h.pid);
    obj["status"] = json::Value(ws2u(ToWide(h.status)));
    obj["errorMessage"] = json::Value(ws2u(h.error_message));
    obj["outputPath"] = opt_str(h.output_path);
    obj["triggeredBy"] = json::Value(h.triggered_by == TriggeredBy::Manual ? "Manual" : "Schedule");
    return json::serialize(obj);
}

std::string task_to_json_v2(const TaskDefinition& t, const std::vector<TimeWindow>& windows,
                            const auth::Identity& identity, bool with_windows) {
    const bool is_own = auth::Policy::can_access_task(identity, t.owner_sid) &&
                        auth::equal_sid(t.owner_sid, identity.sid);
    json::Object obj;
    obj["id"] = json::Value(ws2u(t.id));
    obj["name"] = json::Value(ws2u(t.name));
    obj["description"] = json::Value(ws2u(t.description));
    obj["enabled"] = json::Value(t.enabled);
    obj["programPath"] = json::Value(ws2u(t.program_path));
    obj["arguments"] = json::Value(ws2u(t.arguments));
    obj["workingDirectory"] = json::Value(ws2u(t.working_directory));
    obj["windowStyle"] = json::Value(ws2u(window_style_name(t.window_style)));
    obj["sessionMode"] = json::Value(t.session_mode == SessionMode::ActiveUser ? "ActiveUser" : "Headless");
    obj["onError"] = json::Value(ws2u(on_error_name(t.on_error)));
    obj["retryCount"] = json::Value(t.retry_count);
    obj["onOverlap"] = json::Value(ws2u(on_overlap_name(t.on_overlap)));
    obj["timeoutMinutes"] = opt_int(t.timeout_minutes);
    obj["killOnTimeout"] = json::Value(t.kill_on_timeout);
    obj["logOutput"] = json::Value(t.log_output);
    obj["maxHistoryRecords"] = json::Value(t.max_history_records);
    obj["createdAt"] = json::Value(ws2u(t.created_at));
    obj["modifiedAt"] = json::Value(ws2u(t.modified_at));

    // RunAs-поля и SID владельца видны только администратору.
    if (identity.is_admin && t.run_as_user.has_value()) {
        obj["runAsUser"] = json::Value(ws2u(t.run_as_user->user_name));
        obj["runAsDomain"] = json::Value(ws2u(t.run_as_user->domain));
    } else {
        obj["runAsUser"] = json::Value(nullptr);
        obj["runAsDomain"] = json::Value(nullptr);
    }
    obj["hasPassword"] = json::Value(t.run_as_user.has_value() &&
                                     !t.run_as_user->encrypted_password.empty());

    json::Object owner;
    owner["isOwn"] = json::Value(is_own);
    owner["label"] = json::Value(is_own ? "Вы" : "Другой пользователь");
    if (identity.is_admin) owner["sid"] = json::Value(ws2u(t.owner_sid));
    obj["owner"] = owner;
    obj["isOwn"] = json::Value(is_own);

    if (with_windows) obj["timeWindows"] = json::parse(windows_to_json_v2(windows));

    return json::serialize(obj);
}

// ---- Разбор DTO (принимает camelCase и PascalCase) ----

std::optional<TaskDefinition> task_from_json(const std::string& json_str) {
    auto parsed = json::parse_ex(json_str);
    if (!parsed.ok() || !parsed.value.is_object()) return std::nullopt;
    const json::Value& v = parsed.value;

    TaskDefinition t;
    t.id = str_field(v, { "id", "Id" });
    t.name = str_field(v, { "name", "Name" });
    t.description = str_field(v, { "description", "Description" });
    t.enabled = bool_field(v, { "enabled", "Enabled" }, true);
    t.program_path = str_field(v, { "programPath", "ProgramPath" });
    t.arguments = str_field(v, { "arguments", "Arguments" });
    t.working_directory = str_field(v, { "workingDirectory", "WorkingDirectory" });

    auto parse_enum = [&](std::initializer_list<const char*> names,
                          const std::vector<std::pair<std::string, int>>& map, int& out) {
        const json::Value* value = field(v, names);
        if (value == nullptr) return;
        if (value->is_number()) { out = value->as_int(); return; }
        if (value->is_string()) {
            for (const auto& [name, id] : map) {
                if (value->as_string() == name) { out = id; return; }
            }
        }
    };

    int style = static_cast<int>(WindowStyle::Normal);
    parse_enum({ "windowStyle", "WindowStyle" },
               { { "Normal", 0 }, { "Hidden", 1 }, { "Minimized", 2 }, { "Maximized", 3 } }, style);
    t.window_style = static_cast<WindowStyle>(style);

    int on_error = 0;
    parse_enum({ "onError", "OnError" },
               { { "SkipNext", 0 }, { "Retry", 1 }, { "Fail", 2 } }, on_error);
    t.on_error = static_cast<OnErrorAction>(on_error);

    int on_overlap = 0;
    parse_enum({ "onOverlap", "OnOverlap" },
               { { "WaitComplete", 0 }, { "WaitNext", 1 }, { "StartAnother", 2 } }, on_overlap);
    t.on_overlap = static_cast<OnOverlapAction>(on_overlap);

    std::wstring session = str_field(v, { "sessionMode", "SessionMode" });
    if (session == L"ActiveUser") t.session_mode = SessionMode::ActiveUser;
    else if (session == L"Headless") t.session_mode = SessionMode::Headless;

    t.retry_count = int_field(v, { "retryCount", "RetryCount" }, 0);
    t.timeout_minutes = opt_int_field(v, { "timeoutMinutes", "TimeoutMinutes" });
    t.kill_on_timeout = bool_field(v, { "killOnTimeout", "KillOnTimeout" }, false);
    t.log_output = bool_field(v, { "logOutput", "LogOutput" }, false);
    t.max_history_records = int_field(v, { "maxHistoryRecords", "MaxHistoryRecords" }, 1000);

    std::wstring run_as_user = str_field(v, { "runAsUser", "RunAsUser" });
    if (!run_as_user.empty()) {
        RunAsCredentials cred;
        cred.user_name = run_as_user;
        cred.domain = str_field(v, { "runAsDomain", "RunAsDomain" });
        // Устаревшее поле: старый UI присылал уже зашифрованный blob.
        cred.encrypted_password = str_field(v, { "RunAsPasswordEncrypted", "encryptedPassword" });
        t.run_as_user = cred;
    }

    return t;
}

std::vector<TimeWindow> time_windows_from_json(const std::string& json_str) {
    std::vector<TimeWindow> result;
    auto parsed = json::parse_ex(json_str);
    if (!parsed.ok() || !parsed.value.is_array()) return result;

    for (const auto& elem : parsed.value.as_array()) {
        if (!elem.is_object()) continue;
        TimeWindow tw;

        tw.id = str_field(elem, { "id", "Id" });

        std::wstring kind = str_field(elem, { "kind", "Kind" });
        if (kind == L"Once") tw.kind = TriggerKind::Once;
        else if (kind == L"Daily") tw.kind = TriggerKind::Daily;
        else if (kind == L"Weekly") tw.kind = TriggerKind::Weekly;
        else if (kind == L"Monthly") tw.kind = TriggerKind::Monthly;
        else if (kind == L"Interval") tw.kind = TriggerKind::Interval;
        else {
            // Совместимость: старый клиент присылал Type вместо Kind.
            const json::Value* type_val = field(elem, { "type", "Type" });
            if (type_val != nullptr) {
                if (type_val->is_string()) {
                    tw.type = type_val->as_string() == "Interval" ? TimeWindowType::Interval
                                                                  : TimeWindowType::ExactTimes;
                } else if (type_val->is_number()) {
                    tw.type = static_cast<TimeWindowType>(type_val->as_int());
                }
                tw.kind = (tw.type == TimeWindowType::Interval) ? TriggerKind::Interval : TriggerKind::Daily;
            }
        }
        tw.type = (tw.kind == TriggerKind::Interval) ? TimeWindowType::Interval : TimeWindowType::ExactTimes;

        std::wstring start_date = str_field(elem, { "startDate", "StartDate" });
        if (!start_date.empty()) tw.start_date = start_date;
        std::wstring end_date = str_field(elem, { "endDate", "EndDate" });
        if (!end_date.empty()) tw.end_date = end_date;
        std::wstring start_time = str_field(elem, { "startTime", "StartTime" });
        if (!start_time.empty()) tw.start_time = start_time;
        std::wstring end_time = str_field(elem, { "endTime", "EndTime" });
        if (!end_time.empty()) tw.end_time = end_time;
        tw.repeat_interval_minutes = opt_int_field(elem, { "repeatIntervalMinutes", "RepeatIntervalMinutes" });
        std::wstring repeat_until = str_field(elem, { "repeatUntil", "RepeatUntil" });
        if (!repeat_until.empty()) tw.repeat_until = repeat_until;

        tw.day_interval = int_field(elem, { "dayInterval", "DayInterval" }, 1);
        tw.week_interval = int_field(elem, { "weekInterval", "WeekInterval" }, 1);
        tw.month_interval = int_field(elem, { "monthInterval", "MonthInterval" }, 1);

        {
            const json::Value* arr = field(elem, { "daysOfWeek", "DaysOfWeek" });
            if (arr != nullptr && arr->is_array()) {
                for (const auto& item : arr->as_array()) {
                    if (item.is_number()) tw.days_of_week.push_back(item.as_int());
                    else if (item.is_string()) {
                        static const std::pair<const char*, int> map[] = {
                            { "Sunday", 0 }, { "Monday", 1 }, { "Tuesday", 2 }, { "Wednesday", 3 },
                            { "Thursday", 4 }, { "Friday", 5 }, { "Saturday", 6 }
                        };
                        for (const auto& m : map) {
                            if (item.as_string() == m.first) { tw.days_of_week.push_back(m.second); break; }
                        }
                    }
                }
            }
        }
        {
            const json::Value* arr = field(elem, { "daysOfMonth", "DaysOfMonth" });
            if (arr != nullptr && arr->is_array()) {
                for (const auto& item : arr->as_array()) {
                    if (item.is_number()) tw.days_of_month.push_back(item.as_int());
                }
            }
        }
        {
            const json::Value* arr = field(elem, { "specificDates", "SpecificDates" });
            if (arr != nullptr && arr->is_array()) {
                for (const auto& item : arr->as_array()) {
                    if (item.is_string()) tw.specific_dates.push_back(u2ws(item.as_string()));
                }
            }
        }
        {
            const json::Value* arr = field(elem, { "exactTimes", "ExactTimes" });
            if (arr != nullptr && arr->is_array()) {
                for (const auto& item : arr->as_array()) {
                    if (item.is_string()) tw.exact_times.push_back(u2ws(item.as_string()));
                }
            }
        }

        result.push_back(std::move(tw));
    }
    return result;
}

std::optional<IpcRequest> request_from_json(const std::string& json_str) {
    auto parsed = json::parse_ex(json_str);
    if (!parsed.ok() || !parsed.value.is_object()) return std::nullopt;
    IpcRequest req;
    req.action = str_field(parsed.value, { "action", "Action" });
    req.payload = str_field(parsed.value, { "payload", "Payload" });
    return req;
}

std::string response_to_json(const IpcResponse& resp) {
    IpcReply reply;
    reply.ok = resp.success;
    reply.message = resp.error;
    reply.payload = ws2u(resp.payload);
    if (!reply.ok) reply.code = "INTERNAL";
    return build_legacy_response(reply);
}

} // namespace json_helpers

// ---------------------------------------------------------------------------
// IpcServer
// ---------------------------------------------------------------------------

IpcServer::IpcServer(Database& db, RequestHandler handler, const std::wstring& log_dir)
    : db_(db), scheduler_handler_(std::move(handler)), log_dir_(log_dir) {}

IpcServer::~IpcServer() { stop(); }

void IpcServer::start() {
    if (running_.exchange(true)) return;
    // SID группы операторов читается один раз при старте (реестр GroupSid
    // или LookupAccountName), а не на каждое соединение.
    auth::warm_up_group_cache();
    if (!auth::operator_group_available()) {
        log_msg(log_dir_, "ipc: group '" + ws2u(auth::OPERATOR_GROUP_NAME) +
                          "' not found; pipe restricted to SYSTEM/Administrators");
    }
    stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    thread_ = std::thread(&IpcServer::worker_thread, this);
}

void IpcServer::stop() {
    // Прекращаем приём новых соединений.
    running_ = false;
    if (stop_event_ != nullptr) SetEvent(stop_event_);

    HANDLE listener = listener_pipe_.load();
    if (listener != INVALID_HANDLE_VALUE && listener != nullptr) {
        CancelIoEx(listener, nullptr);
    }

    if (thread_.joinable()) thread_.join();

    // Будим клиентские потоки, заблокированные в ReadFile/WriteFile,
    // и дожидаемся их завершения. Detach запрещён (B-9).
    {
        std::lock_guard<std::mutex> lock(workers_mutex_);
        for (auto& worker : workers_) {
            if (worker.thread.joinable()) CancelSynchronousIo(worker.thread.native_handle());
        }
    }

    std::vector<ClientWorker> pending;
    {
        std::lock_guard<std::mutex> lock(workers_mutex_);
        pending.swap(workers_);
    }
    for (auto& worker : pending) {
        if (worker.thread.joinable()) worker.thread.join();
    }

    if (stop_event_ != nullptr) {
        CloseHandle(stop_event_);
        stop_event_ = nullptr;
    }
    listener_pipe_ = INVALID_HANDLE_VALUE;
}

HANDLE IpcServer::create_pipe_instance() {
    // SDDL строится из кэшированного SID группы; без группы — только
    // SYSTEM/Administrators (fallback без падения службы).
    std::wstring sddl;
    auth::build_pipe_sddl(sddl);

    SECURITY_ATTRIBUTES sa = {};
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) {
        sa.nLength = sizeof(sa);
        sa.lpSecurityDescriptor = descriptor;
        sa.bInheritHandle = FALSE;
    } else {
        log_msg(log_dir_, "ipc: ConvertStringSecurityDescriptor failed: " +
                          std::to_string(GetLastError()));
    }

    HANDLE pipe = CreateNamedPipeW(
        PIPE_NAME,
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        MAX_INSTANCES,
        PIPE_BUFFER_SIZE, PIPE_BUFFER_SIZE,
        0,
        descriptor != nullptr ? &sa : nullptr);

    if (descriptor != nullptr) LocalFree(descriptor);
    return pipe;
}

void IpcServer::reap_finished_workers() {
    std::vector<ClientWorker> finished;
    {
        std::lock_guard<std::mutex> lock(workers_mutex_);
        auto it = workers_.begin();
        while (it != workers_.end()) {
            if (it->done->load()) {
                finished.push_back(std::move(*it));
                it = workers_.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (auto& worker : finished) {
        if (worker.thread.joinable()) worker.thread.join();
    }
}

void IpcServer::worker_thread() {
    while (running_) {
        reap_finished_workers();

        HANDLE pipe = create_pipe_instance();
        if (pipe == INVALID_HANDLE_VALUE) {
            // Лимит инстансов исчерпан или ресурсы кончились — ждём и повторяем.
            Sleep(50);
            continue;
        }

        listener_pipe_ = pipe;

        OVERLAPPED ov = {};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (ov.hEvent == nullptr) {
            CloseHandle(pipe);
            listener_pipe_ = INVALID_HANDLE_VALUE;
            continue;
        }

        BOOL connected = ConnectNamedPipe(pipe, &ov);
        DWORD connect_error = connected ? ERROR_SUCCESS : GetLastError();
        if (!connected && connect_error == ERROR_PIPE_CONNECTED) connected = TRUE;

        bool stop_requested = false;
        if (!connected && connect_error == ERROR_IO_PENDING) {
            HANDLE events[2] = { ov.hEvent, stop_event_ };
            while (!connected) {
                const DWORD wait = WaitForMultipleObjects(2, events, FALSE, 500);
                if (wait == WAIT_OBJECT_0) {
                    DWORD transferred = 0;
                    connected = GetOverlappedResult(pipe, &ov, &transferred, FALSE) != FALSE;
                    break;
                }
                if (wait == WAIT_OBJECT_0 + 1 || !running_) {
                    CancelIoEx(pipe, &ov);
                    stop_requested = true;
                    break;
                }
                // WAIT_TIMEOUT: клиент ещё не подключился, проверяем running_ и ждём.
            }
        }

        CloseHandle(ov.hEvent);
        listener_pipe_ = INVALID_HANDLE_VALUE;

        if (!connected) {
            CloseHandle(pipe);
            if (stop_requested) break;
            continue;
        }

        auto done = std::make_shared<std::atomic<bool>>(false);
        {
            std::lock_guard<std::mutex> lock(workers_mutex_);
            workers_.push_back(ClientWorker{
                std::thread([this, pipe, done]() {
                    // Верхний catch(...): исключения не пересекают границу потока (B-4).
                    try {
                        handle_client(pipe);
                    } catch (...) {
                        log_msg(log_dir_, "ipc: unhandled exception in client thread");
                    }
                    done->store(true);
                }),
                done
            });
        }
    }
}

bool IpcServer::parse_request(const std::string& body, ParsedRequest& req, IpcReply& error) {
    ipc_frame::Envelope envelope;
    switch (ipc_frame::parse_envelope(body, envelope)) {
    case ipc_frame::EnvelopeStatus::Ok:
        break;
    case ipc_frame::EnvelopeStatus::UnsupportedVersion:
        error = IpcReply::Fail("UNSUPPORTED_VERSION", L"Неподдерживаемая версия протокола");
        return false;
    case ipc_frame::EnvelopeStatus::TooLarge:
        error = IpcReply::Fail("TOO_LARGE", L"Превышен размер или глубина JSON");
        return false;
    case ipc_frame::EnvelopeStatus::Invalid:
    default:
        error = IpcReply::Fail("VALIDATION", L"Некорректный конверт запроса");
        return false;
    }

    req.legacy = envelope.legacy;
    req.action = u2ws(envelope.action);
    req.request_id = u2ws(envelope.id);

    json::Limits limits;
    limits.max_bytes = ipc_frame::MAX_FRAME_BYTES;
    limits.max_depth = ipc_frame::MAX_DEPTH;
    limits.max_string_bytes = ipc_frame::MAX_STRING_BYTES;

    auto payload = json::parse_ex(envelope.payload, limits);
    if (!payload.ok()) {
        error = IpcReply::Fail("VALIDATION", L"Некорректный payload запроса");
        return false;
    }
    req.payload = std::move(payload.value);

    if (req.request_id.size() > 128) {
        error = IpcReply::Fail("VALIDATION", L"Слишком длинный идентификатор запроса");
        return false;
    }
    if (req.action.size() > 64) {
        error = IpcReply::Fail("VALIDATION", L"Слишком длинное имя действия");
        return false;
    }
    // Контракт v1: id — GUID запроса, payload — объект (IPC.md раздел 2).
    if (!req.legacy) {
        if (!auth::is_guid(req.request_id)) {
            error = IpcReply::Fail("VALIDATION", L"id должен быть GUID");
            return false;
        }
        if (!req.payload.is_object()) {
            error = IpcReply::Fail("VALIDATION", L"payload должен быть объектом");
            return false;
        }
    }
    return true;
}

void IpcServer::handle_client(HANDLE pipe) {
    struct PipeGuard {
        HANDLE handle;
        ~PipeGuard() {
            // Дожидаемся, пока клиент прочитает ответ, и закрываем дескриптор.
            // DisconnectNamedPipe не вызываем: он возвращает инстанс в режим
            // ожидания и создаёт окно гонки, в котором новый клиент может
            // подключиться к уже закрываемому каналу.
            FlushFileBuffers(handle);
            CloseHandle(handle);
        }
    } guard{ pipe };

    // Освобождает токен клиента и контекст потока при любом выходе.
    struct ContextGuard {
        ~ContextGuard() {
            HANDLE token = auth::current_client_token();
            if (token != nullptr) CloseHandle(token);
            auth::clear_current_context();
        }
    } context_guard;

    auth::Identity identity;
    bool authenticated = false;

    while (running_) {
        Frame frame;
        const ReadResult read_result = read_frame(pipe, running_, frame);
        if (read_result == ReadResult::TooLarge) {
            // Контракт IPC.md §1: ответ TOO_LARGE и закрытие соединения.
            const IpcReply too_large = IpcReply::Fail("TOO_LARGE", L"Превышен размер кадра");
            write_all(pipe, ipc_frame::encode(build_v2_response(too_large, L"")));
            break;
        }
        if (read_result != ReadResult::Ok) break;

        // ImpersonateNamedPipeClient возможен только после того, как клиент
        // записал данные в канал, поэтому идентичность собирается на первом кадре.
        if (!authenticated) {
            std::wstring auth_error;
            if (!auth::impersonate_client(pipe, identity, auth_error)) {
                log_msg(log_dir_, "ipc: authentication failed: " + ws2u(auth_error));
                audit::write(log_dir_, db_, L"", L"Authenticate", L"connection", L"",
                             audit::Result::Denied, auth_error);
                return;
            }
            authenticated = true;

            log_msg(log_dir_, "ipc: connected sid=" + ws2u(identity.sid) +
                              " admin=" + (identity.is_admin ? "1" : "0") +
                              " operator=" + (identity.is_operator ? "1" : "0"));

            if (!identity.is_admin && !identity.is_operator) {
                // Гость может только Ping — политика отклонит остальное.
                audit::write(log_dir_, db_, identity.sid, L"Authenticate", L"connection",
                             L"", audit::Result::Denied, L"нет членства в группе WinSheduler Users");
            }
        }

        ParsedRequest request;
        IpcReply error;
        IpcReply reply;
        if (!parse_request(frame.body, request, error)) {
            reply = error;
        } else {
            try {
                reply = dispatch(request, identity);
            } catch (const std::exception& e) {
                log_msg(log_dir_, "ipc: handler exception: " + std::string(e.what()));
                reply = IpcReply::Fail("INTERNAL", L"Внутренняя ошибка службы");
            } catch (...) {
                log_msg(log_dir_, "ipc: handler unknown exception");
                reply = IpcReply::Fail("INTERNAL", L"Внутренняя ошибка службы");
            }
        }

        std::string response = frame.legacy ? build_legacy_response(reply)
                                            : build_v2_response(reply, request.request_id);
        if (response.size() > ipc_frame::MAX_FRAME_BYTES) {
            const IpcReply too_large = IpcReply::Fail("TOO_LARGE", L"Ответ превышает лимит кадра");
            response = frame.legacy ? build_legacy_response(too_large)
                                    : build_v2_response(too_large, request.request_id);
        }

        const std::string outgoing = frame.legacy ? response : ipc_frame::encode(response);
        if (!write_all(pipe, outgoing)) break;

        // Legacy-клиент получает один ответ на соединение.
        if (frame.legacy) break;
    }
}

void IpcServer::write_audit(const auth::Identity& identity, const wchar_t* action,
                            const wchar_t* object_type, const std::wstring& object_id,
                            audit::Result result, const std::wstring& details) {
    audit::write(log_dir_, db_, identity.sid, action, object_type, object_id, result, details);
}

std::optional<TaskDefinition> IpcServer::load_task_checked(const std::wstring& task_id,
                                                           const auth::Identity& identity,
                                                           IpcReply& denial) {
    auto task = db_.get_task(task_id);
    if (!task.has_value()) {
        denial = IpcReply::Fail("NOT_FOUND", L"Задача не найдена");
        return std::nullopt;
    }
    if (!auth::Policy::can_access_task(identity, task->owner_sid)) {
        write_audit(identity, L"AccessTask", L"task", task_id, audit::Result::Denied, L"чужая задача");
        denial = IpcReply::Fail("AUTH_DENIED", L"Недостаточно прав");
        return std::nullopt;
    }
    return task;
}

IpcReply IpcServer::dispatch(const ParsedRequest& req, const auth::Identity& identity) {
    const auth::Action action = auth::action_from_name(req.action);

    if (action == auth::Action::Unknown) {
        return IpcReply::Fail("VALIDATION", L"Неизвестное действие: " + req.action);
    }
    if (!auth::Policy::allows(identity, action)) {
        write_audit(identity, req.action.c_str(), L"action", L"", audit::Result::Denied, L"нет прав");
        return IpcReply::Fail("AUTH_DENIED", L"Недостаточно прав");
    }

    switch (action) {
    case auth::Action::Ping: return handle_ping();
    case auth::Action::GetStatus: return handle_status(identity, req.legacy);
    case auth::Action::GetDashboard: return handle_dashboard(identity);
    case auth::Action::ListTasks: return handle_list_tasks(req, identity);
    case auth::Action::GetTask: return handle_get_task(req, identity);
    case auth::Action::GetTimeWindows: return handle_get_time_windows(req, identity);
    case auth::Action::GetHistory: return handle_get_history(req, identity);
    case auth::Action::CreateTask: return handle_create_task(req, identity);
    case auth::Action::UpdateTask: return handle_update_task(req, identity);
    case auth::Action::DeleteTask: return handle_delete_task(req, identity);
    case auth::Action::SetTaskEnabled: {
        // Legacy: EnableTask/DisableTask несут идентификатор в payload.
        bool enabled;
        if (req.action == L"DisableTask") enabled = false;
        else if (req.action == L"EnableTask") enabled = true;
        else enabled = bool_field(req.payload, { "enabled", "Enabled" }, false);
        return handle_set_enabled(req, identity, enabled);
    }
    case auth::Action::RunNow: return handle_run_now(req, identity);
    case auth::Action::GetAudit: return handle_get_audit(req);
    case auth::Action::GetSettings: return handle_get_settings();
    case auth::Action::SetSettings: return handle_set_settings(req, identity);
    case auth::Action::Unknown: break;
    }
    return IpcReply::Fail("VALIDATION", L"Неизвестное действие");
}

IpcReply IpcServer::handle_ping() {
    json::Object obj;
    obj["pong"] = json::Value(true);
    obj["serviceVersion"] = json::Value(ws2u(SERVICE_VERSION));
    return IpcReply::Ok(json::serialize(obj));
}

IpcReply IpcServer::handle_status(const auth::Identity& identity, bool legacy) {
    ServiceStatus status;
    status.running = true;
    status.started_at = L"";

    // Данные о счётчиках и времени старта берём у планировщика.
    IpcResponse from_scheduler;
    if (scheduler_handler_) from_scheduler = scheduler_handler_(IpcRequest{ L"GetStatus", L"" });
    if (from_scheduler.success) {
        auto parsed = json::parse_ex(ws2u(from_scheduler.payload));
        if (parsed.ok() && parsed.value.is_object()) {
            const json::Value& v = parsed.value;
            status.running = bool_field(v, { "running", "Running" }, true);
            status.task_count = int_field(v, { "taskCount", "TaskCount" }, 0);
            status.running_task_count = int_field(v, { "runningTaskCount", "RunningTaskCount" }, 0);
            status.started_at = str_field(v, { "startedAt", "StartedAt" });
        }
    } else {
        status.task_count = total_task_count(db_);
    }
    if (status.started_at.empty()) status.started_at = u2ws(utc_now_iso());

    status.is_admin = identity.is_admin;
    status.is_operator = identity.is_operator || identity.is_admin;
    status.can_manage_system_tasks = identity.is_admin;
    status.service_version = SERVICE_VERSION;
    status.db_ready = db_.get_db() != nullptr;

    return IpcReply::Ok(legacy ? json_helpers::status_to_json(status) : status_to_json_v2(status));
}

IpcReply IpcServer::handle_list_tasks(const ParsedRequest& req, const auth::Identity& identity) {
    auto visible = list_tasks_for(db_, identity);

    // Legacy-клиент ожидает прежний формат (PascalCase).
    if (req.legacy) {
        return IpcReply::Ok(json_helpers::task_list_to_json(visible));
    }

    json::Array arr;
    for (auto& task : visible) {
        auto windows = db_.get_time_windows(task.id);
        arr.push_back(json::parse(json_helpers::task_to_json_v2(task, windows, identity, true)));
    }
    return IpcReply::Ok(json::serialize(arr));
}

IpcReply IpcServer::handle_dashboard(const auth::Identity& identity) {
    auto tasks = list_tasks_for(db_, identity);
    json::Array arr;
    for (auto& task : tasks) {
        const bool is_own = auth::equal_sid(task.owner_sid, identity.sid);
        auto windows = db_.get_time_windows(task.id);
        auto history = db_.get_history(task.id, 1);

        json::Object item;
        item["id"] = json::Value(ws2u(task.id));
        item["name"] = json::Value(ws2u(task.name));
        item["enabled"] = json::Value(task.enabled);
        item["isOwn"] = json::Value(is_own);
        item["ownerLabel"] = json::Value(is_own ? "Вы" : "Другой пользователь");
        item["programPath"] = json::Value(ws2u(task.program_path));
        item["scheduleSummary"] = json::Value(ws2u(schedule_summary(windows)));
        item["running"] = json::Value(!history.empty() && history.front().status == RunStatus::Running);

        if (!history.empty()) {
            const RunHistory& h = history.front();
            json::Object last;
            last["startTime"] = json::Value(ws2u(h.start_time));
            last["status"] = json::Value(ws2u(ToWide(h.status)));
            last["exitCode"] = json_helpers::opt_int(h.exit_code);
            item["lastRun"] = last;
        } else {
            item["lastRun"] = json::Value(nullptr);
        }

        arr.push_back(item);
    }

    json::Object root;
    root["tasks"] = arr;
    return IpcReply::Ok(json::serialize(root));
}

IpcReply IpcServer::handle_get_task(const ParsedRequest& req, const auth::Identity& identity) {
    const std::wstring task_id = task_id_from(req.payload);
    if (!auth::is_guid(task_id)) {
        return IpcReply::Fail("VALIDATION", L"taskId должен быть GUID");
    }
    IpcReply denial;
    auto task = load_task_checked(task_id, identity, denial);
    if (!task.has_value()) return denial;

    if (req.legacy) return IpcReply::Ok(json_helpers::task_to_json(*task));

    auto windows = db_.get_time_windows(task->id);
    return IpcReply::Ok(json_helpers::task_to_json_v2(*task, windows, identity, true));
}

IpcReply IpcServer::handle_get_time_windows(const ParsedRequest& req, const auth::Identity& identity) {
    const std::wstring task_id = task_id_from(req.payload);
    if (!auth::is_guid(task_id)) {
        return IpcReply::Fail("VALIDATION", L"taskId должен быть GUID");
    }
    IpcReply denial;
    auto task = load_task_checked(task_id, identity, denial);
    if (!task.has_value()) return denial;

    auto windows = db_.get_time_windows(task->id);
    if (req.legacy) return IpcReply::Ok(json_helpers::time_window_list_to_json(windows));
    return IpcReply::Ok(windows_to_json_v2(windows));
}

IpcReply IpcServer::handle_get_history(const ParsedRequest& req, const auth::Identity& identity) {
    const std::wstring task_id = task_id_from(req.payload);
    if (!auth::is_guid(task_id)) {
        return IpcReply::Fail("VALIDATION", L"taskId должен быть GUID");
    }
    IpcReply denial;
    auto task = load_task_checked(task_id, identity, denial);
    if (!task.has_value()) return denial;

    int limit = int_field(req.payload, { "limit", "Limit" }, 100);
    if (limit < 1) limit = 1;
    if (limit > MAX_HISTORY_LIMIT) limit = MAX_HISTORY_LIMIT;

    json::Array arr;
    auto history = db_.get_history(task->id, limit);
    if (req.legacy) return IpcReply::Ok(json_helpers::history_list_to_json(history));
    for (const auto& h : history) {
        arr.push_back(json::parse(json_helpers::history_to_json_v2(h)));
    }
    return IpcReply::Ok(json::serialize(arr));
}

IpcReply IpcServer::handle_create_task(const ParsedRequest& req, const auth::Identity& identity) {
    if (!req.payload.is_object()) {
        return IpcReply::Fail("VALIDATION", L"Payload должен быть объектом TaskDto");
    }
    auto parsed = json_helpers::task_from_json(json::serialize(req.payload));
    if (!parsed.has_value()) return IpcReply::Fail("VALIDATION", L"Некорректный TaskDto");
    TaskDefinition task = *parsed;

    const std::wstring password = str_field(req.payload, { "password", "Password" });
    if (!identity.is_admin) {
        if (task.run_as_user.has_value() || !password.empty()) {
            write_audit(identity, L"CreateTask", L"task", L"", audit::Result::Denied,
                        L"RunAs запрещён непривилегированному клиенту");
            return IpcReply::Fail("AUTH_DENIED", L"RunAs доступен только администратору");
        }
        if (task.session_mode != SessionMode::Headless) {
            return IpcReply::Fail("AUTH_DENIED", L"Режим ActiveUser доступен только администратору");
        }
    }

    std::wstring error;
    if (!auth::validate_task(task, identity.is_admin, error)) {
        return IpcReply::Fail("VALIDATION", error);
    }

    if (is_empty_or_zero_guid(task.id)) {
        task.id = new_guid();
        if (task.id.empty()) return IpcReply::Fail("INTERNAL", L"Не удалось создать идентификатор");
    } else if (!auth::is_guid(task.id)) {
        return IpcReply::Fail("VALIDATION", L"id должен быть GUID");
    }
    if (db_.get_task(task.id).has_value()) {
        return IpcReply::Fail("CONFLICT", L"Задача с таким идентификатором уже существует");
    }

    auto windows = windows_from_object(req.payload);
    for (const auto& window : windows) {
        if (!auth::validate_time_window(window, error)) {
            return IpcReply::Fail("VALIDATION", error);
        }
    }

    if (!password.empty()) {
        const std::wstring encrypted = auth::protect_secret(password);
        if (encrypted.empty()) return IpcReply::Fail("INTERNAL", L"Не удалось зашифровать пароль");
        RunAsCredentials credentials = task.run_as_user.value_or(RunAsCredentials{});
        credentials.encrypted_password = encrypted;
        task.run_as_user = credentials;
    }

    task.owner_sid = identity.sid;
    task.created_at = u2ws(utc_now_iso());
    task.modified_at = task.created_at;

    db_.insert_task(task);
    for (auto& window : windows) {
        window.task_id = task.id;
        if (is_empty_or_zero_guid(window.id)) window.id = new_guid();
    }
    replace_windows(db_, task.id, windows);

    write_audit(identity, L"CreateTask", L"task", task.id, audit::Result::Ok, L"создана");
    if (req.legacy) return IpcReply::Ok(json_helpers::task_to_json(task));
    return IpcReply::Ok(json_helpers::task_to_json_v2(task, windows, identity, true));
}

IpcReply IpcServer::handle_update_task(const ParsedRequest& req, const auth::Identity& identity) {
    if (!req.payload.is_object()) {
        return IpcReply::Fail("VALIDATION", L"Payload должен быть объектом TaskDto");
    }
    const std::wstring task_id = task_id_from(req.payload);
    if (!auth::is_guid(task_id)) {
        return IpcReply::Fail("VALIDATION", L"taskId должен быть GUID");
    }

    IpcReply denial;
    auto existing = load_task_checked(task_id, identity, denial);
    if (!existing.has_value()) return denial;

    auto parsed = json_helpers::task_from_json(json::serialize(req.payload));
    if (!parsed.has_value()) return IpcReply::Fail("VALIDATION", L"Некорректный TaskDto");
    TaskDefinition task = *parsed;

    const std::wstring password = str_field(req.payload, { "password", "Password" });
    if (!identity.is_admin) {
        if (task.run_as_user.has_value() || !password.empty()) {
            write_audit(identity, L"UpdateTask", L"task", task_id, audit::Result::Denied,
                        L"RunAs запрещён непривилегированному клиенту");
            return IpcReply::Fail("AUTH_DENIED", L"RunAs доступен только администратору");
        }
        if (task.session_mode != SessionMode::Headless) {
            return IpcReply::Fail("AUTH_DENIED", L"Режим ActiveUser доступен только администратору");
        }
    }

    std::wstring error;
    if (!auth::validate_task(task, identity.is_admin, error)) {
        return IpcReply::Fail("VALIDATION", error);
    }

    // Владелец и дата создания не меняются (IPC.md раздел 4).
    task.id = task_id;
    task.owner_sid = existing->owner_sid;
    task.created_at = existing->created_at;
    task.modified_at = u2ws(utc_now_iso());

    if (!password.empty()) {
        const std::wstring encrypted = auth::protect_secret(password);
        if (encrypted.empty()) return IpcReply::Fail("INTERNAL", L"Не удалось зашифровать пароль");
        RunAsCredentials credentials = task.run_as_user.value_or(RunAsCredentials{});
        credentials.encrypted_password = encrypted;
        task.run_as_user = credentials;
    } else if (task.run_as_user.has_value() && existing->run_as_user.has_value()) {
        // Пароль не передан — сохраняем прежний blob.
        task.run_as_user->encrypted_password = existing->run_as_user->encrypted_password;
    }

    auto windows = windows_from_object(req.payload);
    for (const auto& window : windows) {
        if (!auth::validate_time_window(window, error)) {
            return IpcReply::Fail("VALIDATION", error);
        }
    }

    db_.update_task(task);

    // TimeWindows заменяют набор триггеров целиком. Замена выполняется
    // транзакцией трека C (replace_time_windows) либо, до слияния, — без
    // BEGIN из обработчика IPC (DB.md раздел 5). Если поле не передано,
    // существующие триггеры сохраняются.
    const bool has_windows = field(req.payload, { "timeWindows", "TimeWindows" }) != nullptr;
    if (has_windows) {
        for (auto& window : windows) {
            window.task_id = task.id;
            if (is_empty_or_zero_guid(window.id)) window.id = new_guid();
        }
        replace_windows(db_, task.id, windows);
    } else {
        windows = db_.get_time_windows(task.id);
    }

    write_audit(identity, L"UpdateTask", L"task", task.id, audit::Result::Ok,
                password.empty() ? L"изменена" : L"изменена, пароль обновлён");
    if (req.legacy) return IpcReply::Ok(json_helpers::task_to_json(task));
    return IpcReply::Ok(json_helpers::task_to_json_v2(task, windows, identity, true));
}

IpcReply IpcServer::handle_delete_task(const ParsedRequest& req, const auth::Identity& identity) {
    const std::wstring task_id = task_id_from(req.payload);
    if (!auth::is_guid(task_id)) {
        return IpcReply::Fail("VALIDATION", L"taskId должен быть GUID");
    }
    IpcReply denial;
    auto task = load_task_checked(task_id, identity, denial);
    if (!task.has_value()) return denial;

    db_.delete_task(task->id);
    write_audit(identity, L"DeleteTask", L"task", task->id, audit::Result::Ok, L"удалена");
    return IpcReply::Ok("{}");
}

IpcReply IpcServer::handle_set_enabled(const ParsedRequest& req, const auth::Identity& identity,
                                       bool enabled) {
    const std::wstring task_id = task_id_from(req.payload);
    if (!auth::is_guid(task_id)) {
        return IpcReply::Fail("VALIDATION", L"taskId должен быть GUID");
    }
    IpcReply denial;
    auto task = load_task_checked(task_id, identity, denial);
    if (!task.has_value()) return denial;

    db_.set_task_enabled(task->id, enabled);
    write_audit(identity, L"SetTaskEnabled", L"task", task->id, audit::Result::Ok,
                enabled ? L"включена" : L"выключена");
    return IpcReply::Ok("{}");
}

IpcReply IpcServer::handle_run_now(const ParsedRequest& req, const auth::Identity& identity) {
    const std::wstring task_id = task_id_from(req.payload);
    if (!auth::is_guid(task_id)) {
        return IpcReply::Fail("VALIDATION", L"taskId должен быть GUID");
    }
    IpcReply denial;
    auto task = load_task_checked(task_id, identity, denial);
    if (!task.has_value()) return denial;

    if (!scheduler_handler_) {
        return IpcReply::Fail("INTERNAL", L"Планировщик недоступен");
    }

    // Планировщик получает идентификатор задачи в payload, как и раньше.
    const IpcResponse response = scheduler_handler_(IpcRequest{ L"RunNow", task->id });
    if (!response.success) {
        write_audit(identity, L"RunNow", L"task", task->id, audit::Result::Error, response.error);
        return IpcReply::Fail("INTERNAL", response.error.empty() ? L"Запуск отклонён" : response.error);
    }

    write_audit(identity, L"RunNow", L"task", task->id, audit::Result::Ok, L"ручной запуск");
    return IpcReply::Ok("{}");
}

IpcReply IpcServer::handle_get_audit(const ParsedRequest& req) {
    const std::wstring from = str_field(req.payload, { "from", "From" });
    const std::wstring to = str_field(req.payload, { "to", "To" });
    int limit = int_field(req.payload, { "limit", "Limit" }, 500);
    if (limit < 1) limit = 1;
    if (limit > MAX_AUDIT_FETCH) limit = MAX_AUDIT_FETCH;

    json::Array arr;

#ifdef WINSHEDULER_DB_REPOSITORY
    // Репозиторий трека C: единый writer и параметризованный SELECT.
    for (const auto& entry : db_.list_audit(from, to, limit)) {
        json::Object item;
        item["timestamp"] = json::Value(ws2u(entry.ts));
        item["actorSid"] = json::Value(ws2u(entry.actor_sid));
        item["action"] = json::Value(ws2u(entry.action));
        item["objectType"] = json::Value(ws2u(entry.object_type));
        item["objectId"] = json::Value(ws2u(entry.object_id.value_or(std::wstring())));
        item["result"] = json::Value(ws2u(entry.result));
        item["details"] = json::Value(ws2u(entry.details));
        arr.push_back(item);
    }
#else
    sqlite3* db = db_.get_db();
    if (db == nullptr) return IpcReply::Ok(json::serialize(arr));

    const char* sql =
        "SELECT ts,actor_sid,action,object_type,object_id,result,details FROM audit_log "
        "WHERE (?1='' OR ts >= ?1) AND (?2='' OR ts <= ?2) ORDER BY ts DESC LIMIT ?3";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        // Схема v2 ещё не установлена — аудит ведётся в файле.
        return IpcReply::Ok(json::serialize(arr));
    }
    const std::string from_u8 = ws2u(from);
    const std::string to_u8 = ws2u(to);
    sqlite3_bind_text(stmt, 1, from_u8.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, to_u8.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        json::Object entry;
        entry["timestamp"] = json::Value(std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0))));
        entry["actorSid"] = json::Value(std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1))));
        entry["action"] = json::Value(std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2))));
        entry["objectType"] = json::Value(std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3))));
        auto* object_id = sqlite3_column_text(stmt, 4);
        entry["objectId"] = json::Value(object_id ? std::string(reinterpret_cast<const char*>(object_id))
                                                  : std::string());
        entry["result"] = json::Value(std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5))));
        entry["details"] = json::Value(std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6))));
        arr.push_back(entry);
    }
    sqlite3_finalize(stmt);
#endif

    return IpcReply::Ok(json::serialize(arr));
}

IpcReply IpcServer::handle_get_settings() {
    json::Object obj;

#ifdef WINSHEDULER_DB_REPOSITORY
    // Репозиторий трека C: чтение через единый writer-мьютекс.
    for (const auto& [key, value] : db_.list_settings()) {
        obj[ws2u(key)] = json::Value(ws2u(value));
    }
#else
    sqlite3* db = db_.get_db();
    if (db == nullptr) return IpcReply::Ok(json::serialize(obj));

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT key,value FROM settings", -1, &stmt, nullptr) != SQLITE_OK) {
        return IpcReply::Ok(json::serialize(obj));
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        auto* key = sqlite3_column_text(stmt, 0);
        auto* value = sqlite3_column_text(stmt, 1);
        if (key != nullptr && value != nullptr) {
            obj[reinterpret_cast<const char*>(key)] = json::Value(std::string(reinterpret_cast<const char*>(value)));
        }
    }
    sqlite3_finalize(stmt);
#endif

    return IpcReply::Ok(json::serialize(obj));
}

IpcReply IpcServer::handle_set_settings(const ParsedRequest& req, const auth::Identity& identity) {
    if (!req.payload.is_object()) {
        return IpcReply::Fail("VALIDATION", L"Payload должен быть объектом");
    }

    int written = 0;
#ifdef WINSHEDULER_DB_REPOSITORY
    try {
        for (const auto& [key, value] : req.payload.as_object()) {
            if (!value.is_string()) continue;
            db_.set_setting(u2ws(key), u2ws(value.as_string()));
            ++written;
        }
    } catch (...) {
        return IpcReply::Fail("DB", L"Не удалось сохранить настройки");
    }
#else
    sqlite3* db = db_.get_db();
    if (db == nullptr) return IpcReply::Fail("DB", L"База данных недоступна");

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO settings (key,value) VALUES (?,?)",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        return IpcReply::Fail("DB", L"Таблица настроек недоступна");
    }
    for (const auto& [key, value] : req.payload.as_object()) {
        if (!value.is_string()) continue;
        sqlite3_reset(stmt);
        sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, value.as_string().c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        ++written;
    }
    sqlite3_finalize(stmt);
#endif

    write_audit(identity, L"SetSettings", L"settings", L"", audit::Result::Ok,
                std::to_wstring(written) + L" ключей");
    return IpcReply::Ok("{}");
}
