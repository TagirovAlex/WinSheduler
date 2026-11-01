#pragma once
#include "config.h"
#include <string>

// Спецификация запуска одного процесса планировщиком.
struct ProcessLaunchSpec {
    std::wstring program_path;
    std::wstring arguments;
    std::wstring working_directory;      // пусто — каталог программы
    WindowStyle window_style = WindowStyle::Hidden;
    std::wstring output_path;            // пусто — вывод не перехватывается
    bool has_run_as = false;             // задача задаёт RunAsUser (только админ)
    RunAsCredentials run_as;
    HANDLE client_token = nullptr;       // primary-токен вызывающего (не-админ)
    bool require_client_token = false;   // без токена запуск запрещён (fail closed)
    bool caller_is_admin = false;        // разрешает RunAs и запуск от SYSTEM
};

// Состояние процесса, наблюдаемое планировщиком.
struct ProcessStatus {
    bool exited = false;
    DWORD exit_code = 0;
};

// Дескриптор запущенного процесса: владеет process handle и Job Object.
struct RunningProcessHandle {
    HANDLE process = nullptr;
    HANDLE job = nullptr;
    DWORD pid = 0;
    void* user_data = nullptr;           // для fake-раннера в тестах
};

// Абстракция запуска процессов: позволяет подменить раннер в тестах.
class IProcessRunner {
public:
    virtual ~IProcessRunner() = default;
    // Запускает процесс. При ошибке возвращает false и текст ошибки по-русски.
    virtual bool launch(const ProcessLaunchSpec& spec, RunningProcessHandle& out, std::wstring& error) = 0;
    // Проверяет, завершился ли процесс. Не освобождает handle'ы.
    virtual ProcessStatus poll(const RunningProcessHandle& handle) = 0;
    // Принудительно завершает дерево процессов (Job Object).
    virtual void terminate(RunningProcessHandle& handle) = 0;
    // Закрывает handle'ы. Повторный вызов безопасен.
    virtual void release(RunningProcessHandle& handle) = 0;
};

// Реальный раннер: Job Object, kill дерева, RunAs, перехват stdout/stderr.
class Win32ProcessRunner final : public IProcessRunner {
public:
    bool launch(const ProcessLaunchSpec& spec, RunningProcessHandle& out, std::wstring& error) override;
    ProcessStatus poll(const RunningProcessHandle& handle) override;
    void terminate(RunningProcessHandle& handle) override;
    void release(RunningProcessHandle& handle) override;
};

// Расшифровка пароля DPAPI (CRYPTPROTECT_SYSTEM, см. SECURITY.md).
bool decrypt_password(const std::wstring& encrypted_base64, std::wstring& password_out);

namespace process {

// Проверка пути по ./docs/contracts/SECURITY.md:
// локальный абсолютный, без UNC/device-путей, "..", альтернативных потоков.
bool validate_local_path(const std::wstring& path, bool allow_empty, std::wstring& error);

// Проверка рабочего каталога: локальный, существует, не корень диска.
bool validate_working_directory(const std::wstring& path, std::wstring& error);

} // namespace process
