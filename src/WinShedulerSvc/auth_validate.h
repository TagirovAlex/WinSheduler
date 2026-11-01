#pragma once
#include "config.h"
#include <string>

// Валидация входных данных IPC. Чистый модуль (кроме проверки %TEMP%),
// покрывается юнит-тестами в ./tests/ipc.
// Правила: ./docs/contracts/SECURITY.md раздел 5, ./docs/contracts/IPC.md раздел 5.
namespace auth {

// GUID в канонической форме 8-4-4-4-12 (регистр не важен).
bool is_guid(const std::wstring& value);

// Локальный абсолютный путь: буква диска + ':' + разделитель.
// Запрещены UNC, device paths, '..', пустые сегменты, альтернативные потоки.
bool is_local_absolute_path(const std::wstring& path);

// Рабочий каталог: пустая строка допустима, корень диска — нет.
bool is_valid_working_directory(const std::wstring& path);

bool is_hhmm(const std::wstring& value);            // "HH:mm"
bool is_yyyy_mm_dd(const std::wstring& value);      // "yyyy-MM-dd"
bool is_yyyy_mm_dd_thh_mm(const std::wstring& value); // "yyyy-MM-ddTHH:mm"

// Проверка задачи перед записью в БД. error заполняется по-русски.
bool validate_task(const TaskDefinition& task, bool is_admin, std::wstring& error);

// Проверка триггера по правилам IPC.md раздел 5.
bool validate_time_window(const TimeWindow& window, std::wstring& error);

// Обрезка пользовательских данных для логов и аудита: CR/LF заменяются пробелом
// (защита от log injection, SECURITY.md раздел 7).
std::wstring sanitize_line(const std::wstring& value);

} // namespace auth
