#ifndef LUMORA_H
#define LUMORA_H

#include <string>

struct lua_State;

// runtime.cpp
std::string readFile(const char* path);
void pushArgs(lua_State* L, int argc, char** argv, int scriptIndex);
void applySandbox(lua_State* L);
int runScript(const char* path, int argc, char** argv, bool roblox, bool sandbox, double timeout);

// prelude.cpp
bool installPrelude(lua_State* L);
void registerRobloxGlobals(lua_State* L);

// host_api.cpp
void registerHostGlobals(lua_State* L);

// json.cpp
void registerJsonGlobals(lua_State* L);

// json.cpp
std::string jsonEscape(const std::string& value);

#endif // LUMORA_H
