# WinSheduler — Project Knowledge

## Architecture
- **C++ service backend** (`WinShedulerSvc.exe`, ~1.5 MB, /MT static CRT, no .NET)
- **C# WPF UI** (`WinSheduler.UI`, .NET 8) — talks to service via Named Pipe
- **SQLite** (amalgamation `sqlite3.c`/`sqlite3.h`) — 3 tables: `tasks`, `time_windows`, `run_history`
- **IPC**: Named Pipe `\\.\pipe\WinSheduler`, message-mode, JSON protocol
- **DPAPI** (`CryptProtectData`/`CryptUnprotectData`, LocalMachine) for credential encryption

## C++ Service (`src/WinShedulerSvc/`)

### Source Files
| File | Purpose |
|------|---------|
| `main.cpp` | Entry point, `--standalone` mode, `--dbpath`, `--logdir` |
| `service.h/cpp` | Windows Service: ServiceMain, HandlerEx, stop/shutdown |
| `database.h/cpp` | SQLite CRUD wrapper (tasks, time_windows, run_history) |
| `ipc.h/cpp` | Named Pipe server + json_helpers serialization |
| `scheduler.h/cpp` | Scheduler engine: tick timer, time window eval, process launch, timeout |
| `json.h/cpp` | Minimal JSON parser/generator (no deps) |
| `config.h` | Shared types, enums, `ws2u`/`u2ws` helpers, pipe constants |

### Key Technical Details
- **Win32 includes**: `<windows.h>` in `config.h` (no `WIN32_LEAN_AND_MEAN`)
- **GUID generation**: `UuidCreate` (from `<rpcdce.h>`, not `CoCreateGuid`)
- **SQLite**: local include with `#include "sqlite3.h"` (not angle brackets)
- **Intermediate files**: `tmp/<Configuration>/` (not `obj/`)
- **Service class**: `service_main`/`service_ctrl` are `public` static members (called from global C wrappers)
- **Build**: 0 errors, 0 warnings (Debug + Release)

### Build Fixes (Session 2025-06-15)
1. Added `<windows.h>` + `<wincrypt.h>` to `config.h`
2. Removed `WIN32_LEAN_AND_MEAN` from vcxproj and config.h
3. `CoCreateGuid` → `UuidCreate`
4. `#include <sqlite3.h>` → `#include "sqlite3.h"`
5. Added `#include "config.h"` to `service.h`
6. Reordered `main.cpp`: `database.h` before `service.h`
7. Made `service_main`/`service_ctrl` public
8. Added missing `trim_history` impl in `scheduler.cpp`
9. `IntDir`: `obj/` → `tmp/`

### Key Config
- `PIPE_NAME`: `L"\\\\.\\pipe\\WinSheduler"`
- `PIPE_BUFFER_SIZE`: 65536
- `SCHEDULER_TICK_MS`: 1000
- `/MT` static CRT, C++20 (`stdcpp20`)
- v143 toolset (VS 2022), Windows 10 SDK

### Build Commands
```powershell
# Release
MSBuild WinShedulerSvc.vcxproj /p:Configuration=Release /p:Platform=x64

# Debug
MSBuild WinShedulerSvc.vcxproj /p:Configuration=Debug /p:Platform=x64
```
