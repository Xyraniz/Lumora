#include "lua.h"
#include "lualib.h"
#include "Luau/Compiler.h"
#include "lumora.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <limits>
#include <cmath>
#if defined(__unix__)
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

int main(int argc, char** argv)
{
    bool roblox = true, json = false, sandbox = false;
    double timeout = 0.0;
    std::vector<char*> scriptArgs;
    const char* script = nullptr;
    std::string parseError;
    for (int i = 1; i < argc; ++i)
    {
        std::string option = argv[i];
        if (option == "--no-roblox") roblox = false;
        else if (option == "--json") json = true;
        else if (option == "--sandbox") sandbox = true;
        else if (option == "--help" || option == "-h")
        {
            std::cout << "usage: lumora [--no-roblox] [--json] [--sandbox] [--timeout seconds] script.lua [args...]\n";
            return 0;
        }
        else if (option == "--version")
        {
            std::cout << "lumora 0.1.0\n";
            return 0;
        }
        else if (option == "--timeout" && i + 1 < argc)
        {
            try
            {
                timeout = std::stod(argv[++i]);
                if (!std::isfinite(timeout) || timeout < 0.0) throw std::invalid_argument("timeout must be a finite non-negative number");
            }
            catch (const std::exception& error) { parseError = error.what(); break; }
        }
        else if (!script && option.rfind("--", 0) != 0) script = argv[i];
        else if (script) scriptArgs.push_back(argv[i]);
        else { parseError = "unknown option: " + option; break; }
    }
    if (!parseError.empty())
    {
        std::cerr << parseError << "\n";
        return 2;
    }
    if (!script)
    {
        std::cerr << "usage: lumora [--no-roblox] [--json] [--timeout seconds] script.lua [args...]\n";
        return 2;
    }

    // Emit a single-level JSON result with an enriched schema. Every error
    // path (missing file, compile error, runtime error, timeout, signal) uses
    // this same function so consumers always get a flat, predictable object.
    auto emitJson = [](const char* kind, bool ok, const std::string& stdoutText,
                       const std::string& stderrText, const std::string& message,
                       int exitCode, double durationMs, bool timedOut, const char* scriptPath) {
        std::cout << "{\"kind\":" << jsonEscape(kind ? kind : "unknown")
                  << ",\"ok\":" << (ok ? "true" : "false")
                  << ",\"stdout\":" << jsonEscape(stdoutText)
                  << ",\"stderr\":" << jsonEscape(stderrText)
                  << ",\"message\":" << jsonEscape(message)
                  << ",\"exitCode\":" << exitCode
                  << ",\"durationMs\":" << (long long)(durationMs + 0.5)
                  << ",\"timedOut\":" << (timedOut ? "true" : "false")
                  << ",\"script\":" << jsonEscape(scriptPath ? scriptPath : "")
                  << "}\n";
    };

    const auto jsonStart = std::chrono::steady_clock::now();

    // Detect a missing script file before forking so the error is reported at
    // the parent level (single JSON level) rather than being wrapped by the
    // child's own output.
    if (json)
    {
        std::ifstream probe(script);
        if (!probe)
        {
            const std::string msg = std::string("cannot open script: ") + script;
            emitJson("load-error", false, "", "", msg, 2, 0.0, false, script);
            return 2;
        }
    }

    try
    {
#if defined(__unix__)
        if (json)
        {
            const std::string outPath = "/tmp/lumora-" + std::to_string(getpid()) + ".out";
            const std::string errPath = "/tmp/lumora-" + std::to_string(getpid()) + ".err";
            pid_t child = fork();
            if (child == 0)
            {
                // Child: run the script WITHOUT producing JSON. Only the parent
                // assembles the final JSON envelope, so there is never nested
                // JSON. Redirect stdout/stderr to temp files and exit with the
                // script's own exit code.
                FILE* out = fopen(outPath.c_str(), "w"); FILE* err = fopen(errPath.c_str(), "w");
                if (out) { dup2(fileno(out), STDOUT_FILENO); fclose(out); }
                if (err) { dup2(fileno(err), STDERR_FILENO); fclose(err); }
                std::vector<char*> args; args.push_back(argv[0]); args.push_back(const_cast<char*>(script));
                for (char* a : scriptArgs) args.push_back(a);
                int rc = runScript(script, int(args.size()), args.data(), roblox, sandbox, timeout);
                std::cout.flush(); std::cerr.flush(); _exit(rc);
            }
            int status = 0; bool killed = false;
            while (waitpid(child, &status, WNOHANG) == 0)
            {
                if (timeout > 0 && std::chrono::duration<double>(std::chrono::steady_clock::now() - jsonStart).count() > timeout + 1.0)
                { kill(child, SIGKILL); killed = true; waitpid(child, &status, 0); break; }
                usleep(10000);
            }
            auto readText = [](const std::string& path) { std::ifstream f(path); std::ostringstream s; s << f.rdbuf(); return s.str(); };
            const std::string stdoutText = readText(outPath), stderrText = readText(errPath);
            std::remove(outPath.c_str()); std::remove(errPath.c_str());
            int code = killed ? 124 : (WIFEXITED(status) ? WEXITSTATUS(status) : 1);
            const double durationMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - jsonStart).count();
            // A cooperative Luau timeout surfaces as a script error whose
            // message starts with "execution timeout". Detect it so consumers
            // can distinguish a genuine timeout from an ordinary script error.
            const bool cooperativelyTimedOut = !killed && code != 0 &&
                stderrText.rfind("execution timeout", 0) == 0;
            const char* kind = killed ? "timeout" : (code == 0 ? "success" : "script-error");
            emitJson(kind, code == 0, stdoutText, stderrText,
                     code == 0 ? "" : stderrText, code, durationMs,
                     killed || cooperativelyTimedOut, script);
            return code;
        }
#endif
        std::vector<char*> args; args.push_back(argv[0]); args.push_back(const_cast<char*>(script));
        for (char* a : scriptArgs) args.push_back(a);
        return runScript(script, int(args.size()), args.data(), roblox, sandbox, timeout);
    }
    catch (const std::exception& error)
    {
        if (json)
        {
            const double durationMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - jsonStart).count();
            emitJson("invocation-error", false, "", "", error.what(), 2, durationMs, false, script);
        }
        else std::cerr << error.what() << "\n";
        return 2;
    }
}
