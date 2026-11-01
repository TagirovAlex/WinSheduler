#pragma once
#include "sqlite3.h"
#include <string>

// Целевая версия схемы в PRAGMA user_version (./docs/contracts/DB.md, раздел 2).
constexpr int DB_SCHEMA_VERSION = 2;

// SID, которым помечаются задачи, созданные до появления владельца.
inline constexpr const char* DB_LEGACY_OWNER_SID = "S-1-5-18";

namespace migrations {

bool table_exists(sqlite3* db, const char* table);
bool column_exists(sqlite3* db, const char* table, const char* column);

int read_user_version(sqlite3* db);
void set_user_version(sqlite3* db, int version);

// Создание целевой схемы v2. Идемпотентно: CREATE ... IF NOT EXISTS.
void create_schema_v2(sqlite3* db);

// Миграция v1 → v2. Вызывается внутри уже открытой транзакции BEGIN IMMEDIATE.
// Старые колонки сохраняются (контракт DB.md, раздел 4).
void migrate_v1_to_v2(sqlite3* db);

// Значение install.owner_sid из legacy-таблицы settings, иначе DB_LEGACY_OWNER_SID.
std::string legacy_owner_sid(sqlite3* db);

} // namespace migrations
