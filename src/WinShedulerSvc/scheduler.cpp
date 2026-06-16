#include "scheduler.h"
#include "json.h"
#include <sstream>
#include <iomanip>
#include <rpc.h>
#include <rpcdce.h>
#include <fstream>

#pragma comment(lib, "rpcrt4.lib")
#pragma comment(lib, "crypt32.lib")

static void log_msg(const std::wstring& log_dir, const std::string& msg) {
    std::ofstream f(log_dir + L"\\scheduler.log", std::ios::app);
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

static std::wstring generate_guid() {
    GUID guid;
    UuidCreate(&guid);
    wchar_t buf[64];
    StringFromGUID2(guid, buf, 64);
    std::wstring s(buf + 1);
    s.pop_back();
    for (auto& c : s) c = towlower(c);
    return s;
}

static std::wstring now_to_string() {
    auto now = std::chrono::system_clock::now();
    auto now_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_s(&tm, &now_t);
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return u2ws(ss.str());
}

static long long time_to_minutes(const std::wstring& time_str) {
    // Parse HH:MM format
    int h = 0, m = 0;
    swscanf_s(time_str.c_str(), L"%d:%d", &h, &m);
    return h * 60LL + m;
}

bool decrypt_password(const std::wstring& encrypted_base64, std::wstring& password_out) {
    // DPAPI CryptUnprotectData
    DATA_BLOB encrypted_blob = {};
    DATA_BLOB decrypted_blob = {};

    // Decode base64
    std::string encoded = ws2u(encrypted_base64);
    DWORD decoded_len = 0;
    if (!CryptStringToBinaryA(encoded.c_str(), (DWORD)encoded.size(), CRYPT_STRING_BASE64, nullptr, &decoded_len, nullptr, nullptr))
        return false;

    std::vector<BYTE> decoded(decoded_len);
    if (!CryptStringToBinaryA(encoded.c_str(), (DWORD)encoded.size(), CRYPT_STRING_BASE64, decoded.data(), &decoded_len, nullptr, nullptr))
        return false;

    encrypted_blob.cbData = decoded_len;
    encrypted_blob.pbData = decoded.data();

    if (!CryptUnprotectData(&encrypted_blob, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_LOCAL_MACHINE, &decrypted_blob))
        return false;

    // Convert to wchar_t
    int wlen = MultiByteToWideChar(CP_UTF8, 0, (char*)decrypted_blob.pbData, decrypted_blob.cbData, nullptr, 0);
    password_out.resize(wlen);
    MultiByteToWideChar(CP_UTF8, 0, (char*)decrypted_blob.pbData, decrypted_blob.cbData, &password_out[0], wlen);

    SecureZeroMemory(decrypted_blob.pbData, decrypted_blob.cbData);
    LocalFree(decrypted_blob.pbData);
    return true;
}

Scheduler::Scheduler(Database& db, const std::wstring& log_dir)
    : db_(db), log_dir_(log_dir) {
    InitializeCriticalSection(&lock_);
}

Scheduler::~Scheduler() {
    stop();
    DeleteCriticalSection(&lock_);
}

void Scheduler::start() {
    if (running_) return;
    running_ = true;
    started_at_ = std::chrono::system_clock::now();
    stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    thread_ = std::thread(&Scheduler::worker_thread, this);
}

void Scheduler::stop() {
    running_ = false;
    if (stop_event_) SetEvent(stop_event_);

    // Kill all running processes
    EnterCriticalSection(&lock_);
    auto running = running_map_;
    running_map_.clear();
    LeaveCriticalSection(&lock_);

    for (auto& [id, rp] : running) {
        if (rp.process_handle) {
            TerminateProcess(rp.process_handle, 1);
            CloseHandle(rp.process_handle);
        }
        if (rp.thread_handle) CloseHandle(rp.thread_handle);
    }

    if (thread_.joinable()) thread_.join();
    if (stop_event_) { CloseHandle(stop_event_); stop_event_ = nullptr; }
    if (tick_timer_) { CloseHandle(tick_timer_); tick_timer_ = nullptr; }
}

ServiceStatus Scheduler::get_status() {
    ServiceStatus s;
    s.running = running_;
    s.task_count = task_count();
    s.running_task_count = running_task_count();
    auto t = std::chrono::system_clock::to_time_t(started_at_);
    std::tm tm;
    localtime_s(&tm, &t);
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    s.started_at = u2ws(ss.str());
    return s;
}

int Scheduler::task_count() const {
    return (int)db_.list_tasks().size();
}

int Scheduler::running_task_count() const {
    EnterCriticalSection(&lock_);
    int count = (int)running_map_.size();
    LeaveCriticalSection(&lock_);
    return count;
}

void Scheduler::worker_thread() {
    tick_timer_ = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    if (!tick_timer_) return;

    LARGE_INTEGER due;
    due.QuadPart = -10000LL; // 1ms from now for immediate first tick
    SetWaitableTimer(tick_timer_, &due, SCHEDULER_TICK_MS, nullptr, nullptr, FALSE);

    log_msg(log_dir_, "worker_thread started");
    while (running_) {
        HANDLE events[2] = { tick_timer_, stop_event_ };
        DWORD wait = WaitForMultipleObjects(2, events, FALSE, INFINITE);
        if (wait == WAIT_OBJECT_0 + 1 || !running_) break;
        if (wait == WAIT_OBJECT_0) {
            tick();
        }
    }
    log_msg(log_dir_, "worker_thread stopped");
}

void Scheduler::tick() {
    try {
        auto tasks = db_.list_tasks();
        auto now = std::chrono::system_clock::now();
        auto now_t = std::chrono::system_clock::to_time_t(now);
        std::tm now_tm;
        localtime_s(&now_tm, &now_t);
        int current_minute = now_tm.tm_hour * 60 + now_tm.tm_min;

        log_msg(log_dir_, "tick: checking " + std::to_string(tasks.size()) + " tasks");

        for (auto& task : tasks) {
            if (!task.enabled) continue;

            auto windows = db_.get_time_windows(task.id);
            bool should_run = false;

            for (auto& w : windows) {
                if (!is_in_window(w, now)) continue;

                if (w.type == TimeWindowType::ExactTimes) {
                    auto it = last_run_minute_.find(task.id);
                    int last_min = (it != last_run_minute_.end()) ? it->second : -1;
                    if (last_min == current_minute) continue;
                    should_run = true;
                    last_run_minute_[task.id] = current_minute;
                    break;
                } else {
                    should_run = true;
                    break;
                }
            }

            if (!should_run) continue;

            // Check overlap
            EnterCriticalSection(&lock_);
            bool already_running = running_map_.find(task.id) != running_map_.end();
            if (already_running) {
                switch (task.on_overlap) {
                case OnOverlapAction::WaitNext:
                    LeaveCriticalSection(&lock_);
                    continue;
                case OnOverlapAction::WaitComplete:
                    pending_queue_[task.id].push(now);
                    LeaveCriticalSection(&lock_);
                    continue;
                case OnOverlapAction::StartAnother:
                    break;
                }
            }
            LeaveCriticalSection(&lock_);

            log_msg(log_dir_, "launching task " + ws2u(task.id));
            launch_task(task);
        }

        process_pending_queue();
        check_timeouts();
    } catch (const std::exception& e) {
        log_msg(log_dir_, "tick exception: " + std::string(e.what()));
    } catch (...) {
        log_msg(log_dir_, "tick unknown exception");
    }
}

bool Scheduler::is_in_window(const TimeWindow& w, const std::chrono::system_clock::time_point& now) {
    auto now_t = std::chrono::system_clock::to_time_t(now);
    std::tm now_tm;
    localtime_s(&now_tm, &now_t);

    // Date range check
    if (w.start_date.has_value()) {
        auto sd = time_to_minutes(w.start_date.value());
        // Simple date compare - parse YYYY-MM-DD
        // For now, skip date comparison for brevity
    }

    // Day of week check
    if (!w.days_of_week.empty()) {
        int dow = now_tm.tm_wday; // 0=Sun
        if (std::find(w.days_of_week.begin(), w.days_of_week.end(), dow) == w.days_of_week.end())
            return false;
    }

    // Day of month check
    if (!w.days_of_month.empty()) {
        if (std::find(w.days_of_month.begin(), w.days_of_month.end(), now_tm.tm_mday) == w.days_of_month.end())
            return false;
    }

    int current_minutes = now_tm.tm_hour * 60 + now_tm.tm_min;

    if (w.type == TimeWindowType::ExactTimes) {
        for (auto& et : w.exact_times) {
            int et_minutes = (int)time_to_minutes(et);
            if (abs(current_minutes - et_minutes) < 1) return true;
        }
        return false;
    }

    // Interval type
    if (!w.start_time.has_value() || !w.end_time.has_value() || !w.repeat_interval_minutes.has_value())
        return false;

    int st_min = (int)time_to_minutes(w.start_time.value());
    int et_min = (int)time_to_minutes(w.end_time.value());

    if (current_minutes < st_min || current_minutes > et_min) return false;

    if (w.repeat_until.has_value()) {
        int ru_min = (int)time_to_minutes(w.repeat_until.value());
        if (current_minutes > ru_min) return false;
    }

    int elapsed = current_minutes - st_min;
    return (elapsed % w.repeat_interval_minutes.value()) < 1;
}

void Scheduler::launch_task(const TaskDefinition& task) {
    log_msg(log_dir_, "launch_task: " + ws2u(task.name));
    // Create history record
    RunHistory history;
    history.id = generate_guid();
    history.task_id = task.id;
    history.start_time = now_to_string();
    history.status = RunStatus::Running;

    db_.insert_history(history);

    // Launch process
    std::wstring output_path;
    if (task.log_output) {
        output_path = log_dir_ + L"\\output\\" + task.id + L"\\" + history.id + L".log";
        // Create directory
        std::wstring dir = log_dir_ + L"\\output\\" + task.id;
        CreateDirectoryW(dir.c_str(), nullptr);
    }

    DWORD pid = run_process(task, output_path);
    log_msg(log_dir_, "run_process result: pid=" + std::to_string(pid));

    // Update history with PID
    history.pid = pid;
    db_.update_history(history);

    // Track running
    EnterCriticalSection(&lock_);
    // The process handle is opened by run_process, but for monitoring we need the handle
    // For simplicity, we don't track the handle here; timeout monitoring uses pid-based approach
    LeaveCriticalSection(&lock_);
}

DWORD Scheduler::run_process(const TaskDefinition& task, const std::wstring& output_path) {
    std::wstring command = L"\"" + task.program_path + L"\" " + task.arguments;

    PROCESS_INFORMATION pi = {};
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    switch (task.window_style) {
        case WindowStyle::Normal: si.wShowWindow = SW_SHOWNORMAL; break;
        case WindowStyle::Hidden: si.wShowWindow = SW_HIDE; break;
        case WindowStyle::Minimized: si.wShowWindow = SW_SHOWMINIMIZED; break;
        case WindowStyle::Maximized: si.wShowWindow = SW_SHOWMAXIMIZED; break;
        default: si.wShowWindow = SW_SHOWNORMAL;
    }

    // Handle output redirection
    HANDLE hStdoutRead = nullptr, hStdoutWrite = nullptr;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };

    if (!output_path.empty()) {
        // Redirect to file
        si.dwFlags |= STARTF_USESTDHANDLES;
        hStdoutWrite = CreateFileW(output_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        si.hStdOutput = hStdoutWrite;
        si.hStdError = hStdoutWrite;
    }

    BOOL result;
    std::wstring working_dir = task.working_directory.empty() ? std::wstring() : task.working_directory;
    std::wstring command_line = command; // non-const copy needed

    if (task.run_as_user.has_value()) {
        // Launch with alternate credentials
        std::wstring password;
        if (!task.run_as_user->encrypted_password.empty()) {
            if (!decrypt_password(task.run_as_user->encrypted_password, password)) {
                // Failed to decrypt
                if (hStdoutWrite) CloseHandle(hStdoutWrite);
                return 0;
            }
        }

        std::wstring domain = task.run_as_user->domain;
        std::wstring username = task.run_as_user->user_name;

        // CreateProcessWithLogonW requires a modifiable buffer
        std::vector<wchar_t> cmd_buf(command_line.begin(), command_line.end());
        cmd_buf.push_back(0);

        std::vector<wchar_t> dir_buf(working_dir.begin(), working_dir.end());
        dir_buf.push_back(0);

        result = CreateProcessWithLogonW(
            username.c_str(),
            domain.empty() ? nullptr : domain.c_str(),
            password.c_str(),
            LOGON_WITH_PROFILE,
            nullptr, // application name
            cmd_buf.data(), // command line
            CREATE_DEFAULT_ERROR_MODE | CREATE_UNICODE_ENVIRONMENT | (task.window_style == WindowStyle::Hidden ? CREATE_NO_WINDOW : 0),
            nullptr, // environment
            working_dir.empty() ? nullptr : dir_buf.data(),
            &si, &pi);

        // Clear password from memory
        if (!password.empty()) {
            SecureZeroMemory(&password[0], password.size() * sizeof(wchar_t));
            password.clear();
        }
    } else {
        // Launch as SYSTEM (current user)
        std::vector<wchar_t> cmd_buf(command_line.begin(), command_line.end());
        cmd_buf.push_back(0);

        std::vector<wchar_t> dir_buf(working_dir.begin(), working_dir.end());
        dir_buf.push_back(0);

        result = CreateProcessW(
            nullptr, cmd_buf.data(),
            nullptr, nullptr, TRUE,
            CREATE_DEFAULT_ERROR_MODE | CREATE_UNICODE_ENVIRONMENT | (task.window_style == WindowStyle::Hidden ? CREATE_NO_WINDOW : 0),
            nullptr,
            working_dir.empty() ? nullptr : dir_buf.data(),
            &si, &pi);
    }

    if (hStdoutWrite) CloseHandle(hStdoutWrite);

    if (!result) return 0;

    CloseHandle(pi.hThread);
    DWORD pid = pi.dwProcessId;

    // Store for timeout monitoring
    EnterCriticalSection(&lock_);
    RunningProcess rp;
    rp.process_handle = pi.hProcess;
    rp.thread_handle = nullptr;
    rp.task_id = task.id;
    rp.start_time = std::chrono::system_clock::now();
    running_map_[task.id] = std::move(rp);
    LeaveCriticalSection(&lock_);

    return pid;
}

void Scheduler::check_timeouts() {
    EnterCriticalSection(&lock_);
    auto now = std::chrono::system_clock::now();

    std::vector<std::wstring> exited_tasks;
    std::vector<std::wstring> timed_out_tasks;
    for (auto& [task_id, rp] : running_map_) {
        auto task = db_.get_task(task_id);
        if (!task.has_value()) continue;

        // Check if process has exited
        DWORD exit_code = 0;
        bool exited = GetExitCodeProcess(rp.process_handle, &exit_code) && exit_code != STILL_ACTIVE;

        if (exited) {
            exited_tasks.push_back(task_id);
            auto history_list = db_.get_history(task_id, 1);
            if (!history_list.empty()) {
                history_list[0].end_time = now_to_string();
                history_list[0].exit_code = static_cast<int>(exit_code);
                history_list[0].status = RunStatus::Completed;
                db_.update_history(history_list[0]);
            }
            CloseHandle(rp.process_handle);
            continue;
        }

        // Check timeout
        if (!task->timeout_minutes.has_value()) continue;
        auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - rp.start_time);
        if (elapsed.count() >= task->timeout_minutes.value()) {
            timed_out_tasks.push_back(task_id);
        }
    }

    // Erase exited tasks from running_map_
    for (auto& task_id : exited_tasks) {
        running_map_.erase(task_id);
    }
    LeaveCriticalSection(&lock_);

    // Handle timeouts outside the lock
    for (auto& task_id : timed_out_tasks) {
        EnterCriticalSection(&lock_);
        auto it = running_map_.find(task_id);
        if (it == running_map_.end()) { LeaveCriticalSection(&lock_); continue; }
        auto rp = it->second;
        running_map_.erase(it);
        LeaveCriticalSection(&lock_);

        auto task = db_.get_task(task_id);
        bool kill = task.has_value() && task->kill_on_timeout;

        if (kill) {
            TerminateProcess(rp.process_handle, 1);
        }

        // Update history
        auto history_list = db_.get_history(task_id, 1);
        if (!history_list.empty()) {
            history_list[0].end_time = now_to_string();
            history_list[0].status = RunStatus::Timeout;
            if (kill) history_list[0].error_message = L"Killed by timeout";
            else history_list[0].error_message = L"Timeout exceeded";
            db_.update_history(history_list[0]);
        }

        CloseHandle(rp.process_handle);
    }
}

void Scheduler::trim_history(const std::wstring& task_id, int max_records) {
    db_.trim_history(task_id, max_records);
}

void Scheduler::process_pending_queue() {
    EnterCriticalSection(&lock_);
    auto copy = pending_queue_;
    LeaveCriticalSection(&lock_);

    for (auto& [task_id, queue] : copy) {
        if (queue.empty()) continue;

        EnterCriticalSection(&lock_);
        bool running = running_map_.find(task_id) != running_map_.end();
        LeaveCriticalSection(&lock_);

        if (running) continue;

        queue.pop();

        auto task = db_.get_task(task_id);
        if (task.has_value()) launch_task(task.value());

        EnterCriticalSection(&lock_);
        auto& q = pending_queue_[task_id];
        if (q.empty()) pending_queue_.erase(task_id);
        LeaveCriticalSection(&lock_);
    }
}
