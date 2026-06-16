#include "ipc.h"
#include "json.h"
#include <sstream>
#include <iomanip>
#include <chrono>
#include <cwctype>

// ---- JSON helpers ----

namespace json_helpers {

static json::Value opt_str(const std::optional<std::wstring>& v) {
    if (v.has_value()) return json::Value(ws2u(v.value()));
    return json::Value(nullptr);
}

static json::Value opt_int(const std::optional<int>& v) {
    if (v.has_value()) return json::Value(v.value());
    return json::Value(nullptr);
}

std::string task_to_json(const TaskDefinition& t) {
    json::Object obj;
    obj["Id"] = json::Value(ws2u(t.id));
    obj["Name"] = json::Value(ws2u(t.name));
    obj["Description"] = json::Value(ws2u(t.description));
    obj["Enabled"] = json::Value(t.enabled);
    obj["ProgramPath"] = json::Value(ws2u(t.program_path));
    obj["Arguments"] = json::Value(ws2u(t.arguments));
    obj["WorkingDirectory"] = json::Value(ws2u(t.working_directory));
    switch (t.window_style) {
        case WindowStyle::Normal: obj["WindowStyle"] = json::Value("Normal"); break;
        case WindowStyle::Hidden: obj["WindowStyle"] = json::Value("Hidden"); break;
        case WindowStyle::Minimized: obj["WindowStyle"] = json::Value("Minimized"); break;
        case WindowStyle::Maximized: obj["WindowStyle"] = json::Value("Maximized"); break;
        default: obj["WindowStyle"] = json::Value("Normal");
    }
    switch (t.on_error) {
        case OnErrorAction::SkipNext: obj["OnError"] = json::Value("SkipNext"); break;
        case OnErrorAction::Retry: obj["OnError"] = json::Value("Retry"); break;
        case OnErrorAction::Fail: obj["OnError"] = json::Value("Fail"); break;
        default: obj["OnError"] = json::Value("SkipNext");
    }
    obj["RetryCount"] = json::Value(t.retry_count);
    switch (t.on_overlap) {
        case OnOverlapAction::WaitComplete: obj["OnOverlap"] = json::Value("WaitComplete"); break;
        case OnOverlapAction::WaitNext: obj["OnOverlap"] = json::Value("WaitNext"); break;
        case OnOverlapAction::StartAnother: obj["OnOverlap"] = json::Value("StartAnother"); break;
        default: obj["OnOverlap"] = json::Value("WaitComplete");
    }
    obj["TimeoutMinutes"] = opt_int(t.timeout_minutes);
    obj["KillOnTimeout"] = json::Value(t.kill_on_timeout);
    obj["LogOutput"] = json::Value(t.log_output);
    obj["MaxHistoryRecords"] = json::Value(t.max_history_records);
    obj["CreatedAt"] = json::Value(ws2u(t.created_at));
    obj["ModifiedAt"] = json::Value(ws2u(t.modified_at));

    if (t.run_as_user.has_value()) {
        obj["RunAsUser"] = json::Value(ws2u(t.run_as_user->user_name));
        obj["RunAsDomain"] = json::Value(ws2u(t.run_as_user->domain));
        obj["RunAsPasswordEncrypted"] = json::Value(ws2u(t.run_as_user->encrypted_password));
    }

    return json::serialize(obj);
}

std::string task_list_to_json(const std::vector<TaskDefinition>& tasks) {
    json::Array arr;
    for (auto& t : tasks) {
        auto json_str = task_to_json(t);
        arr.push_back(json::parse(json_str));
    }
    return json::serialize(arr);
}

std::string time_window_to_json(const TimeWindow& w) {
    json::Object obj;
    obj["Id"] = json::Value(ws2u(w.id));
    obj["TaskId"] = json::Value(ws2u(w.task_id));
    obj["Type"] = json::Value(ws2u(w.type == TimeWindowType::ExactTimes ? L"ExactTimes" : L"Interval"));
    obj["StartDate"] = opt_str(w.start_date);
    obj["EndDate"] = opt_str(w.end_date);
    obj["StartTime"] = opt_str(w.start_time);
    obj["EndTime"] = opt_str(w.end_time);
    obj["RepeatIntervalMinutes"] = opt_int(w.repeat_interval_minutes);
    obj["RepeatUntil"] = opt_str(w.repeat_until);

    json::Array dow;
    for (int d : w.days_of_week) {
        switch (d) {
            case 0: dow.push_back(json::Value("Sunday")); break;
            case 1: dow.push_back(json::Value("Monday")); break;
            case 2: dow.push_back(json::Value("Tuesday")); break;
            case 3: dow.push_back(json::Value("Wednesday")); break;
            case 4: dow.push_back(json::Value("Thursday")); break;
            case 5: dow.push_back(json::Value("Friday")); break;
            case 6: dow.push_back(json::Value("Saturday")); break;
            default: dow.push_back(json::Value(d));
        }
    }
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
    obj["TaskCount"] = json::Value(s.task_count);
    obj["RunningTaskCount"] = json::Value(s.running_task_count);
    obj["StartedAt"] = json::Value(ws2u(s.started_at));
    return json::serialize(obj);
}

std::optional<TaskDefinition> task_from_json(const std::string& json_str) {
    auto v = json::parse(json_str);
    if (!v.is_object()) return std::nullopt;
    auto& obj = v.as_object();

    TaskDefinition t;
    auto get_str = [&](const std::string& key) -> std::wstring {
        auto* val = v.get(key);
        if (val && val->is_string()) return u2ws(val->as_string());
        return {};
    };

    t.id = get_str("Id");
    if (t.id.empty()) t.id = u2ws("");
    t.name = get_str("Name");
    t.description = get_str("Description");
    auto* enabled_val = v.get("Enabled");
    if (enabled_val && enabled_val->is_bool()) t.enabled = enabled_val->as_bool();
    t.program_path = get_str("ProgramPath");
    t.arguments = get_str("Arguments");
    t.working_directory = get_str("WorkingDirectory");

    auto parse_enum_int = [&](const std::string& key, int& out, const std::vector<std::pair<std::string, int>>& string_map) {
        auto* val = v.get(key);
        if (!val) return;
        if (val->is_number()) { out = val->as_int(); return; }
        if (val->is_string()) {
            auto s = val->as_string();
            for (auto& [str, id] : string_map) {
                if (s == str) { out = id; return; }
            }
        }
    };
    int ws_int = 0, oe_int = 0, oo_int = 0;
    parse_enum_int("WindowStyle", ws_int, {{"Normal",0},{"Hidden",1},{"Minimized",2},{"Maximized",3}});
    parse_enum_int("OnError", oe_int, {{"SkipNext",0},{"Retry",1},{"Fail",2}});
    parse_enum_int("OnOverlap", oo_int, {{"WaitComplete",0},{"WaitNext",1},{"StartAnother",2}});
    t.window_style = static_cast<WindowStyle>(ws_int);
    t.on_error = static_cast<OnErrorAction>(oe_int);
    t.on_overlap = static_cast<OnOverlapAction>(oo_int);

    auto* rc_val = v.get("RetryCount");
    if (rc_val && rc_val->is_number()) t.retry_count = rc_val->as_int();
    auto* tm_val = v.get("TimeoutMinutes");
    if (tm_val && !tm_val->is_null()) t.timeout_minutes = tm_val->as_int();

    // RunAsUser
    auto* rau = v.get("RunAsUser");
    if (rau && rau->is_string() && !rau->as_string().empty()) {
        RunAsCredentials cred;
        cred.user_name = u2ws(rau->as_string());
        auto* rad = v.get("RunAsDomain");
        cred.domain = rad && rad->is_string() ? u2ws(rad->as_string()) : L"";
        auto* rap = v.get("RunAsPasswordEncrypted");
        cred.encrypted_password = rap && rap->is_string() ? u2ws(rap->as_string()) : L"";
        t.run_as_user = cred;
    }

    return t;
}

std::vector<TimeWindow> time_windows_from_json(const std::string& json_str) {
    std::vector<TimeWindow> result;
    auto v = json::parse(json_str);
    if (!v.is_array()) return result;
    for (auto& elem : v.as_array()) {
        if (!elem.is_object()) continue;
        TimeWindow tw;
        auto get_str = [&](const std::string& key) -> std::wstring {
            auto* val = elem.get(key);
            if (val && val->is_string()) return u2ws(val->as_string());
            return {};
        };
        tw.id = get_str("Id");
        // Type: string ("ExactTimes"/"Interval") or int (0/1)
        auto* type_val = elem.get("Type");
        if (type_val) {
            if (type_val->is_string())
                tw.type = type_val->as_string() == "Interval" ? TimeWindowType::Interval : TimeWindowType::ExactTimes;
            else if (type_val->is_number())
                tw.type = static_cast<TimeWindowType>(type_val->as_int());
        }
        auto* sd = elem.get("StartDate");
        if (sd && sd->is_string() && !sd->as_string().empty()) tw.start_date = u2ws(sd->as_string());
        auto* ed = elem.get("EndDate");
        if (ed && ed->is_string() && !ed->as_string().empty()) tw.end_date = u2ws(ed->as_string());
        auto* st = elem.get("StartTime");
        if (st && st->is_string() && !st->as_string().empty()) tw.start_time = u2ws(st->as_string());
        auto* et = elem.get("EndTime");
        if (et && et->is_string() && !et->as_string().empty()) tw.end_time = u2ws(et->as_string());
        auto* ri = elem.get("RepeatIntervalMinutes");
        if (ri && ri->is_number()) tw.repeat_interval_minutes = ri->as_int();
        auto* ru = elem.get("RepeatUntil");
        if (ru && ru->is_string() && !ru->as_string().empty()) tw.repeat_until = u2ws(ru->as_string());

        auto parse_str_arr = [&](const std::string& key, auto& vec) {
            auto* arr = elem.get(key);
            if (arr && arr->is_array()) {
                for (auto& item : arr->as_array())
                    if (item.is_string()) vec.push_back(u2ws(item.as_string()));
            }
        };
        auto parse_int_arr = [&](const std::string& key, auto& vec) {
            auto* arr = elem.get(key);
            if (arr && arr->is_array()) {
                for (auto& item : arr->as_array())
                    if (item.is_number()) vec.push_back(item.as_int());
            }
        };

        // DaysOfWeek from C#: ints or strings ("Sunday"=0.."Saturday"=6)
        {
            auto* arr = elem.get("DaysOfWeek");
            if (arr && arr->is_array()) {
                for (auto& item : arr->as_array()) {
                    if (item.is_number()) tw.days_of_week.push_back(item.as_int());
                    else if (item.is_string()) {
                        static const std::pair<const char*, int> dow_map[] = {
                            {"Sunday",0},{"Monday",1},{"Tuesday",2},{"Wednesday",3},
                            {"Thursday",4},{"Friday",5},{"Saturday",6}
                        };
                        auto s = item.as_string();
                        for (auto& m : dow_map) {
                            if (s == m.first) { tw.days_of_week.push_back(m.second); break; }
                        }
                    }
                }
            }
        }
        // DaysOfMonth: always ints
        parse_int_arr("DaysOfMonth", tw.days_of_month);
        parse_str_arr("SpecificDates", tw.specific_dates);
        parse_str_arr("ExactTimes", tw.exact_times);

        result.push_back(std::move(tw));
    }
    return result;
}

std::optional<IpcRequest> request_from_json(const std::string& json_str) {
    auto v = json::parse(json_str);
    if (!v.is_object()) return std::nullopt;
    IpcRequest req;
    auto* action = v.get("Action");
    if (action && action->is_string()) req.action = u2ws(action->as_string());
    auto* payload = v.get("Payload");
    if (payload && payload->is_string()) req.payload = u2ws(payload->as_string());
    return req;
}

std::string response_to_json(const IpcResponse& resp) {
    json::Object obj;
    obj["Success"] = json::Value(resp.success);
    obj["Error"] = json::Value(ws2u(resp.error));
    obj["Payload"] = json::Value(ws2u(resp.payload));
    return json::serialize(obj);
}

} // namespace json_helpers

// ---- IpcServer ----

IpcServer::IpcServer(Database& db, RequestHandler handler)
    : db_(db), scheduler_handler_(std::move(handler)) {}

IpcServer::~IpcServer() { stop(); }

static IpcResponse ok(const std::wstring& payload = L"") {
    return IpcResponse{ true, L"", payload };
}
static IpcResponse fail(const std::wstring& error) {
    return IpcResponse{ false, error, L"" };
}

void IpcServer::start() {
    if (running_) return;
    running_ = true;
    stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    thread_ = std::thread(&IpcServer::worker_thread, this);
}

void IpcServer::stop() {
    running_ = false;
    if (stop_event_) SetEvent(stop_event_);
    if (thread_.joinable()) thread_.join();
    if (stop_event_) { CloseHandle(stop_event_); stop_event_ = nullptr; }
}

void IpcServer::worker_thread() {
    while (running_) {
        HANDLE pipe = CreateNamedPipeW(
            PIPE_NAME,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            PIPE_BUFFER_SIZE, PIPE_BUFFER_SIZE,
            0, nullptr);

        if (pipe == INVALID_HANDLE_VALUE) {
            Sleep(100);
            continue;
        }

        OVERLAPPED ov = {};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

        BOOL connected = ConnectNamedPipe(pipe, &ov);
        if (!connected && GetLastError() != ERROR_IO_PENDING) {
            CloseHandle(pipe);
            CloseHandle(ov.hEvent);
            continue;
        }

        HANDLE events[2] = { ov.hEvent, stop_event_ };
        DWORD wait = WaitForMultipleObjects(2, events, FALSE, INFINITE);

        if (wait == WAIT_OBJECT_0 + 1 || !running_) {
            CancelIo(pipe);
            CloseHandle(pipe);
            CloseHandle(ov.hEvent);
            break;
        }

        // Handle client in a separate thread, immediately listen for next client
        std::thread([this, pipe]() {
            handle_client(pipe);
        }).detach();

        CloseHandle(ov.hEvent);
    }
}

void IpcServer::handle_client_core(HANDLE pipe) {
    char buf[PIPE_BUFFER_SIZE];
    DWORD total = 0;
    DWORD bytes_read = 0;
    BOOL more = TRUE;

    while (more && total < PIPE_BUFFER_SIZE) {
        if (ReadFile(pipe, buf + total, PIPE_BUFFER_SIZE - total, &bytes_read, NULL)) {
            total += bytes_read;
            more = FALSE;
        } else if (GetLastError() == ERROR_MORE_DATA) {
            total += bytes_read;
        } else {
            goto cleanup;
        }
    }

    if (total == 0) goto cleanup;

    {
        std::string request_str(buf, total);
        auto req = json_helpers::request_from_json(request_str);
        if (!req.has_value()) {
            auto resp = IpcResponse{ false, L"Invalid request", L"" };
            auto resp_str = json_helpers::response_to_json(resp);
            DWORD written;
            WriteFile(pipe, resp_str.data(), (DWORD)resp_str.size(), &written, nullptr);
            goto cleanup;
        }

        IpcResponse resp;
        if (req->action == L"GetStatus") {
            resp = scheduler_handler_(req.value());
        } else {
            resp = process_request(req.value());
        }

        auto resp_str = json_helpers::response_to_json(resp);
        DWORD written;
        WriteFile(pipe, resp_str.data(), (DWORD)resp_str.size(), &written, nullptr);
    }

cleanup:
    FlushFileBuffers(pipe);
    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
}

void IpcServer::handle_client(HANDLE pipe) {
    __try {
        handle_client_core(pipe);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        CloseHandle(pipe);
    }
}

IpcResponse IpcServer::process_request(const IpcRequest& req) {
    try {
        if (req.action == L"ListTasks") return handle_list_tasks();
        if (req.action == L"GetTask") return handle_get_task(req.payload);
        if (req.action == L"GetTimeWindows") return handle_get_time_windows(req.payload);
        if (req.action == L"GetHistory") return handle_get_history(req.payload);
        if (req.action == L"CreateTask") return handle_create_task(req.payload);
        if (req.action == L"UpdateTask") return handle_update_task(req.payload);
        if (req.action == L"DeleteTask") return handle_delete_task(req.payload);
        if (req.action == L"EnableTask") return handle_toggle_task(req.payload, true);
        if (req.action == L"DisableTask") return handle_toggle_task(req.payload, false);
        return fail(L"Unknown action: " + req.action);
    } catch (const std::exception& e) {
        return fail(u2ws(e.what()));
    }
}

IpcResponse IpcServer::handle_list_tasks() {
    auto tasks = db_.list_tasks();
    return ok(u2ws(json_helpers::task_list_to_json(tasks)));
}

IpcResponse IpcServer::handle_get_task(const std::wstring& payload) {
    auto task = db_.get_task(payload);
    if (!task.has_value()) return fail(L"Task not found");
    return ok(u2ws(json_helpers::task_to_json(task.value())));
}

IpcResponse IpcServer::handle_get_time_windows(const std::wstring& payload) {
    auto windows = db_.get_time_windows(payload);
    return ok(u2ws(json_helpers::time_window_list_to_json(windows)));
}

IpcResponse IpcServer::handle_get_history(const std::wstring& payload) {
    auto history = db_.get_history(payload);
    return ok(u2ws(json_helpers::history_list_to_json(history)));
}

IpcResponse IpcServer::handle_create_task(const std::wstring& payload) {
    auto task = json_helpers::task_from_json(ws2u(payload));
    if (!task.has_value()) return fail(L"Invalid task data");

    // Generate ID if empty or all-zero GUID
    if (task->id.empty() || task->id == L"00000000-0000-0000-0000-000000000000") {
            GUID guid;
            UuidCreate(&guid);
            wchar_t buf[64];
            StringFromGUID2(guid, buf, 64);
            task->id = buf + 1;
            task->id.pop_back();
            for (auto& c : task->id) c = towlower(c);
    }

    // Parse and save time windows from the DTO
    auto payload_obj = json::parse(ws2u(payload));
    auto* tws = payload_obj.get("TimeWindows");
    std::vector<TimeWindow> windows;
    if (tws && tws->is_array()) {
        auto tw_json = json::serialize(*tws);
        windows = json_helpers::time_windows_from_json(tw_json);
    }

    auto now = std::chrono::system_clock::now();
    auto now_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_s(&tm, &now_t);
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    task->created_at = u2ws(ss.str());

    db_.insert_task(task.value());
        for (auto& w : windows) {
            w.task_id = task->id;
            if (w.id.empty() || w.id == L"00000000-0000-0000-0000-000000000000") {
                GUID guid;
                UuidCreate(&guid);
                wchar_t buf[64];
                StringFromGUID2(guid, buf, 64);
                w.id = buf + 1;
                w.id.pop_back();
                for (auto& c : w.id) c = towlower(c);
            }
            db_.insert_time_window(w);
        }
        return ok(L"");
}

IpcResponse IpcServer::handle_update_task(const std::wstring& payload) {
    auto task = json_helpers::task_from_json(ws2u(payload));
    if (!task.has_value()) return fail(L"Invalid task data");

    auto now = std::chrono::system_clock::now();
    auto now_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_s(&tm, &now_t);
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    task->modified_at = u2ws(ss.str());

    db_.update_task(task.value());

    // Re-save time windows
    auto payload_obj = json::parse(ws2u(payload));
    auto* tws = payload_obj.get("TimeWindows");
    if (tws && tws->is_array()) {
        db_.delete_time_windows(task->id);
        auto tw_json = json::serialize(*tws);
        auto windows = json_helpers::time_windows_from_json(tw_json);
        for (auto& w : windows) {
            w.task_id = task->id;
            if (w.id.empty() || w.id == L"00000000-0000-0000-0000-000000000000") {
                GUID guid;
                UuidCreate(&guid);
                wchar_t buf[64];
                StringFromGUID2(guid, buf, 64);
                w.id = buf + 1;
                w.id.pop_back();
                for (auto& c : w.id) c = towlower(c);
            }
            db_.insert_time_window(w);
        }
    }
    return ok(L"");
}

IpcResponse IpcServer::handle_delete_task(const std::wstring& payload) {
    db_.delete_task(payload);
    return ok(L"");
}

IpcResponse IpcServer::handle_toggle_task(const std::wstring& payload, bool enabled) {
    db_.set_task_enabled(payload, enabled);
    return ok(L"");
}
