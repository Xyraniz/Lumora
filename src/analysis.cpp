#include "lua.h"
#include "lualib.h"
#include "lumora.h"

#include <regex>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <fstream>
#include <sstream>

namespace {

static std::string value(lua_State* L, int index) {
    size_t n = 0; const char* s = luaL_checklstring(L, index, &n); return std::string(s, n);
}
static void field(lua_State* L, int table, const char* key, const std::string& v) { lua_pushlstring(L, v.data(), v.size()); lua_setfield(L, table, key); }
static void field(lua_State* L, int table, const char* key, int v) { lua_pushinteger(L, v); lua_setfield(L, table, key); }
static void field(lua_State* L, int table, const char* key, bool v) { lua_pushboolean(L, v); lua_setfield(L, table, key); }
static void add(lua_State* L, int table, int index, const std::string& v) { lua_pushlstring(L, v.data(), v.size()); lua_rawseti(L, table, index); }

static std::regex compilePattern(lua_State* L, int index) {
    try { return std::regex(value(L, index), std::regex::ECMAScript); }
    catch (const std::regex_error& e) { luaL_error(L, "invalid regular expression: %s", e.what()); return {}; }
}

static int regexIsMatch(lua_State* L) {
    const std::string input = value(L, 1); const auto pattern = compilePattern(L, 2);
    lua_pushboolean(L, std::regex_search(input, pattern)); return 1;
}

static void pushMatch(lua_State* L, const std::string& input, const std::smatch& m) {
    lua_newtable(L); const int t = lua_gettop(L);
    field(L, t, "text", m.str()); field(L, t, "start", int(m.position()) + 1); field(L, t, "end", int(m.position() + m.length()));
    lua_newtable(L); const int groups = lua_gettop(L);
    for (size_t i = 1; i < m.size(); ++i) { if (m[i].matched) add(L, groups, int(i), m[i].str()); }
    lua_setfield(L, t, "groups");
}

static int regexMatch(lua_State* L) {
    const std::string input = value(L, 1); const auto pattern = compilePattern(L, 2); std::smatch m;
    if (!std::regex_search(input, m, pattern)) { lua_pushnil(L); return 1; }
    pushMatch(L, input, m); return 1;
}

static int regexMatchAll(lua_State* L) {
    const std::string input = value(L, 1); const auto pattern = compilePattern(L, 2); std::sregex_iterator it(input.begin(), input.end(), pattern), end;
    lua_newtable(L); const int result = lua_gettop(L); int index = 1;
    for (; it != end; ++it) pushMatch(L, input, *it), lua_rawseti(L, result, index++);
    return 1;
}

static int regexReplace(lua_State* L) {
    const std::string input = value(L, 1), replacement = value(L, 3); const auto pattern = compilePattern(L, 2);
    try { const std::string out = std::regex_replace(input, pattern, replacement); lua_pushlstring(L, out.data(), out.size()); return 1; }
    catch (const std::regex_error& e) { luaL_error(L, "regex replacement failed: %s", e.what()); return 0; }
}

static int regexSplit(lua_State* L) {
    const std::string input = value(L, 1); const auto pattern = compilePattern(L, 2); std::sregex_token_iterator it(input.begin(), input.end(), pattern, -1), end;
    lua_newtable(L); const int result = lua_gettop(L); int index = 1;
    for (; it != end; ++it) add(L, result, index++, *it);
    return 1;
}

static std::string read(const std::string& path) { std::ifstream f(path); std::ostringstream s; s << f.rdbuf(); return s.str(); }

static int graph(lua_State* L) {
    std::string source = value(L, 1); if (source.rfind("@file:", 0) == 0) source = read(source.substr(6));
    std::map<std::string, int> lines; std::set<std::string> calls; std::vector<std::pair<std::string,std::string>> edges;
    std::regex fn(R"((?:local\s+)?function\s+([A-Za-z_][A-Za-z0-9_]*))"), req(R"(require\s*\(\s*["']([^"']+)["']\s*\))"), call(R"(([A-Za-z_][A-Za-z0-9_]*)\s*\()"), line(R"([^\n]*)");
    std::string current = "<main>"; int number = 0; std::stringstream stream(source); std::string text;
    while (std::getline(stream, text)) {
        ++number; std::smatch m;
        if (std::regex_search(text, m, fn)) { current = m[1].str(); lines[current] = number; }
        for (std::sregex_iterator i(text.begin(), text.end(), req), e; i != e; ++i) edges.emplace_back(current, "module:" + (*i)[1].str());
        for (std::sregex_iterator i(text.begin(), text.end(), call), e; i != e; ++i) { const std::string target = (*i)[1].str(); if (target != "if" && target != "for" && target != "while" && target != "function") { calls.insert(target); edges.emplace_back(current, "call:" + target); } }
    }
    lua_newtable(L); const int out = lua_gettop(L); lua_newtable(L); const int nodes = lua_gettop(L); int ni = 1;
    for (const auto& p : lines) { lua_newtable(L); const int n = lua_gettop(L); field(L,n,"id",p.first); field(L,n,"kind","function"); field(L,n,"line",p.second); lua_rawseti(L,nodes,ni++); }
    lua_newtable(L); const int edgeTable = lua_gettop(L); int ei = 1;
    for (const auto& edge : edges) { lua_newtable(L); const int e = lua_gettop(L); field(L,e,"from",edge.first); field(L,e,"to",edge.second); lua_rawseti(L,edgeTable,ei++); }
    lua_setfield(L,out,"edges"); lua_setfield(L,out,"nodes"); field(L,out,"lineCount",number); return 1;
}

static int scan(lua_State* L) {
    const std::string source = value(L, 1); lua_newtable(L); const int out = lua_gettop(L); int n = 1;
    const std::vector<std::pair<const char*, const char*>> rules = {{"dynamic-code", "loadstring"}, {"dynamic-code", "load("}, {"filesystem", "writefile"}, {"filesystem", "readfile"}, {"network", "request"}, {"network", "HttpGet"}, {"process", "os.execute"}, {"debug", "getfenv"}};
    for (const auto& rule : rules) { size_t pos = 0; while ((pos = source.find(rule.second, pos)) != std::string::npos) { lua_newtable(L); const int item = lua_gettop(L); field(L,item,"category",rule.first); field(L,item,"pattern",rule.second); field(L,item,"offset",int(pos)); lua_rawseti(L,out,n++); pos += std::string(rule.second).size(); } }
    return 1;
}

static void addFunction(lua_State* L, int table, const char* name, lua_CFunction fn) { lua_pushcfunction(L, fn, name); lua_setfield(L, table, name); }
}

void registerAnalysisGlobals(lua_State* L) {
    lua_getglobal(L, "lumora"); if (!lua_istable(L, -1)) { lua_pop(L, 1); lua_newtable(L); }
    const int api = lua_gettop(L); lua_newtable(L); const int regex = lua_gettop(L);
    addFunction(L, regex, "isMatch", regexIsMatch); addFunction(L, regex, "match", regexMatch); addFunction(L, regex, "matchAll", regexMatchAll); addFunction(L, regex, "replace", regexReplace); addFunction(L, regex, "split", regexSplit); lua_setfield(L, api, "regex");
    lua_newtable(L); const int analysis = lua_gettop(L); addFunction(L, analysis, "graph", graph); addFunction(L, analysis, "scan", scan); lua_setfield(L, api, "analysis");
    lua_setglobal(L, "lumora");
}

void registerEmbeddedAnalysisModules(lua_State* L) {
    lua_getglobal(L, "lumora"); if (!lua_istable(L, -1)) { lua_pop(L,1); return; }
    lua_getfield(L, -1, "regex"); lua_setfield(L, LUA_GLOBALSINDEX, "__lumora_regex"); lua_getfield(L, -1, "analysis"); lua_setfield(L, LUA_GLOBALSINDEX, "__lumora_analysis"); lua_pop(L,1);
}
