#pragma once
struct lua_State;
void registerNativeModules(lua_State* L);
int requireNativeModule(lua_State* L, const char* name);
