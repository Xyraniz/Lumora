#include "lua.h"
#include "lualib.h"
#include "Luau/Compiler.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

static std::string readFile(const char* path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error(std::string("cannot open script: ") + path);
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

static void pushArgs(lua_State* L, int argc, char** argv, int scriptIndex)
{
    lua_createtable(L, argc - scriptIndex, 0);
    for (int i = scriptIndex; i < argc; ++i)
    {
        lua_pushinteger(L, i - scriptIndex);
        lua_pushstring(L, argv[i]);
        lua_settable(L, -3);
    }
    lua_setglobal(L, "arg");
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: luau-vm script.lua [args...]\n";
        return 2;
    }

    try
    {
        const std::string source = readFile(argv[1]);
        lua_State* L = luaL_newstate();
        if (!L)
        {
            std::cerr << "failed to create Luau state\n";
            return 70;
        }
        luaL_openlibs(L);
        pushArgs(L, argc, argv, 1);

        Luau::CompileOptions options;
        options.optimizationLevel = 1;
        options.debugLevel = 1;
        const std::string bytecode = Luau::compile(source, options);
        if (luau_load(L, argv[1], bytecode.data(), bytecode.size(), 0) != 0)
        {
            std::cerr << lua_tostring(L, -1) << "\n";
            lua_close(L);
            return 1;
        }
        if (lua_pcall(L, 0, LUA_MULTRET, 0) != 0)
        {
            std::cerr << lua_tostring(L, -1) << "\n";
            lua_close(L);
            return 1;
        }
        lua_close(L);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << "\n";
        return 2;
    }
}
