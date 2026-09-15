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
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <cerrno>
#include <cstring>

namespace
{
std::string g_clipboard;

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
    setStringField(L, table, "version", "0.4.0");
    std::string ignored;
    const bool systemClipboard = readSystemClipboard(ignored);
    setStringField(L, table, "clipboard", "memory");
    setBooleanField(L, table, "systemClipboardAvailable", systemClipboard);
    setStringField(L, table, "filesystem", "memory");
    setStringField(L, table, "http", "disabled");
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
    int outPipe[2], errPipe[2]; if (pipe(outPipe) || pipe(errPipe)) { luaL_error(L, "could not create process pipes"); return 0; }
    pid_t pid = fork();
    if (pid < 0) { luaL_error(L, "could not fork process"); return 0; }
    if (pid == 0) { dup2(outPipe[1], STDOUT_FILENO); dup2(errPipe[1], STDERR_FILENO); close(outPipe[0]); close(outPipe[1]); close(errPipe[0]); close(errPipe[1]); if (!cwd.empty()) chdir(cwd.c_str()); std::vector<char*> av; av.push_back(const_cast<char*>(program)); for (auto& a : args) av.push_back(const_cast<char*>(a.c_str())); av.push_back(nullptr); if (shell) execl("/bin/sh", "sh", "-c", program, (char*)nullptr); else execvp(program, av.data()); _exit(127); }
    close(outPipe[1]); close(errPipe[1]); std::string out, err; char buffer[4096]; ssize_t n; while ((n = read(outPipe[0], buffer, sizeof(buffer))) > 0) out.append(buffer, size_t(n)); while ((n = read(errPipe[0], buffer, sizeof(buffer))) > 0) err.append(buffer, size_t(n)); close(outPipe[0]); close(errPipe[0]); int status = 0; waitpid(pid, &status, 0); int code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    lua_newtable(L); lua_pushlstring(L, out.data(), out.size()); lua_setfield(L, -2, "stdout"); lua_pushlstring(L, err.data(), err.size()); lua_setfield(L, -2, "stderr"); lua_pushinteger(L, code); lua_setfield(L, -2, "code"); lua_pushinteger(L, pid); lua_setfield(L, -2, "pid"); lua_pushboolean(L, code == 0); lua_setfield(L, -2, "ok"); return 1;
}

int hostProcessPid(lua_State* L) { lua_pushinteger(L, getpid()); return 1; }
int hostProcessExecPath(lua_State* L) { char path[4096]; ssize_t n = readlink("/proc/self/exe", path, sizeof(path)-1); if (n < 0) { lua_pushnil(L); return 1; } path[n] = 0; lua_pushstring(L, path); return 1; }
int hostProcessKill(lua_State* L) { pid_t pid = luaL_checkinteger(L, 1); int sig = int(luaL_optinteger(L, 2, SIGTERM)); if (kill(pid, sig) != 0) { luaL_error(L, "could not signal process: %s", strerror(errno)); return 0; } lua_pushboolean(L, 1); return 1; }

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
    setStringField(L, api, "version", "0.4.0");
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
    registerFunction(L, api, "exec", hostExec);
    registerFunction(L, api, "pid", hostProcessPid);
    registerFunction(L, api, "execPath", hostProcessExecPath);
    registerFunction(L, api, "kill", hostProcessKill);
    registerFunction(L, api, "listDir", hostListDir);
    registerFunction(L, api, "link", hostLink);
    registerFunction(L, api, "symlink", hostSymlink);
    lua_setglobal(L, "__lumora_host");
}
