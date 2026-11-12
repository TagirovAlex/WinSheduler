#include "scheduler.h"
#include "db_compat.h"
#include "json.h"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <rpc.h>
#include <rpcdce.h>
#include <cwchar>
#include <lm.h>
#include <sddl.h>

#pragma comment(lib, "rpcrt4.lib")
#pragma comment(lib, "netapi32.lib")

static constexpr size_t MAX_LOG_SIZE = 512 * 1024; // 512 КБ

// Общая функция логирования: используется планировщиком и IPC (см. ipc.h).
void log_msg(const std::wstring& log_dir, const std::string& msg) {
    auto path = log_dir + L"\\scheduler.log";
    // Ротация по размеру.
    {
        std::ifstream check(path, std::ios::binary | std::ios::ate);
        if (check.is_open() && static_cast<size_t>(check.tellg()) > MAX_LOG_SIZE) {
            check.close();
            auto bak = log_dir + L"\\scheduler.log.bak";
            MoveFileExW(path.c_str(), bak.c_str(), MOVEFILE_REPLACE_EXISTING);
        }
    }
    std::ofstream f(path, std::ios::app);
    if (f) {
        auto now = std::chrono::system_clock::now();
        auto now_t = std::chrono::system_clock::to_time_t(now);
        std::tm tm;
        localtime_s(&tm, &now_t);
        char buf[64];
        strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
        f << buf << " " << msg << std::endl;
    }
}

namespace {

std::wstring generate_guid() {
    GUID guid;
    UuidCreate(&guid);
    wchar_t buf[64];
    StringFromGUID2(guid, buf, 64);
    std::wstring s(buf + 1);
    s.pop_back();
    for (auto& c : s) c = towlower(c);
    return s;
}

std::string detect_pe_architecture(const std::wstring& path) {
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return "unknown (file not found)";
    IMAGE_DOS_HEADER dos = {};
    DWORD read = 0;
    BOOL ok = ReadFile(hFile, &dos, sizeof(dos), &read, nullptr);
    if (!ok || read != sizeof(dos) || dos.e_magic != IMAGE_DOS_SIGNATURE) {
        CloseHandle(hFile);
        return "unknown (bad DOS header)";
    }
    SetFilePointer(hFile, dos.e_lfanew, nullptr, FILE_BEGIN);
    DWORD pe_sig = 0;
    ok = ReadFile(hFile, &pe_sig, sizeof(pe_sig), &read, nullptr);
    if (!ok || read != sizeof(pe_sig) || pe_sig != IMAGE_NT_SIGNATURE) {
        CloseHandle(hFile);
        return "unknown (bad PE signature)";
    }
    IMAGE_FILE_HEADER fh = {};
    ok = ReadFile(hFile, &fh, sizeof(fh), &read, nullptr);
    CloseHandle(hFile);
    if (!ok || read != sizeof(fh)) return "unknown (bad COFF header)";
    switch (fh.Machine) {
        case IMAGE_FILE_MACHINE_AMD64: return "x64";
        case IMAGE_FILE_MACHINE_I386:  return "x86";
        case IMAGE_FILE_MACHINE_ARM64: return "ARM64";
        case IMAGE_FILE_MACHINE_ARM:   return "ARM";
        default: return "unknown (machine=" + std::to_string(fh.Machine) + ")";
    }
}

bool is_system_sid(const std::wstring& sid) {
    return sid.empty() || _wcsicmp(sid.c_str(), L"S-1-5-18") == 0;
}

// Задача, созданная администратором, по контракту SECURITY.md запускается
// токеном службы (SYSTEM). Проверяем членство владельца в локальной группе
// Administrators, включая косвенное (доменные группы). Имя группы берём через
// SID: на русской Windows она называется «Администраторы».
bool is_admin_sid(const std::wstring& sid_text) {
    if (is_system_sid(sid_text)) return true;

    PSID sid = nullptr;
    if (!ConvertStringSidToSidW(sid_text.c_str(), &sid)) return false;

    BYTE admin_sid[SECURITY_MAX_SID_SIZE] = {};
    DWORD admin_sid_size = sizeof(admin_sid);
    wchar_t admin_name[256] = {};
    wchar_t admin_domain[256] = {};
    DWORD admin_name_length = 256;
    DWORD admin_domain_length = 256;
    SID_NAME_USE admin_use = SidTypeUnknown;
    const bool have_admin_name =
        CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, admin_sid, &admin_sid_size) &&
        LookupAccountSidW(nullptr, admin_sid, admin_name, &admin_name_length,
                          admin_domain, &admin_domain_length, &admin_use);

    bool member = false;

    if (have_admin_name) {
        LPBYTE buffer = nullptr;
        DWORD read = 0;
        DWORD total = 0;
        if (NetLocalGroupGetMembers(nullptr, admin_name, 0, &buffer, MAX_PREFERRED_LENGTH,
                &read, &total, nullptr) == NERR_Success && buffer != nullptr) {
            auto* members = reinterpret_cast<LOCALGROUP_MEMBERS_INFO_0*>(buffer);
            for (DWORD i = 0; i < read; ++i) {
                if (EqualSid(members[i].lgrmi0_sid, sid)) {
                    member = true;
                    break;
                }
            }
            NetApiBufferFree(buffer);
        }
    }

    if (!member) {
        wchar_t name[256] = {};
        wchar_t domain[256] = {};
        DWORD name_length = 256;
        DWORD domain_length = 256;
        SID_NAME_USE use = SidTypeUnknown;
        if (LookupAccountSidW(nullptr, sid, name, &name_length, domain, &domain_length, &use)) {
            const std::wstring account =
                domain_length > 0 ? std::wstring(domain) + L"\\" + name : std::wstring(name);
            LPBYTE groups = nullptr;
            DWORD group_read = 0;
            DWORD group_total = 0;
            if (NetUserGetLocalGroups(nullptr, account.c_str(), 0, LG_INCLUDE_INDIRECT,
                    &groups, MAX_PREFERRED_LENGTH, &group_read, &group_total) == NERR_Success &&
                groups != nullptr) {
                auto* info = reinterpret_cast<LOCALGROUP_USERS_INFO_0*>(groups);
                for (DWORD i = 0; i < group_read; ++i) {
                    if (have_admin_name && _wcsicmp(info[i].lgrui0_name, admin_name) == 0) {
                        member = true;
                        break;
                    }
                }
                NetApiBufferFree(groups);
            }
        }
    }

    LocalFree(sid);
    return member;
}

// Пауза перед повтором: 1, 2, 4, 8 … минут, не более 30 минут.
std::chrono::seconds retry_backoff(int attempt) {
    const int exponent = attempt < 5 ? attempt : 5;
    const int minutes = (1 << exponent) < 30 ? (1 << exponent) : 30;
    return std::chrono::minutes(minutes);
}

} // namespace

Scheduler::Scheduler(Database& db, const std::wstring& log_dir)
    : Scheduler(db, log_dir, std::make_unique<Win32ProcessRunner>(), nullptr, nullptr) {
}

Scheduler::Scheduler(Database& db, const std::wstring& log_dir,
    std::unique_ptr<IProcessRunner> runner, std::unique_ptr<ITriggerStateStore> state,
    std::unique_ptr<IClock> clock)
    : db_(db),
      log_dir_(log_dir),
      clock_(std::move(clock)),
      runner_(std::move(runner)),
      state_(std::move(state)),
      cache_(std::make_unique<ScheduleCache>(db)) {
    if (!clock_) clock_ = std::make_unique<SystemClock>();
    if (!runner_) runner_ = std::make_unique<Win32ProcessRunner>();

    // По умолчанию состояние срабатываний хранится в БД (trigger_state, трек C).
    if (!state_) state_ = std::make_unique<DatabaseTriggerStateStore>(db_);
}

Scheduler::~Scheduler() {
    stop();
}

void Scheduler::start() {
    if (running_) return;

    // Осиротевшие Running после падения службы помечаются Aborted (ADR 0003, п. 7).
    recover_orphaned_runs();

    // Прогрев кэша расписания: первый тик не должен читать БД по задачам.
    cache_->get();

    // Чистка файлов вывода по retention (DB.md, раздел 6).
    dbcompat::purge_expired_outputs(db_);
    last_purge_day_ = current_day();

    running_ = true;
    started_at_ = clock_->now_utc();
    stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    thread_ = std::thread(&Scheduler::worker_thread, this);
}

void Scheduler::stop() {
    running_ = false;
    if (stop_event_) SetEvent(stop_event_);
    if (tick_timer_) CancelWaitableTimer(tick_timer_);

    // Тик короткий (кэш в памяти), поэтому поток завершается за один период.
    if (thread_.joinable()) thread_.join();

    // Оставшиеся процессы завершаем и помечаем Killed.
    std::vector<RunningProcess> remaining;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        for (auto& [history_id, run] : running_processes_) remaining.push_back(run);
    }
    for (auto& run : remaining) {
        runner_->terminate(run.handle);
        finish_run(run, RunStatus::Killed, std::optional<int>(1), L"Служба остановлена");
    }
    {
        // Страховка от записей, которых уже нет в running_processes_.
        std::lock_guard<std::mutex> guard(mutex_);
        for (auto& [history_id, run] : running_processes_) runner_->release(run.handle);
        running_processes_.clear();
        pending_.clear();
    }

    if (stop_event_) { CloseHandle(stop_event_); stop_event_ = nullptr; }
    if (tick_timer_) { CloseHandle(tick_timer_); tick_timer_ = nullptr; }
}

ServiceStatus Scheduler::get_status() {
    ServiceStatus s;
    s.running = running_;
    s.task_count = task_count();
    s.running_task_count = running_task_count();
    // Время старта — событие, поэтому в UTC (см. DB.md).
    s.started_at = trigger::format_utc_iso(started_at_);
    return s;
}

void Scheduler::tick_once() {
    tick();
}

int Scheduler::task_count() const {
    // Счётчик задач — через репозиторий трека C (без SQL в планировщике).
    return db_.task_count();
}

int Scheduler::running_task_count() const {
    std::lock_guard<std::mutex> guard(mutex_);
    // Считаем уникальные задачи: при StartAnother их может быть несколько.
    std::vector<std::wstring> seen;
    for (const auto& [history_id, run] : running_processes_) {
        bool known = false;
        for (const auto& task_id : seen) {
            if (task_id == run.task_id) { known = true; break; }
        }
        if (!known) seen.push_back(run.task_id);
    }
    return static_cast<int>(seen.size());
}

void Scheduler::invalidate_schedule() {
    cache_->invalidate();
}

void Scheduler::on_task_changed(const std::wstring& task_id) {
    // Кэш перечитывается, состояние слотов задачи сбрасывается: после правки
    // расписания старые отметки не должны блокировать новые запуски.
    cache_->invalidate();
    if (!task_id.empty()) state_->forget_task(task_id);
}

int64_t Scheduler::current_day() const {
    return std::chrono::duration_cast<std::chrono::hours>(clock_->now_utc().time_since_epoch()).count() / 24;
}

void Scheduler::prune_trigger_state(const LocalDateTime& now_local) {
    // Состояние слотов хранится ограниченное время: старые ключи не нужны.
    constexpr int kRetentionDays = 90;
    int64_t day = trigger::days_from_civil(now_local.year, now_local.month, now_local.day) - kRetentionDays;
    LocalDateTime cutoff;
    trigger::civil_from_days(day, cutoff.year, cutoff.month, cutoff.day);
    const std::wstring before_slot = trigger::format_slot(cutoff);

    const auto entries = cache_->get();
    for (const auto& entry : *entries) {
        dbcompat::prune_trigger_state(db_, entry.task.id, before_slot);
    }
}

void Scheduler::worker_thread() {
    tick_timer_ = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    if (!tick_timer_) return;

    LARGE_INTEGER due;
    due.QuadPart = -10000LL; // первый тик через 1 мс
    SetWaitableTimer(tick_timer_, &due, SCHEDULER_TICK_MS, nullptr, nullptr, FALSE);

    log_msg(log_dir_, "worker_thread started");
    while (running_) {
        HANDLE events[2] = { tick_timer_, stop_event_ };
        DWORD wait = WaitForMultipleObjects(2, events, FALSE, INFINITE);
        if (wait == WAIT_OBJECT_0 + 1 || !running_) break;
        if (wait == WAIT_OBJECT_0) tick();
    }
    log_msg(log_dir_, "worker_thread stopped");
}

void Scheduler::tick() {
    try {
        check_running();

        const auto now_utc = clock_->now_utc();
        const auto now_local = clock_->to_local(now_utc);

        // Ежедневное обслуживание: файлы вывода и устаревшие слоты.
        const int64_t today = current_day();
        if (today != last_purge_day_) {
            last_purge_day_ = today;
            dbcompat::purge_expired_outputs(db_);
            prune_trigger_state(now_local);
        }
        auto entries = cache_->get();
        dispatch(*entries, now_local);

        process_pending();
    } catch (const std::exception& e) {
        log_msg(log_dir_, "tick exception: " + std::string(e.what()));
    } catch (...) {
        log_msg(log_dir_, "tick unknown exception");
    }
}

void Scheduler::dispatch(const std::vector<ScheduleCache::Entry>& entries, const LocalDateTime& now_local) {
    for (const auto& entry : entries) {
        if (!entry.task.enabled) continue;

        for (const auto& window : entry.windows) {
            // Окно catch-up покрывает задержку тика и переход через полночь.
            const auto slots = trigger::due_slots(window, now_local, trigger::CATCH_UP_MINUTES);
            for (const auto& slot : slots) {
                const std::wstring slot_key = trigger::format_slot(slot);
                // Идемпотентность в минуту: слот фиксируется в trigger_state.
                if (state_->was_fired(entry.task.id, window.id, slot_key)) continue;
                request_start(entry.task, window.id, slot_key, TriggeredBy::Schedule, 0, nullptr, true);
            }
        }
    }
}

std::wstring Scheduler::request_start(const TaskDefinition& task, const std::wstring& window_id,
    const std::wstring& slot, TriggeredBy triggered_by, int attempt,
    HANDLE client_token, bool caller_is_admin) {
    // v1 поддерживает только headless-задачи (ADR 0003, п. 9).
    if (task.session_mode != SessionMode::Headless) {
        log_msg(log_dir_, "отклонён запуск ActiveUser: task=" + ws2u(task.id));
        return {};
    }

    // Политика наложения запусков.
    {
        std::lock_guard<std::mutex> guard(mutex_);
        bool already_running = false;
        for (const auto& [history_id, run] : running_processes_) {
            if (run.task_id == task.id) { already_running = true; break; }
        }

        if (already_running) {
            switch (task.on_overlap) {
            case OnOverlapAction::WaitNext:
                return {};
            case OnOverlapAction::WaitComplete: {
                // Коалесинг: на задачу хранится один ожидающий запуск.
                PendingRun pending;
                pending.task_id = task.id;
                pending.window_id = window_id;
                pending.slot = slot;
                pending.triggered_by = triggered_by;
                pending.attempt = attempt;
                pending.not_before = clock_->now_utc();
                pending_[task.id] = pending;
                return {};
            }
            case OnOverlapAction::StartAnother:
            default:
                break;
            }
        }
    }

    const std::wstring fired_at = trigger::format_utc_iso(clock_->now_utc());

    // Слот фиксируется до запуска: повторный тик не создаст второй процесс.
    // Для повторов (attempt > 0) слот уже зафиксирован ранее.
    if (triggered_by == TriggeredBy::Schedule && attempt == 0) {
        if (!state_->try_mark_fired(task.id, window_id, slot, fired_at)) return {};
    }

    RunHistory history;
    history.id = generate_guid();
    history.task_id = task.id;
    history.start_time = fired_at;
    history.status = RunStatus::Running;
    history.triggered_by = triggered_by;

    try {
        dbcompat::insert_history(db_, history, window_id);
    } catch (const std::exception& e) {
        log_msg(log_dir_, "insert_history: " + std::string(e.what()));
        return {};
    }

    ProcessLaunchSpec spec;
    spec.program_path = task.program_path;
    spec.arguments = task.arguments;
    spec.working_directory = task.working_directory;
    spec.window_style = task.window_style;
    spec.has_run_as = task.run_as_user.has_value();
    if (spec.has_run_as) spec.run_as = task.run_as_user.value();
    spec.client_token = client_token;
    spec.caller_is_admin = caller_is_admin;
    // Не-админская задача без токена владельца не запускается от SYSTEM.
    // Задача с RunAs запускается отдельным токеном пользователя (только админ).
    spec.require_client_token = !spec.has_run_as && !is_admin_sid(task.owner_sid) && client_token == nullptr;

    std::optional<std::wstring> output_path;
    if (task.log_output) {
        const std::wstring directory = log_dir_ + L"\\output\\" + task.id;
        CreateDirectoryW(directory.c_str(), nullptr);
        output_path = directory + L"\\" + history.id + L".log";
        spec.output_path = output_path.value();
    }

    log_msg(log_dir_, "launch task=" + ws2u(task.id) + " history=" + ws2u(history.id)
        + " arch=" + detect_pe_architecture(task.program_path));

    RunningProcessHandle handle;
    std::wstring error;
    if (!runner_->launch(spec, handle, error)) {
        history.end_time = trigger::format_utc_iso(clock_->now_utc());
        history.status = RunStatus::Failed;
        history.error_message = error;
        history.output_path = output_path;
        try {
            db_.update_history(history);
            trim_history(task.id, task.max_history_records);
        } catch (...) {
        }
        log_msg(log_dir_, "launch failed: task=" + ws2u(task.id) + " error=" + ws2u(error));
        return {};
    }

    history.pid = static_cast<int>(handle.pid);
    history.output_path = output_path;
    try {
        db_.update_history(history);
    } catch (...) {
    }

    RunningProcess run;
    run.handle = handle;
    run.history_id = history.id;
    run.task_id = task.id;
    run.window_id = window_id;
    run.slot = slot;
    run.triggered_by = triggered_by;
    run.output_path = output_path;
    run.start_time = clock_->now_utc();
    run.timeout_minutes = task.timeout_minutes.value_or(0);
    run.kill_on_timeout = task.kill_on_timeout;
    run.attempt = attempt;

    {
        std::lock_guard<std::mutex> guard(mutex_);
        running_processes_[history.id] = run;
    }

    log_msg(log_dir_, "started pid=" + std::to_string(handle.pid) + " history=" + ws2u(history.id));
    return history.id;
}

void Scheduler::check_running() {
    std::vector<RunningProcess> snapshot;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        for (const auto& [history_id, run] : running_processes_) snapshot.push_back(run);
    }

    const auto now = clock_->now_utc();
    for (auto& run : snapshot) {
        const ProcessStatus status = runner_->poll(run.handle);
        if (status.exited) {
            if (run.timed_out) {
                finish_run(run, RunStatus::Timeout, std::optional<int>(static_cast<int>(status.exit_code)),
                    L"Превышен таймаут");
            } else if (status.exit_code == 0) {
                finish_run(run, RunStatus::Completed, std::optional<int>(0), L"");
            } else {
                finish_run(run, RunStatus::Failed, std::optional<int>(static_cast<int>(status.exit_code)),
                    L"Код возврата: " + std::to_wstring(status.exit_code));
            }
            continue;
        }

        // timeout_minutes = 0 или NULL — таймаут не задан.
        if (run.timeout_minutes <= 0) continue;
        const auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - run.start_time).count();
        if (elapsed < run.timeout_minutes) continue;

        if (run.kill_on_timeout) {
            runner_->terminate(run.handle);
            finish_run(run, RunStatus::Timeout, std::nullopt, L"Превышен таймаут, дерево процессов завершено");
        } else if (!run.timed_out) {
            // Процесс не убиваем, но фиксируем Timeout один раз и продолжаем следить.
            mark_timeout(run);
        }
    }
}

void Scheduler::mark_timeout(const RunningProcess& run) {
    {
        std::lock_guard<std::mutex> guard(mutex_);
        auto it = running_processes_.find(run.history_id);
        if (it != running_processes_.end()) it->second.timed_out = true;
    }

    RunHistory history;
    history.id = run.history_id;
    history.task_id = run.task_id;
    history.pid = static_cast<int>(run.handle.pid);
    history.status = RunStatus::Timeout;
    history.error_message = L"Превышен таймаут (процесс оставлен работать)";
    history.output_path = run.output_path;
    try {
        db_.update_history(history);
    } catch (const std::exception& e) {
        log_msg(log_dir_, "mark_timeout: " + std::string(e.what()));
    }
    log_msg(log_dir_, "timeout history=" + ws2u(run.history_id) + " (без kill)");
}

void Scheduler::finish_run(const RunningProcess& run, RunStatus status,
    std::optional<int> exit_code, const std::wstring& message) {
    {
        std::lock_guard<std::mutex> guard(mutex_);
        auto it = running_processes_.find(run.history_id);
        if (it != running_processes_.end()) {
            runner_->release(it->second.handle);
            running_processes_.erase(it);
        }
    }

    RunHistory history;
    history.id = run.history_id;
    history.task_id = run.task_id;
    history.end_time = trigger::format_utc_iso(clock_->now_utc());
    history.exit_code = exit_code;
    history.pid = static_cast<int>(run.handle.pid);
    history.status = status;
    history.error_message = message;
    history.output_path = run.output_path;

    try {
        db_.update_history(history);
        const auto entries = cache_->get();
        const TaskDefinition* task = find_task(*entries, run.task_id);
        trim_history(run.task_id, task ? task->max_history_records : 1000);
    } catch (const std::exception& e) {
        log_msg(log_dir_, "update_history: " + std::string(e.what()));
    }

    log_msg(log_dir_, "finished history=" + ws2u(run.history_id) + " status=" + ws2u(ToWide(status)));

    if (status == RunStatus::Failed) enqueue_retry(run);
}

void Scheduler::enqueue_retry(const RunningProcess& run) {
    // Retry/OnError с backoff и лимитом попыток (A-11).
    const auto entries = cache_->get();
    const TaskDefinition* task = find_task(*entries, run.task_id);
    if (!task || task->on_error != OnErrorAction::Retry) return;
    if (run.attempt >= task->retry_count) return;

    PendingRun pending;
    pending.task_id = run.task_id;
    pending.window_id = run.window_id;
    pending.slot = run.slot;
    pending.triggered_by = run.triggered_by;
    pending.attempt = run.attempt + 1;
    pending.not_before = clock_->now_utc() + retry_backoff(run.attempt);

    {
        std::lock_guard<std::mutex> guard(mutex_);
        pending_[run.task_id] = pending;
    }
    log_msg(log_dir_, "retry scheduled task=" + ws2u(run.task_id) + " attempt=" + std::to_string(pending.attempt));
}

void Scheduler::process_pending() {
    std::vector<PendingRun> ready;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        const auto now = clock_->now_utc();
        for (auto it = pending_.begin(); it != pending_.end();) {
            if (it->second.not_before <= now) {
                ready.push_back(it->second);
                it = pending_.erase(it);
            } else {
                ++it;
            }
        }
    }
    if (ready.empty()) return;

    const auto entries = cache_->get();
    for (const auto& pending : ready) {
        const TaskDefinition* task = find_task(*entries, pending.task_id);
        if (!task || !task->enabled) continue;
        request_start(*task, pending.window_id, pending.slot, pending.triggered_by,
            pending.attempt, nullptr, true);
    }
}

const TaskDefinition* Scheduler::find_task(const std::vector<ScheduleCache::Entry>& entries,
    const std::wstring& task_id) const {
    for (const auto& entry : entries) {
        if (entry.task.id == task_id) return &entry.task;
    }
    return nullptr;
}

void Scheduler::trim_history(const std::wstring& task_id, int max_records) {
    db_.trim_history(task_id, max_records);
}

void Scheduler::recover_orphaned_runs() {
    sqlite3* db = db_.get_db();
    if (!db) return;

    // Осиротевшие запуски после падения службы: Running → Aborted (ADR 0003, п. 7).
    const char* sql = "UPDATE run_history SET status=?, end_time=?, error_message=? WHERE status=?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return;

    const std::string message = ws2u(L"Осиротевший запуск: служба была перезапущена");
    const std::string end_time = ws2u(trigger::format_utc_iso(clock_->now_utc()));
    sqlite3_bind_int(stmt, 1, static_cast<int>(RunStatus::Aborted));
    sqlite3_bind_text(stmt, 2, end_time.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, message.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, static_cast<int>(RunStatus::Running));

    if (sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db) > 0) {
        log_msg(log_dir_, "recovery: осиротевших запусков помечено Aborted: "
            + std::to_string(sqlite3_changes(db)));
    }
    sqlite3_finalize(stmt);
}

std::wstring Scheduler::run_now(const std::wstring& task_id, std::wstring& error) {
    return run_now(task_id, nullptr, true, error);
}

std::wstring Scheduler::run_now(const std::wstring& task_id, HANDLE client_token,
    bool caller_is_admin, std::wstring& error) {
    auto task = db_.get_task(task_id);
    if (!task.has_value()) {
        error = L"Задача не найдена";
        return {};
    }

    const std::wstring history_id = request_start(task.value(), L"", L"", TriggeredBy::Manual, 0,
        client_token, caller_is_admin);
    if (history_id.empty()) error = L"Запуск отклонён (политика наложения или ошибка запуска)";
    return history_id;
}

bool Scheduler::stop_task(const std::wstring& task_id, std::wstring& error) {
    std::vector<RunningProcess> targets;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        for (const auto& [history_id, run] : running_processes_) {
            if (run.task_id == task_id) targets.push_back(run);
        }
    }

    if (targets.empty()) {
        error = L"Задача не выполняется";
        return false;
    }

    // При StartAnother у задачи может быть несколько процессов — завершаем все.
    for (auto& run : targets) {
        runner_->terminate(run.handle);
        finish_run(run, RunStatus::Killed, std::optional<int>(1), L"Остановлено пользователем");
    }
    return true;
}

void Scheduler::launch_task(const TaskDefinition& task) {
    request_start(task, L"", L"", TriggeredBy::Manual, 0, nullptr, true);
}

bool Scheduler::task_is_running(const std::wstring& task_id) const {
    std::lock_guard<std::mutex> guard(mutex_);
    for (const auto& [history_id, run] : running_processes_) {
        if (run.task_id == task_id) return true;
    }
    return false;
}
