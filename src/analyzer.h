#ifndef LUMORA_ANALYZER_H
#define LUMORA_ANALYZER_H

#include <string>

struct AnalyzerOptions {
    std::string input;
    std::string outputDir;
    bool json = false;
    bool deterministic = false;
    bool traceGlobals = false;
    bool traceIndexes = false;
    bool traceCalls = false;
    bool traceVm = false;
    std::size_t instructionLimit = 100000;
    std::size_t eventLimit = 10000;
    std::size_t outputLimit = 4 * 1024 * 1024;
    std::string fixtures;
};

int runAnalyzerCommand(const std::string& command, const AnalyzerOptions& options);
void printAnalyzerHelp();

#endif
