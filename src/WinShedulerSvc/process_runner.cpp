#define WINVER 0x0601
#define _WIN32_WINNT 0x0601
#include "process_runner.h"

#include <userenv.h>
#include <vector>

#pragma comment(lib, "userenv.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "crypt32.lib")

// Флаг DPAPI: расшифровать данные может только SYSTEM (см. SECURITY.md).
// В некоторых версиях SDK константа не объявлена в wincrypt.h.
#ifndef CRYPTPROTECT_SYSTEM
#define CRYPTPROTECT_SYSTEM 0x2
#endif

namespace {

constexpr wchar_t kSepForward = L'/';
constexpr wchar_t kSepBack = L'\\';

bool is_separator(wchar_t c) {
    return c == kSepForward || c == kSepBack;
}

// Проверка, что каталог существует.
bool directory_exists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool file_exists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

// Каталог программы, если working_directory не задан.
std::wstring parent_directory(const std::wstring& path) {
    const size_t position = path.find_last_of(L"/\\");
    if (position == std::wstring::npos) return {};
    if (position == 2) return path.substr(0, 3); // корень диска "d:/"
    return path.substr(0, position);
}

DWORD window_show_command(WindowStyle style) {
    switch (style) {
    case WindowStyle::Hidden: return SW_HIDE;
    case WindowStyle::Minimized: return SW_SHOWMINIMIZED;
    case WindowStyle::Maximized: return SW_SHOWMAXIMIZED;
    case WindowStyle::Normal:
    default: return SW_SHOWNORMAL;
    }
}

std::wstring format_win32_error(DWORD code) {
    wchar_t buffer[512] = {};
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, buffer, 512, nullptr);
    std::wstring message = length > 0 ? std::wstring(buffer, length) : L"неизвестная ошибка";
    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ')) {
        message.pop_back();
    }
    return message;
}

// Создаёт Job Object с флагом kill-on-close: закрытие job убивает всё дерево.
HANDLE create_kill_on_close_job() {
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job) return nullptr;

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        CloseHandle(job);
        return nullptr;
    }
    return job;
}

// Освобождение временных ресурсов RunAs.
void release_profile(HANDLE token, PROFILEINFOW& profile, bool profile_loaded) {
    if (profile_loaded) UnloadUserProfile(token, profile.hProfile);
    if (token) CloseHandle(token);
}

} // namespace

namespace process {

bool validate_local_path(const std::wstring& path, bool allow_empty, std::wstring& error) {
    if (path.empty()) {
        if (allow_empty) return true;
        error = L"Путь не задан";
        return false;
    }

    // Только локальный абсолютный путь вида "d:/..." или "d:\...".
    if (path.size() < 3 || !iswalpha(path[0]) || path[1] != L':') {
        error = L"Путь должен быть локальным абсолютным (например, d:/scripts/run.cmd)";
        return false;
    }
    if (!is_separator(path[2])) {
        error = L"После буквы диска ожидается разделитель пути";
        return false;
    }

    // Корень диска "d:/" допустим только для рабочего каталога (проверяется отдельно).
    bool has_segment = false;
    std::wstring segment;
    for (size_t i = 3; i <= path.size(); ++i) {
        const bool end = i == path.size();
        if (!end && !is_separator(path[i])) {
            const wchar_t c = path[i];
            if (c == L':') {
                error = L"Недопустимый символ ':' в пути (альтернативный поток или устройство)";
                return false;
            }
            segment.push_back(c);
            continue;
        }

        if (!segment.empty()) {
            if (segment == L"." || segment == L"..") {
                error = L"Путь не должен содержать '.' или '..'";
                return false;
            }
            has_segment = true;
            segment.clear();
        } else if (!end) {
            // Двойной разделитель — пустой сегмент (в том числе "d://..." и "//").
            error = L"Путь не должен содержать пустых сегментов";
            return false;
        }
    }

    if (!has_segment) {
        error = L"Путь указывает на корень диска";
        return false;
    }
    return true;
}

bool validate_working_directory(const std::wstring& path, std::wstring& error) {
    if (path.empty()) return true;
    if (!validate_local_path(path, false, error)) return false;
    if (!directory_exists(path)) {
        error = L"Рабочий каталог не существует";
        return false;
    }
    return true;
}

} // namespace process

bool decrypt_password(const std::wstring& encrypted_base64, std::wstring& password_out) {
    DATA_BLOB encrypted_blob = {};
    DATA_BLOB decrypted_blob = {};

    const std::string encoded = ws2u(encrypted_base64);
    DWORD decoded_length = 0;
    if (!CryptStringToBinaryA(encoded.c_str(), static_cast<DWORD>(encoded.size()), CRYPT_STRING_BASE64,
            nullptr, &decoded_length, nullptr, nullptr)) {
        return false;
    }

    std::vector<BYTE> decoded(decoded_length);
    if (!CryptStringToBinaryA(encoded.c_str(), static_cast<DWORD>(encoded.size()), CRYPT_STRING_BASE64,
            decoded.data(), &decoded_length, nullptr, nullptr)) {
        return false;
    }

    encrypted_blob.cbData = decoded_length;
    encrypted_blob.pbData = decoded.data();

    // CRYPTPROTECT_SYSTEM: расшифровать может только служба SYSTEM.
    if (!CryptUnprotectData(&encrypted_blob, nullptr, nullptr, nullptr, nullptr,
            CRYPTPROTECT_SYSTEM, &decrypted_blob)) {
        return false;
    }

    const int wide_length = MultiByteToWideChar(CP_UTF8, 0,
        reinterpret_cast<const char*>(decrypted_blob.pbData), static_cast<int>(decrypted_blob.cbData), nullptr, 0);
    password_out.resize(wide_length);
    MultiByteToWideChar(CP_UTF8, 0,
        reinterpret_cast<const char*>(decrypted_blob.pbData), static_cast<int>(decrypted_blob.cbData),
        password_out.data(), wide_length);

    SecureZeroMemory(decrypted_blob.pbData, decrypted_blob.cbData);
    LocalFree(decrypted_blob.pbData);
    return true;
}

bool Win32ProcessRunner::launch(const ProcessLaunchSpec& spec, RunningProcessHandle& out, std::wstring& error) {
    std::wstring path_error;
    if (!process::validate_local_path(spec.program_path, false, path_error)) {
        error = path_error;
        return false;
    }
    if (!file_exists(spec.program_path)) {
        error = L"Программа не найдена: " + spec.program_path;
        return false;
    }

    std::wstring working_directory = spec.working_directory;
    if (working_directory.empty()) working_directory = parent_directory(spec.program_path);
    if (!process::validate_working_directory(working_directory, path_error)) {
        error = path_error;
        return false;
    }

    std::wstring command_line = L"\"" + spec.program_path + L"\"";
    if (!spec.arguments.empty()) command_line += L" " + spec.arguments;

    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = static_cast<WORD>(window_show_command(spec.window_style));

    DWORD creation_flags = CREATE_DEFAULT_ERROR_MODE | CREATE_UNICODE_ENVIRONMENT;
    if (spec.window_style == WindowStyle::Hidden) creation_flags |= CREATE_NO_WINDOW;

    // Перехват stdout/stderr в файл вывода (путь строит только служба).
    HANDLE output_file = nullptr;
    if (!spec.output_path.empty()) {
        SECURITY_ATTRIBUTES security = { sizeof(security), nullptr, TRUE };
        output_file = CreateFileW(spec.output_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (output_file == INVALID_HANDLE_VALUE) {
            error = L"Не удалось создать файл вывода: " + format_win32_error(GetLastError());
            output_file = nullptr;
            return false;
        }
        startup.dwFlags |= STARTF_USESTDHANDLES;
        startup.hStdOutput = output_file;
        startup.hStdError = output_file;
    }

    // Выбор токена: клиентский (не-админ), RunAs (только админ) или служба (SYSTEM).
    HANDLE run_as_token = nullptr;
    LPVOID environment = nullptr;
    PROFILEINFOW profile = {};
    bool profile_loaded = false;
    bool use_create_process_as_user = false;
    HANDLE launch_token = nullptr;
    std::wstring password;

    if (spec.has_run_as) {
        if (!spec.caller_is_admin) {
            error = L"Запуск от другого пользователя разрешён только администратору";
            if (output_file) CloseHandle(output_file);
            return false;
        }
        if (!decrypt_password(spec.run_as.encrypted_password, password) || password.empty()) {
            error = L"Не удалось расшифровать пароль RunAs";
            if (output_file) CloseHandle(output_file);
            return false;
        }

        // LOGON32_LOGON_BATCH: headless-задача без интерактивного сеанса.
        if (!LogonUserW(spec.run_as.user_name.c_str(),
                spec.run_as.domain.empty() ? nullptr : spec.run_as.domain.c_str(),
                password.c_str(), LOGON32_LOGON_BATCH, LOGON32_PROVIDER_DEFAULT, &run_as_token)) {
            const DWORD code = GetLastError();
            SecureZeroMemory(password.data(), password.size() * sizeof(wchar_t));
            error = L"LogonUser завершился ошибкой: " + format_win32_error(code);
            if (output_file) CloseHandle(output_file);
            return false;
        }
        SecureZeroMemory(password.data(), password.size() * sizeof(wchar_t));
        password.clear();

        profile.dwSize = sizeof(profile);
        profile.lpUserName = const_cast<wchar_t*>(spec.run_as.user_name.c_str());
        profile.dwFlags = PI_NOUI;
        profile_loaded = LoadUserProfileW(run_as_token, &profile) != FALSE;
        if (!profile_loaded) {
            // Профиль не обязателен для запуска, но нужен для HKCU и %USERPROFILE%.
            profile = {};
        }

        CreateEnvironmentBlock(&environment, run_as_token, FALSE);
        launch_token = run_as_token;
        use_create_process_as_user = true;
    } else if (spec.client_token != nullptr) {
        // Токен клиента: процесс наследует права пользователя, а не службы.
        CreateEnvironmentBlock(&environment, spec.client_token, FALSE);
        launch_token = spec.client_token;
        use_create_process_as_user = true;
    } else if (spec.require_client_token) {
        // Fail closed: не-админскую задачу нельзя запускать от SYSTEM.
        error = L"Нет токена владельца задачи: запуск от имени SYSTEM запрещён";
        if (output_file) CloseHandle(output_file);
        return false;
    }

    HANDLE job = create_kill_on_close_job();

    PROCESS_INFORMATION process_info = {};
    std::vector<wchar_t> command_buffer(command_line.begin(), command_line.end());
    command_buffer.push_back(L'\0');
    std::vector<wchar_t> directory_buffer(working_directory.begin(), working_directory.end());
    directory_buffer.push_back(L'\0');

    // CREATE_SUSPENDED: процесс попадает в Job Object до запуска своих потомков.
    const DWORD flags = creation_flags | CREATE_SUSPENDED;
    BOOL created = FALSE;
    if (use_create_process_as_user) {
        created = CreateProcessAsUserW(launch_token, nullptr, command_buffer.data(),
            nullptr, nullptr, TRUE, flags, environment, directory_buffer.data(), &startup, &process_info);
    } else {
        created = CreateProcessW(nullptr, command_buffer.data(),
            nullptr, nullptr, TRUE, flags, environment, directory_buffer.data(), &startup, &process_info);
    }

    if (output_file) CloseHandle(output_file);
    if (environment) DestroyEnvironmentBlock(environment);
    release_profile(run_as_token, profile, profile_loaded);

    if (!created) {
        const DWORD code = GetLastError();
        if (job) CloseHandle(job);
        error = L"CreateProcess завершился ошибкой: " + format_win32_error(code);
        return false;
    }

    if (job) {
        // Best effort: при отказе процесс продолжит работу без kill-on-close.
        if (!AssignProcessToJobObject(job, process_info.hProcess)) {
            CloseHandle(job);
            job = nullptr;
        }
    }

    ResumeThread(process_info.hThread);
    CloseHandle(process_info.hThread);

    out.process = process_info.hProcess;
    out.job = job;
    out.pid = process_info.dwProcessId;
    return true;
}

ProcessStatus Win32ProcessRunner::poll(const RunningProcessHandle& handle) {
    ProcessStatus status;
    if (!handle.process) {
        status.exited = true;
        status.exit_code = 0;
        return status;
    }
    DWORD exit_code = STILL_ACTIVE;
    if (GetExitCodeProcess(handle.process, &exit_code) && exit_code != STILL_ACTIVE) {
        status.exited = true;
        status.exit_code = exit_code;
    }
    return status;
}

void Win32ProcessRunner::terminate(RunningProcessHandle& handle) {
    if (handle.job) {
        // Убивает всё дерево процессов, включая внуков.
        TerminateJobObject(handle.job, 1);
        return;
    }
    if (handle.process) TerminateProcess(handle.process, 1);
}

void Win32ProcessRunner::release(RunningProcessHandle& handle) {
    if (handle.job) {
        CloseHandle(handle.job);
        handle.job = nullptr;
    }
    if (handle.process) {
        CloseHandle(handle.process);
        handle.process = nullptr;
    }
    handle.pid = 0;
}
