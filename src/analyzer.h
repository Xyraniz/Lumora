#ifndef LUMORA_ANALYZER_H
#define LUMORA_ANALYZER_H

#include <string>

struct AnalyzerOptions {
    std::string input;
    std::string outputDir;
    bool json = false;
};

int runAnalyzerCommand(const std::string& command, const AnalyzerOptions& options);
void printAnalyzerHelp();

#endif
