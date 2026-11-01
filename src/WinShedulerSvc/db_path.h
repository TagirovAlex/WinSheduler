#pragma once
#include <string>

// Проверка пути к файлу БД и ACL каталога данных.
// Правила — ./docs/contracts/SECURITY.md, разделы 5 и 8.

// Путь должен быть локальным абсолютным: буква диска, двоеточие, разделитель.
// Запрещены UNC, device paths, '..', пустые сегменты и альтернативные потоки.
bool db_path_is_valid(const std::wstring& path, std::wstring& error);

// Каталог не должен быть доступен на запись Everyone/Users/Authenticated Users/Interactive.
// Возвращает false и текст ошибки, если ACL прочитать не удалось или доступ не ограничен.
bool db_dir_acl_is_restricted(const std::wstring& dir, std::wstring& error);

// Рекурсивное создание каталога. Пустой путь считается успехом.
bool db_ensure_directory(const std::wstring& dir, std::wstring& error);
