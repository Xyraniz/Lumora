#pragma once
struct lua_State;
void registerNativeModules(lua_State* L);
int requireNativeModule(lua_State* L, const char* name);
void registerExecutorCryptoGlobals(lua_State* L, int (*lz4Compress)(lua_State*), int (*lz4Decompress)(lua_State*));
