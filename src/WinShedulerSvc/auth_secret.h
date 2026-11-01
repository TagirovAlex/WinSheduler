#pragma once
#include <string>

// Шифрование секретов (паролей RunAs) средствами DPAPI.
// Контракт: ./docs/contracts/SECURITY.md раздел 4.
// Служба работает от LocalSystem, поэтому используется CRYPTPROTECT_SYSTEM:
// расшифровать blob может только SYSTEM, а не любой пользователь машины.
namespace auth {

// Шифрует пароль и возвращает base64. Пустая строка — пустой результат.
std::wstring protect_secret(const std::wstring& plain);

// Расшифровывает base64-blob. legacy_local_machine=true допускает старый
// формат CRYPTPROTECT_LOCAL_MACHINE (считается скомпрометированным,
// поддерживается только для чтения унаследованных записей).
bool unprotect_secret(const std::wstring& base64, std::wstring& plain, bool legacy_local_machine = false);

// Затирает строку в памяти (SecureZeroMemory).
void wipe_secret(std::wstring& value);

} // namespace auth
