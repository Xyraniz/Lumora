#include "lua.h"
#include "lualib.h"
#include "lumora.h"

#include <string>
#include <cstdio>
#include <cstdlib>
#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>
#include <system_error>
#include <cerrno>
#include <cstring>
#include <thread>

#if !defined(_WIN32)
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif

#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#endif

namespace
{
std::string g_clipboard;

#if defined(_WIN32)

// Real Win32 clipboard integration. On a real desktop this behaves exactly
// like the client does: the text becomes available to every other program
// through the OS clipboard. When no interactive desktop session is present
// (headless runners), the OS call fails and we report the memory clipboard.
bool writeSystemClipboard(const std::string& text)
{
    if (!std::getenv("LUMORA_SYSTEM_CLIPBOARD")) return false;
    if (!OpenClipboard(nullptr)) return false;
    bool ok = false;
    if (EmptyClipboard())
    {
        const SIZE_T bytes = (text.size() + 1) * sizeof(char);
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (memory)
        {
            if (char* destination = static_cast<char*>(GlobalLock(memory)))
            {
                memcpy(destination, text.data(), text.size() + 1);
                GlobalUnlock(memory);
                ok = SetClipboardData(CF_TEXT, memory) != nullptr;
            }
            if (!ok) GlobalFree(memory);
        }
    }
    CloseClipboard();
    return ok;
}

bool readSystemClipboard(std::string& output)
{
    if (!std::getenv("LUMORA_SYSTEM_CLIPBOARD")) return false;
    if (!OpenClipboard(nullptr) || !IsClipboardFormatAvailable(CF_TEXT)) { CloseClipboard(); return false; }
    bool ok = false;
    if (HANDLE handle = GetClipboardData(CF_TEXT))
    {
        if (const char* source = static_cast<const char*>(GlobalLock(handle)))
        {
            const SIZE_T capacity = GlobalSize(handle);
            size_t size = 0;
            while (size < capacity && source[size] != '\0') ++size;
            output.assign(source, size);
            GlobalUnlock(handle);
            ok = true;
        }
    }
    CloseClipboard();
    return ok;
}

#else

bool writeSystemClipboard(const std::string& text)
{
    if (!std::getenv("LUMORA_SYSTEM_CLIPBOARD")) return false;
    if (!std::getenv("DISPLAY") && !std::getenv("WAYLAND_DISPLAY")) return false;
    const std::array<const char*, 3> commands = {{"timeout 1s wl-copy 2>/dev/null", "timeout 1s xclip -selection clipboard 2>/dev/null", "timeout 1s xsel --clipboard --input 2>/dev/null"}};
    for (const char* command : commands)
    {
        FILE* pipe = popen(command, "w");
        if (!pipe) continue;
        const size_t written = fwrite(text.data(), 1, text.size(), pipe);
        const int status = pclose(pipe);
        if (written == text.size() && status == 0) return true;
    }
    return false;
}

bool readSystemClipboard(std::string& output)
{
    if (!std::getenv("LUMORA_SYSTEM_CLIPBOARD")) return false;
    if (!std::getenv("DISPLAY") && !std::getenv("WAYLAND_DISPLAY")) return false;
    const std::array<const char*, 3> commands = {{"timeout 1s wl-paste --no-newline 2>/dev/null", "timeout 1s xclip -selection clipboard -o 2>/dev/null", "timeout 1s xsel --clipboard --output 2>/dev/null"}};
    for (const char* command : commands)
    {
        FILE* pipe = popen(command, "r");
        if (!pipe) continue;
        std::string value;
        char buffer[4096];
        while (fgets(buffer, sizeof(buffer), pipe)) value += buffer;
        const int status = pclose(pipe);
        if (status == 0)
        {
            output = std::move(value);
            return true;
        }
    }
    return false;
}

#endif

int setClipboard(lua_State* L)
{
    const char* text = luaL_checkstring(L, 1);
    g_clipboard = text ? text : "";
    writeSystemClipboard(g_clipboard);
    return 0;
}

int getClipboard(lua_State* L)
{
    std::string value;
    if (readSystemClipboard(value)) g_clipboard = std::move(value);
    lua_pushlstring(L, g_clipboard.data(), g_clipboard.size());
    return 1;
}

void setStringField(lua_State* L, int table, const char* key, const char* value)
{
    lua_pushstring(L, value);
    lua_setfield(L, table, key);
}

void setBooleanField(lua_State* L, int table, const char* key, bool value)
{
    lua_pushboolean(L, value);
    lua_setfield(L, table, key);
}

int capabilities(lua_State* L)
{
    lua_newtable(L);
    const int table = lua_gettop(L);
    setStringField(L, table, "runtime", "Lumora");
    setStringField(L, table, "version", "0.7.0");
    std::string ignored;
    const bool systemClipboard = readSystemClipboard(ignored);
    setStringField(L, table, "clipboard", "memory");
    setBooleanField(L, table, "systemClipboardAvailable", systemClipboard);
    setStringField(L, table, "filesystem", "memory");
    setStringField(L, table, "http", "libcurl");
    setStringField(L, table, "rendering", "headless");
    setStringField(L, table, "network", "disabled");
    setBooleanField(L, table, "debug", true);
    setBooleanField(L, table, "executorHooks", false);
    setBooleanField(L, table, "robloxClient", false);
    setBooleanField(L, table, "analysis", true);
    setStringField(L, table, "dynamicCode", "observable");
    setStringField(L, table, "process", "explicit");
    return 1;
}

int getCallStack(lua_State* L)
{
    const int firstLevel = int(luaL_optinteger(L, 1, 1));
    const int maxFrames = int(luaL_optinteger(L, 2, 64));
    if (firstLevel < 0)
        luaL_argerror(L, 1, "level must be non-negative");
    if (maxFrames < 0)
        luaL_argerror(L, 2, "maxFrames must be non-negative");

    lua_newtable(L);
    int resultIndex = lua_gettop(L);
    int count = 0;
    for (int level = firstLevel; count < maxFrames; ++level)
    {
        lua_Debug ar{};
        if (!lua_getinfo(L, level, "sln", &ar))
            break;

        lua_newtable(L);
        const int frameIndex = lua_gettop(L);
        lua_pushinteger(L, level);
        lua_setfield(L, frameIndex, "level");
        lua_pushstring(L, ar.name ? ar.name : "");
        lua_setfield(L, frameIndex, "name");
        lua_pushstring(L, ar.what ? ar.what : "");
        lua_setfield(L, frameIndex, "what");
        lua_pushstring(L, ar.source ? ar.source : "");
        lua_setfield(L, frameIndex, "source");
        lua_pushstring(L, ar.short_src ? ar.short_src : "");
        lua_setfield(L, frameIndex, "short_src");
        lua_pushinteger(L, ar.currentline);
        lua_setfield(L, frameIndex, "line");
        lua_pushinteger(L, ar.linedefined);
        lua_setfield(L, frameIndex, "linedefined");
        lua_rawseti(L, resultIndex, ++count);
    }
    return 1;
}

void registerFunction(lua_State* L, int table, const char* name, lua_CFunction fn)
{
    lua_pushcfunction(L, fn, name);
    lua_setfield(L, table, name);
}

int hostReadFile(lua_State* L)
{
    const char* path = luaL_checkstring(L, 1);
    std::ifstream input(path, std::ios::binary);
    if (!input) { luaL_error(L, "could not read file '%s'", path); return 0; }
    std::ostringstream contents; contents << input.rdbuf();
    const std::string value = contents.str();
    lua_pushlstring(L, value.data(), value.size());
    return 1;
}

int hostWriteFile(lua_State* L)
{
    const char* path = luaL_checkstring(L, 1); size_t length = 0;
    const char* data = luaL_checklstring(L, 2, &length);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) { luaL_error(L, "could not write file '%s'", path); return 0; }
    output.write(data, std::streamsize(length));
    if (!output) { luaL_error(L, "could not write file '%s'", path); return 0; }
    return 0;
}

int hostPathExists(lua_State* L)
{
    std::error_code error; const bool exists = std::filesystem::exists(luaL_checkstring(L, 1), error);
    lua_pushboolean(L, exists && !error); return 1;
}

int hostIsFile(lua_State* L)
{
    std::error_code error; const auto status = std::filesystem::status(luaL_checkstring(L, 1), error);
    lua_pushboolean(L, !error && std::filesystem::is_regular_file(status)); return 1;
}

int hostIsDir(lua_State* L)
{
    std::error_code error; const auto status = std::filesystem::status(luaL_checkstring(L, 1), error);
    lua_pushboolean(L, !error && std::filesystem::is_directory(status)); return 1;
}

int hostCopy(lua_State* L)
{
    const char* source = luaL_checkstring(L, 1); const char* destination = luaL_checkstring(L, 2);
    std::error_code error; const auto status = std::filesystem::status(source, error);
    if (error || !std::filesystem::exists(status)) { luaL_error(L, "source path does not exist: %s", source); return 0; }
    const auto options = std::filesystem::is_directory(status)
        ? std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing
        : std::filesystem::copy_options::overwrite_existing;
    std::filesystem::copy(source, destination, options, error);
    if (error) { luaL_error(L, "could not copy '%s' to '%s': %s", source, destination, error.message().c_str()); return 0; }
    return 0;
}

int hostMove(lua_State* L)
{
    const char* source = luaL_checkstring(L, 1); const char* destination = luaL_checkstring(L, 2);
    std::error_code error; std::filesystem::rename(source, destination, error);
    if (error) { luaL_error(L, "could not move '%s' to '%s': %s", source, destination, error.message().c_str()); return 0; }
    return 0;
}

int hostExec(lua_State* L)
{
    const char* program = luaL_checkstring(L, 1); std::vector<std::string> args; std::string cwd; bool shell = false;
    if (lua_istable(L, 2)) for (int i = 1, n = int(lua_objlen(L, 2)); i <= n; ++i) { lua_rawgeti(L, 2, i); args.emplace_back(luaL_checkstring(L, -1)); lua_pop(L, 1); }
    if (lua_istable(L, 3)) { lua_getfield(L, 3, "cwd"); if (!lua_isnil(L, -1)) cwd = luaL_checkstring(L, -1); lua_pop(L, 1); lua_getfield(L, 3, "shell"); shell = lua_toboolean(L, -1); lua_pop(L, 1); }

#if defined(_WIN32)
    // Real Win32 process spawn mirroring the POSIX fork/exec contract: the
    // child inherits two real anonymous pipes as stdout/stderr, we drain both
    // until the child exits, and the exit code maps exactly like WEXITSTATUS.
    SECURITY_ATTRIBUTES inheritable{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE outRead = nullptr, outWrite = nullptr, errRead = nullptr, errWrite = nullptr;
    if (!CreatePipe(&outRead, &outWrite, &inheritable, 0) || !CreatePipe(&errRead, &errWrite, &inheritable, 0))
    {
        if (outRead) CloseHandle(outRead);
        if (outWrite) CloseHandle(outWrite);
        if (errRead) CloseHandle(errRead);
        if (errWrite) CloseHandle(errWrite);
        luaL_error(L, "could not create process pipes");
        return 0;
    }
    SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);

    std::string commandLine = shell ? std::string("cmd.exe /c ") + program : std::string(program);
    if (!shell) for (const std::string& argument : args) commandLine += " " + argument;

    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = outWrite;
    startup.hStdError = errWrite;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    if (!CreateProcessA(nullptr, commandLine.data(), nullptr, nullptr, TRUE, 0, nullptr, cwd.empty() ? nullptr : cwd.c_str(), &startup, &process))
    {
        CloseHandle(outRead); CloseHandle(outWrite); CloseHandle(errRead); CloseHandle(errWrite);
        luaL_error(L, "could not create process (Win32 error %lu)", GetLastError());
        return 0;
    }
    CloseHandle(process.hThread);
    // The parent must drop its copy of the write ends before draining or
    // ReadFile would never report end-of-pipe.
    CloseHandle(outWrite); CloseHandle(errWrite);

    std::string out, err;
    char buffer[4096];
    HANDLE streams[2] = {outRead, errRead};
    bool open[2] = {true, true};
    while (open[0] || open[1])
    {
        bool progressed = false;
        for (int stream = 0; stream < 2; ++stream)
        {
            if (!open[stream]) continue;
            DWORD available = 0;
            if (!PeekNamedPipe(streams[stream], nullptr, 0, nullptr, &available, nullptr))
            {
                CloseHandle(streams[stream]); open[stream] = false; progressed = true; continue;
            }
            if (available == 0) continue;
            while (available > 0)
            {
                DWORD readBytes = 0;
                if (!ReadFile(streams[stream], buffer, sizeof(buffer), &readBytes, nullptr) || readBytes == 0)
                {
                    CloseHandle(streams[stream]); open[stream] = false; break;
                }
                (stream == 0 ? out : err).append(buffer, readBytes);
                available = available > readBytes ? available - readBytes : 0;
                progressed = true;
            }
        }
        if (!progressed) Sleep(5);
    }
    CloseHandle(outRead); CloseHandle(errRead);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);

    lua_newtable(L); lua_pushlstring(L, out.data(), out.size()); lua_setfield(L, -2, "stdout"); lua_pushlstring(L, err.data(), err.size()); lua_setfield(L, -2, "stderr");
    lua_pushinteger(L, int(exitCode)); lua_setfield(L, -2, "code");
    lua_pushinteger(L, int(process.dwProcessId)); lua_setfield(L, -2, "pid");
    lua_pushboolean(L, exitCode == 0); lua_setfield(L, -2, "ok");
    return 1;
#else
    int outPipe[2], errPipe[2]; if (pipe(outPipe) || pipe(errPipe)) { luaL_error(L, "could not create process pipes"); return 0; }
    pid_t pid = fork();
    if (pid < 0) { luaL_error(L, "could not fork process"); return 0; }
    if (pid == 0) { dup2(outPipe[1], STDOUT_FILENO); dup2(errPipe[1], STDERR_FILENO); close(outPipe[0]); close(outPipe[1]); close(errPipe[0]); close(errPipe[1]); if (!cwd.empty()) chdir(cwd.c_str()); std::vector<char*> av; av.push_back(const_cast<char*>(program)); for (auto& a : args) av.push_back(const_cast<char*>(a.c_str())); av.push_back(nullptr); if (shell) execl("/bin/sh", "sh", "-c", program, (char*)nullptr); else execvp(program, av.data()); _exit(127); }
    close(outPipe[1]); close(errPipe[1]); std::string out, err; char buffer[4096]; ssize_t n; while ((n = read(outPipe[0], buffer, sizeof(buffer))) > 0) out.append(buffer, size_t(n)); while ((n = read(errPipe[0], buffer, sizeof(buffer))) > 0) err.append(buffer, size_t(n)); close(outPipe[0]); close(errPipe[0]); int status = 0; waitpid(pid, &status, 0); int code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    lua_newtable(L); lua_pushlstring(L, out.data(), out.size()); lua_setfield(L, -2, "stdout"); lua_pushlstring(L, err.data(), err.size()); lua_setfield(L, -2, "stderr"); lua_pushinteger(L, code); lua_setfield(L, -2, "code"); lua_pushinteger(L, pid); lua_setfield(L, -2, "pid"); lua_pushboolean(L, code == 0); lua_setfield(L, -2, "ok"); return 1;
#endif
}

#if defined(_WIN32)
int hostProcessPid(lua_State* L) { lua_pushinteger(L, int(GetCurrentProcessId())); return 1; }
int hostProcessExecPath(lua_State* L)
{
    char path[MAX_PATH]; const DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (n == 0 || n >= sizeof(path)) { lua_pushnil(L); return 1; }
    path[n] = 0; lua_pushstring(L, path); return 1;
}
int hostProcessKill(lua_State* L)
{
    const int pid = int(luaL_checkinteger(L, 1));
    // The Luau-level default is SIGTERM (15); on Win32 every request maps to
    // TerminateProcess — the client contract cares about the semantic
    // (forceful termination), not the POSIX signal number.
    const int sig = int(luaL_optinteger(L, 2, 15));
    HANDLE process = pid == int(GetCurrentProcessId())
        ? GetCurrentProcess()
        : OpenProcess(PROCESS_TERMINATE, FALSE, DWORD(pid));
    if (!process || process == INVALID_HANDLE_VALUE)
    {
        luaL_error(L, "could not signal process (Win32 error %lu)", GetLastError());
        return 0;
    }
    const BOOL terminated = TerminateProcess(process, UINT(sig));
    CloseHandle(process);
    if (!terminated) { luaL_error(L, "could not signal process (Win32 error %lu)", GetLastError()); return 0; }
    lua_pushboolean(L, 1); return 1;
}
#else
int hostProcessPid(lua_State* L) { lua_pushinteger(L, getpid()); return 1; }
int hostProcessExecPath(lua_State* L)
{
#if defined(__APPLE__)
    // /proc/self/exe does not exist on macOS; ask dyld for the real path and
    // resolve it, then hand back an absolute path just like the Linux branch.
    char buffer[4096]; uint32_t size = sizeof(buffer);
    if (_NSGetExecutablePath(buffer, &size) != 0) { lua_pushnil(L); return 1; }
    char resolved[4096];
    if (realpath(buffer, resolved)) { lua_pushstring(L, resolved); return 1; }
    lua_pushstring(L, buffer); return 1;
#else
    char path[4096]; ssize_t n = readlink("/proc/self/exe", path, sizeof(path)-1); if (n < 0) { lua_pushnil(L); return 1; } path[n] = 0; lua_pushstring(L, path); return 1;
#endif
}
int hostProcessKill(lua_State* L) { pid_t pid = luaL_checkinteger(L, 1); int sig = int(luaL_optinteger(L, 2, SIGTERM)); if (kill(pid, sig) != 0) { luaL_error(L, "could not signal process: %s", strerror(errno)); return 0; } lua_pushboolean(L, 1); return 1; }
#endif

int hostReadDir(lua_State* L)
{
    const char* path = luaL_optstring(L, 1, "."); std::error_code error;
    lua_newtable(L); int index = 1;
    for (const auto& entry : std::filesystem::directory_iterator(path, error))
    {
        lua_pushstring(L, entry.path().filename().string().c_str()); lua_rawseti(L, -2, index++);
    }
    if (error) { luaL_error(L, "could not read directory '%s'", path); return 0; }
    return 1;
}

int hostListDir(lua_State* L)
{
    const char* path = luaL_optstring(L, 1, "."); std::error_code error; lua_newtable(L); int index = 1;
    for (const auto& entry : std::filesystem::directory_iterator(path, error)) { lua_newtable(L); lua_pushstring(L, entry.path().filename().string().c_str()); lua_setfield(L, -2, "name"); auto st = entry.symlink_status(error); const char* type = std::filesystem::is_symlink(st) ? "symlink" : (std::filesystem::is_directory(st) ? "directory" : "file"); lua_pushstring(L, type); lua_setfield(L, -2, "type"); lua_rawseti(L, -2, index++); }
    if (error) { luaL_error(L, "could not list directory '%s'", path); return 0; } return 1;
}

int hostLink(lua_State* L) { std::error_code e; std::filesystem::create_hard_link(luaL_checkstring(L, 1), luaL_checkstring(L, 2), e); if (e) { luaL_error(L, "could not create hard link: %s", e.message().c_str()); return 0; } return 0; }
int hostSymlink(lua_State* L) { std::error_code e; std::filesystem::create_symlink(luaL_checkstring(L, 1), luaL_checkstring(L, 2), e); if (e) { luaL_error(L, "could not create symlink: %s", e.message().c_str()); return 0; } return 0; }

int hostMakeDir(lua_State* L)
{
    std::error_code error; const bool ok = std::filesystem::create_directories(luaL_checkstring(L, 1), error);
    lua_pushboolean(L, (ok || !error)); return 1;
}

int hostRemove(lua_State* L)
{
    std::error_code error; const bool ok = std::filesystem::remove_all(luaL_checkstring(L, 1), error) > 0;
    if (error) { luaL_error(L, "could not remove path"); return 0; }
    lua_pushboolean(L, ok); return 1;
}

int hostCwd(lua_State* L)
{
    std::error_code error; const auto path = std::filesystem::current_path(error);
    if (error) { luaL_error(L, "could not get current directory"); return 0; }
    lua_pushstring(L, path.string().c_str()); return 1;
}

int hostSetCwd(lua_State* L)
{
    std::error_code error; std::filesystem::current_path(luaL_checkstring(L, 1), error);
    if (error) { luaL_error(L, "could not change current directory"); return 0; }
    return 0;
}

int hostEnv(lua_State* L)
{
    const char* name = luaL_checkstring(L, 1); const char* value = std::getenv(name);
    if (value) lua_pushstring(L, value); else lua_pushnil(L); return 1;
}

// Real operating-system introspection. Every value comes from an actual OS
// call on the running host: sysctl(3) on macOS, GlobalMemoryStatusEx and
// GetNativeSystemInfo on Windows, and the procfs/sysinfo interfaces on Linux.
// No value is synthesised, and a field the OS refuses to answer stays 0.
int hostSystemInfo(lua_State* L)
{
    std::string osName = "unknown", arch = "unknown";
    double uptime = 0.0;
    double totalMemory = 0.0, freeMemory = 0.0;
    double cpuCount = 1.0;

#if defined(_WIN32)
    osName = "windows";
    SYSTEM_INFO info; GetNativeSystemInfo(&info);
    switch (info.wProcessorArchitecture)
    {
        case PROCESSOR_ARCHITECTURE_AMD64: arch = "x64"; break;
        case PROCESSOR_ARCHITECTURE_ARM64: arch = "arm64"; break;
        case PROCESSOR_ARCHITECTURE_INTEL: arch = "x86"; break;
        case PROCESSOR_ARCHITECTURE_ARM: arch = "arm"; break;
        default: arch = "unknown"; break;
    }
    cpuCount = double(info.dwNumberOfProcessors);
    uptime = double(GetTickCount64()) / 1000.0;
    MEMORYSTATUSEX memory{}; memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory))
    {
        totalMemory = double(memory.ullTotalPhys);
        freeMemory = double(memory.ullAvailPhys);
    }
#elif defined(__APPLE__)
    osName = "macos";
    {
        int mib[2] = {CTL_HW, HW_MEMSIZE}; unsigned long long memory = 0; size_t size = sizeof(memory);
        if (sysctl(mib, 2, &memory, &size, nullptr, 0) == 0) totalMemory = double(memory);
    }
    {
        // Free memory is the sum of the Mach free + inactive pages, which is
        // the same definition Activity Monitor reports.
        vm_size_t pageSize = 0; if (host_page_size(mach_host_self(), &pageSize) == KERN_SUCCESS)
        {
            vm_statistics_data_t stats{}; mach_msg_type_number_t count = HOST_VM_INFO_COUNT;
            if (host_statistics(mach_host_self(), HOST_VM_INFO, reinterpret_cast<host_info_t>(&stats), &count) == KERN_SUCCESS)
                freeMemory = double(stats.free_count + stats.inactive_count) * double(pageSize);
        }
    }
    {
        struct timeval boot{}; size_t size = sizeof(boot);
        int mib[2] = {CTL_KERN, KERN_BOOTTIME};
        struct timeval now{}; gettimeofday(&now, nullptr);
        if (sysctl(mib, 2, &boot, &size, nullptr, 0) == 0)
            uptime = double(now.tv_sec - boot.tv_sec);
    }
    {
        int cores = 0; size_t size = sizeof(cores);
        if (sysctl((int[]){CTL_HW, HW_NCPU}, 2, &cores, &size, nullptr, 0) == 0 && cores > 0) cpuCount = double(cores);
    }
    arch =
#if defined(__aarch64__) || defined(__arm64__)
        "arm64";
#else
        "x64";
#endif
#else
    osName = "linux";
#if defined(__aarch64__)
    arch = "arm64";
#elif defined(__x86_64__)
    arch = "x64";
#elif defined(__i386__)
    arch = "x86";
#endif
    cpuCount = double(std::thread::hardware_concurrency());
    if (cpuCount < 1.0) cpuCount = 1.0;
    {
        std::ifstream meminfo("/proc/meminfo"); std::string line;
        while (std::getline(meminfo, line))
        {
            const auto value = [&line]() -> double
            {
                const size_t colon = line.find(':');
                if (colon == std::string::npos) return 0.0;
                return std::strtod(line.c_str() + colon + 1, nullptr) * 1024.0;
            };
            if (line.rfind("MemTotal:", 0) == 0) totalMemory = value();
            else if (line.rfind("MemFree:", 0) == 0) freeMemory = value();
        }
    }
    {
        std::ifstream uptimeFile("/proc/uptime"); double seconds = 0.0;
        if (uptimeFile >> seconds) uptime = seconds;
    }
#endif

    lua_newtable(L);
    lua_pushstring(L, osName.c_str()); lua_setfield(L, -2, "os");
    lua_pushstring(L, arch.c_str()); lua_setfield(L, -2, "arch");
    lua_pushnumber(L, uptime); lua_setfield(L, -2, "uptime");
    lua_pushnumber(L, totalMemory); lua_setfield(L, -2, "totalMemory");
    lua_pushnumber(L, freeMemory); lua_setfield(L, -2, "freeMemory");
    lua_pushnumber(L, cpuCount); lua_setfield(L, -2, "cpuCount");
    return 1;
}
}

void registerHostGlobals(lua_State* L)
{
    registerJsonGlobals(L);
    registerFunction(L, LUA_GLOBALSINDEX, "setclipboard", setClipboard);
    registerFunction(L, LUA_GLOBALSINDEX, "getclipboard", getClipboard);
    registerFunction(L, LUA_GLOBALSINDEX, "getcallstack", getCallStack);

    lua_getglobal(L, "lumora");
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 1);
        lua_newtable(L);
    }
    const int api = lua_gettop(L);
    setStringField(L, api, "version", "0.7.0");
    registerFunction(L, api, "setClipboard", setClipboard);
    registerFunction(L, api, "getClipboard", getClipboard);
    registerFunction(L, api, "getCallStack", getCallStack);
    registerFunction(L, api, "capabilities", capabilities);
    lua_setglobal(L, "lumora");
}

void registerEmbeddedHost(lua_State* L)
{
    lua_newtable(L); const int api = lua_gettop(L);
    registerFunction(L, api, "readFile", hostReadFile);
    registerFunction(L, api, "writeFile", hostWriteFile);
    registerFunction(L, api, "exists", hostPathExists);
    registerFunction(L, api, "isFile", hostIsFile);
    registerFunction(L, api, "isDir", hostIsDir);
    registerFunction(L, api, "readDir", hostReadDir);
    registerFunction(L, api, "makeDir", hostMakeDir);
    registerFunction(L, api, "remove", hostRemove);
    registerFunction(L, api, "copy", hostCopy);
    registerFunction(L, api, "move", hostMove);
    registerFunction(L, api, "cwd", hostCwd);
    registerFunction(L, api, "setCwd", hostSetCwd);
    registerFunction(L, api, "env", hostEnv);
    registerFunction(L, api, "systemInfo", hostSystemInfo);
    registerFunction(L, api, "exec", hostExec);
    registerFunction(L, api, "pid", hostProcessPid);
    registerFunction(L, api, "execPath", hostProcessExecPath);
    registerFunction(L, api, "kill", hostProcessKill);
    registerFunction(L, api, "listDir", hostListDir);
    registerFunction(L, api, "link", hostLink);
    registerFunction(L, api, "symlink", hostSymlink);
    lua_setglobal(L, "__lumora_host");
}
