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
#include <filesystem>
#include <algorithm>

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

static void pushMatch(lua_State* L, const std::smatch& m) {
    lua_newtable(L); const int t = lua_gettop(L);
    field(L, t, "text", m.str()); field(L, t, "start", int(m.position()) + 1); field(L, t, "end", int(m.position() + m.length()));
    lua_newtable(L); const int groups = lua_gettop(L);
    for (size_t i = 1; i < m.size(); ++i) if (m[i].matched) add(L, groups, int(i), m[i].str());
    lua_setfield(L, t, "groups");
}

static int regexMatch(lua_State* L) {
    const std::string input = value(L, 1); const auto pattern = compilePattern(L, 2); std::smatch m;
    if (!std::regex_search(input, m, pattern)) { lua_pushnil(L); return 1; }
    pushMatch(L, m); return 1;
}

static int regexMatchAll(lua_State* L) {
    const std::string input = value(L, 1); const auto pattern = compilePattern(L, 2); std::sregex_iterator it(input.begin(), input.end(), pattern), end;
    lua_newtable(L); const int result = lua_gettop(L); int index = 1;
    for (; it != end; ++it) { pushMatch(L, *it); lua_rawseti(L, result, index++); }
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

struct Module {
    std::string id;
    std::string path;
    std::string source;
};

struct ModuleGraph {
    std::vector<Module> modules;
    std::vector<std::pair<std::string, std::string>> moduleEdges;
    std::map<std::string, std::vector<std::string>> adjacency;
    std::set<std::string> seen;
};

static std::string normalizeModulePath(const std::filesystem::path& path) {
    std::error_code error;
    const auto absolute = std::filesystem::weakly_canonical(path, error);
    return (error ? path.lexically_normal() : absolute).generic_string();
}

static std::string resolveModule(const std::string& parent, const std::string& requested) {
    if (requested.empty() || requested[0] != '.') return {};
    std::filesystem::path base = std::filesystem::path(parent).parent_path() / requested;
    std::vector<std::filesystem::path> candidates = {base, base.string() + ".luau", base.string() + ".lua", base / "init.luau", base / "init.lua"};
    for (const auto& candidate : candidates) {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error) && !error) return normalizeModulePath(candidate);
    }
    return {};
}

static void collectModule(ModuleGraph& graph, const std::string& path) {
    const std::string id = normalizeModulePath(path);
    if (graph.seen.count(id)) return;
    graph.seen.insert(id);
    Module module{id, id, read(id)};
    graph.modules.push_back(module);
    const std::regex req(R"(require\s*\(\s*["']([^"']+)["']\s*\))");
    for (std::sregex_iterator it(module.source.begin(), module.source.end(), req), end; it != end; ++it) {
        const std::string requested = (*it)[1].str();
        const std::string target = resolveModule(id, requested);
        const std::string targetId = target.empty() ? "module:" + requested : target;
        graph.moduleEdges.emplace_back(id, targetId);
        graph.adjacency[id].push_back(targetId);
        if (!target.empty()) collectModule(graph, target);
    }
}

static void dfsCycles(const std::string& node, const std::map<std::string, std::vector<std::string>>& adjacency,
                      std::map<std::string, int>& colors, std::vector<std::string>& stack,
                      std::vector<std::vector<std::string>>& cycles) {
    colors[node] = 1; stack.push_back(node);
    auto found = adjacency.find(node);
    if (found != adjacency.end()) {
        for (const std::string& next : found->second) {
            if (!adjacency.count(next)) continue;
            if (colors[next] == 0) dfsCycles(next, adjacency, colors, stack, cycles);
            else if (colors[next] == 1) {
                auto begin = std::find(stack.begin(), stack.end(), next);
                if (begin != stack.end()) { std::vector<std::string> cycle(begin, stack.end()); cycle.push_back(next); cycles.push_back(std::move(cycle)); }
            }
        }
    }
    stack.pop_back(); colors[node] = 2;
}

static int graph(lua_State* L) {
    const std::string input = value(L, 1);
    ModuleGraph graphData;
    std::string rootId = "<main>";
    if (input.rfind("@file:", 0) == 0) {
        const std::string rootPath = normalizeModulePath(input.substr(6));
        collectModule(graphData, rootPath);
        rootId = rootPath;
    } else {
        graphData.modules.push_back({rootId, rootId, input});
        graphData.seen.insert(rootId);
        const std::regex req(R"(require\s*\(\s*["']([^"']+)["']\s*\))");
        for (std::sregex_iterator it(input.begin(), input.end(), req), end; it != end; ++it) {
            const std::string target = "module:" + (*it)[1].str();
            graphData.moduleEdges.emplace_back(rootId, target);
            graphData.adjacency[rootId].push_back(target);
        }
    }

    std::map<std::string, int> colors; std::vector<std::string> stack; std::vector<std::vector<std::string>> cycles;
    for (const auto& module : graphData.modules) if (colors[module.id] == 0) dfsCycles(module.id, graphData.adjacency, colors, stack, cycles);

    lua_newtable(L); const int out = lua_gettop(L);
    lua_newtable(L); const int nodes = lua_gettop(L); int ni = 1;
    for (const auto& module : graphData.modules) {
        lua_newtable(L); const int node = lua_gettop(L); field(L, node, "id", module.id); field(L, node, "kind", "module"); field(L, node, "path", module.path);
        int line = 1; for (char c : module.source) if (c == '\n') ++line; field(L, node, "lineCount", line); lua_rawseti(L, nodes, ni++);
    }
    lua_setfield(L, out, "modules");

    lua_newtable(L); const int edges = lua_gettop(L); int ei = 1;
    for (const auto& edge : graphData.moduleEdges) { lua_newtable(L); const int item = lua_gettop(L); field(L, item, "from", edge.first); field(L, item, "to", edge.second); field(L, item, "kind", "module"); lua_rawseti(L, edges, ei++); }
    lua_setfield(L, out, "moduleEdges");

    lua_newtable(L); const int cycleTable = lua_gettop(L); int ci = 1;
    for (const auto& cycle : cycles) { lua_newtable(L); const int item = lua_gettop(L); int index = 1; for (const auto& id : cycle) add(L, item, index++, id); lua_rawseti(L, cycleTable, ci++); }
    lua_setfield(L, out, "cycles"); field(L, out, "hasCycle", !cycles.empty());
    field(L, out, "moduleCount", int(graphData.modules.size())); field(L, out, "edgeCount", int(graphData.moduleEdges.size()));

    // Preserve the original single-source call graph fields for callers that
    // pass source text directly, while the module fields above expose the
    // recursive global graph for @file inputs.
    const std::string source = graphData.modules.empty() ? input : graphData.modules.front().source;
    std::regex fn(R"((?:local\s+)?function\s+([A-Za-z_][A-Za-z0-9_]*))"), call(R"(([A-Za-z_][A-Za-z0-9_]*)\s*\()"), req(R"(require\s*\(\s*["']([^"']+)["']\s*\))");
    lua_newtable(L); const int functionNodes = lua_gettop(L); int nodeIndex = 1; std::string current = "<main>"; int lineNumber = 0;
    std::stringstream lines(source); std::string line;
    while (std::getline(lines, line)) {
        ++lineNumber; std::smatch match;
        if (std::regex_search(line, match, fn)) { current = match[1].str(); lua_newtable(L); const int node = lua_gettop(L); field(L,node,"id",current); field(L,node,"kind","function"); field(L,node,"line",lineNumber); lua_rawseti(L,functionNodes,nodeIndex++); }
    }
    lua_setfield(L, out, "nodes");
    lua_newtable(L); const int callEdges = lua_gettop(L); int callIndex = 1; lineNumber = 0; current = "<main>";
    std::stringstream edgeLines(source);
    while (std::getline(edgeLines, line)) {
        ++lineNumber; std::smatch match;
        if (std::regex_search(line, match, fn)) current = match[1].str();
        for (std::sregex_iterator it(line.begin(), line.end(), req), end; it != end; ++it) { lua_newtable(L); const int edge = lua_gettop(L); field(L,edge,"from",current); field(L,edge,"to","module:" + (*it)[1].str()); lua_rawseti(L,callEdges,callIndex++); }
        for (std::sregex_iterator it(line.begin(), line.end(), call), end; it != end; ++it) { const std::string target = (*it)[1].str(); if (target == "if" || target == "for" || target == "while" || target == "function" || target == "require") continue; lua_newtable(L); const int edge = lua_gettop(L); field(L,edge,"from",current); field(L,edge,"to","call:" + target); lua_rawseti(L,callEdges,callIndex++); }
    }
    lua_setfield(L, out, "edges");
    int sourceLines = 1;
    for (char c : source) if (c == '\n') ++sourceLines;
    field(L, out, "lineCount", sourceLines);
    return 1;
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
