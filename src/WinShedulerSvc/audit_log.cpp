#include "audit_log.h"
#include "auth_validate.h"
#include "config.h"
#include <fstream>
#include <mutex>
#include <ctime>
#include <sstream>
#include <iomanip>

namespace audit {

namespace {

// Сериализация записей: несколько IPC-потоков пишут одновременно.
std::mutex& audit_mutex() {
    static std::mutex m;
    return m;
}

std::string utc_now() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_s(&tm, &t);
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

#ifndef WINSHEDULER_DB_REPOSITORY
bool insert_db(sqlite3* db, const std::string& ts, const std::string& actor,
               const std::string& action, const std::string& object_type,
               const std::string& object_id, const std::string& result,
               const std::string& details) {
    if (db == nullptr) return false;
    const char* sql =
        "INSERT INTO audit_log (ts,actor_sid,action,object_type,object_id,result,details) "
        "VALUES (?,?,?,?,?,?,?)";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, ts.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, actor.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, action.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, object_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, object_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, result.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, details.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}
#endif

void append_file(const std::wstring& log_dir, const std::string& line) {
    if (log_dir.empty()) return;
    std::wstring path = log_dir + L"\\audit.log";
    std::ofstream f(path, std::ios::app);
    if (f) f << line << "\n";
}

} // namespace

const wchar_t* result_name(Result result) {
    switch (result) {
    case Result::Ok: return L"ok";
    case Result::Denied: return L"denied";
    case Result::Error: return L"error";
    }
    return L"error";
}

std::wstring redact(const std::wstring& details) {
    return auth::sanitize_line(details);
}

void write(const std::wstring& log_dir, Database& db,
           const std::wstring& actor_sid,
           const std::wstring& action,
           const std::wstring& object_type,
           const std::wstring& object_id,
           Result result,
           const std::wstring& details) {
    // Секреты и переводы строк не попадают в аудит.
    const std::wstring safe_details = redact(details);
    const std::string ts = utc_now();
    const std::string actor = ws2u(actor_sid);
    const std::string action_u8 = ws2u(action);
    const std::string object_type_u8 = ws2u(object_type);
    const std::string object_id_u8 = ws2u(object_id);
    const std::string result_u8 = ws2u(result_name(result));
    const std::string details_u8 = ws2u(safe_details);

    std::lock_guard<std::mutex> lock(audit_mutex());

#ifdef WINSHEDULER_DB_REPOSITORY
    // Репозиторий трека C: единый writer, параметризованный INSERT.
    try {
        AuditEntry entry;
        entry.ts = u2ws(ts);
        entry.actor_sid = actor_sid;
        entry.action = action;
        entry.object_type = object_type;
        if (!object_id.empty()) entry.object_id = object_id;
        entry.result = result_name(result);
        entry.details = safe_details;
        db.append_audit(entry);
        return;
    } catch (...) {
        // Ниже — файловый fallback: событие аудита не должно теряться.
    }
#else
    if (insert_db(db.get_db(), ts, actor, action_u8, object_type_u8, object_id_u8,
                  result_u8, details_u8)) {
        return;
    }
#endif

    // Таблицы audit_log ещё нет — пишем в файл, чтобы не потерять событие.
    std::ostringstream line;
    line << ts << " | " << actor << " | " << action_u8 << " | "
         << object_type_u8 << " | " << object_id_u8 << " | "
         << result_u8 << " | " << details_u8;
    append_file(log_dir, line.str());
}

} // namespace audit
