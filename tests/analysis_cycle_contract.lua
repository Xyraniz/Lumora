local analysis = require("@lumora/analysis")
local graph = analysis.graph("@file:../tests/analysis_cycle_a.luau")
assert(graph.moduleCount == 2, "global graph should include both modules")
assert(graph.edgeCount == 2, "global graph should include both dependency edges")
assert(graph.hasCycle == true, "DFS should detect the circular dependency")
assert(#graph.cycles >= 1, "cycle path should be returned")
local cycle = graph.cycles[1]
assert(cycle[1] == cycle[#cycle], "cycle path should close at its starting module")
print("analysis-cycle-contract-ok")
