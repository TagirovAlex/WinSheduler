#pragma once
#include <string>
#include <vector>

// Вспомогательные функции слоя хранения: время, GUID, JSON-массивы.
// Выделены в отдельный модуль, чтобы репозитории не дублировали разбор.

// Текущее время в UTC, формат ISO 8601 с 'Z' (например, 2026-11-01T09:00:00Z).
std::wstring db_utc_now();

// Время UTC N суток назад в формате ISO 8601 с 'Z'. Для retention.
std::wstring db_utc_days_ago(int days);

// Время UTC для имени файла резервной копии: yyyyMMdd-HHmmss.
std::wstring db_utc_timestamp();

// Строка является каноническим GUID в нижнем регистре без фигурных скобок.
// Используется для проверки имён каталогов/файлов вывода перед удалением.
bool db_is_guid(const std::wstring& value);

// Нормализация GUID к нижнему регистру без фигурных скобок.
std::wstring db_normalize_guid(const std::wstring& value);

// Сериализация массивов для колонок *_json.
std::string db_json_int_array(const std::vector<int>& values);
std::string db_json_string_array(const std::vector<std::wstring>& values);

// Разбор JSON-массивов из колонок. Некорректный JSON даёт пустой массив.
std::vector<int> db_parse_int_array(const char* text);
std::vector<std::wstring> db_parse_string_array(const char* text);
