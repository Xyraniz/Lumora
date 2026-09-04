#include "lua.h"
#include "lualib.h"
#include "Luau/Compiler.h"
#include "lumora.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <limits>
#include <cmath>
#if defined(__unix__)
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#else
#include <ctime>
#endif

// Lumora semantic version — keep in sync with CHANGELOG.md.
static constexpr const char* kVersion = "lumora 0.3.0";

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
            std::cout << kVersion << "\n";
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
        std::cerr << "usage: lumora [--no-roblox] [--json] [--sandbox] [--timeout seconds] script.lua [args...]\n";
        return 2;
    }

    // Emit a single-level JSON result with an enriched schema. Every error
    // path (missing file, compile error, runtime error, timeout, signal) uses
    // this same function so consumers always get a flat, predictable object.
    auto emitJson = [](const char* kind, bool ok, const std::string& stdoutText,
                       const std::string& stderrText, const std::string& message,
                       int exitCode, double durationMs, bool timedOut, const char* scriptPath) {
        const std::string traceback = stderrText;
        std::cout << "{\"kind\":" << jsonEscape(kind ? kind : "unknown")
                  << ",\"ok\":" << (ok ? "true" : "false")
                  << ",\"stdout\":" << jsonEscape(stdoutText)
                  << ",\"stderr\":" << jsonEscape(stderrText)
                  << ",\"message\":" << jsonEscape(message)
                  << ",\"traceback\":" << jsonEscape(traceback)
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

    // Build the argv vector once — used by both the fork path and the
    // thread-based fallback path.
    std::vector<char*> runArgs;
    runArgs.push_back(argv[0]);
    runArgs.push_back(const_cast<char*>(script));
    for (char* a : scriptArgs) runArgs.push_back(a);

    try
    {
#if defined(__unix__)
        // ── Unix path: fork + waitpid ───────────────────────────────────
        // True process isolation: the child runs the script with stdout/stderr
        // redirected to temp files, while the parent monitors for cooperative
        // or hard timeout via SIGKILL. This is the preferred JSON capture
        // strategy because it guarantees clean exit-code propagation and
        // prevents a runaway script from corrupting the parent's JSON output.
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
                int rc = runScript(script, int(runArgs.size()), runArgs.data(), roblox, sandbox, timeout);
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
#else
        // ── Non-Unix (Windows) path: thread-based capture ──────────────
        // On platforms without fork(), capture stdout/stderr by redirecting
        // the C FILE* handles to temporary files via freopen, running the
        // script in the current thread, then restoring the original streams.
        // The cooperative timeout interrupt (installed inside runScript)
        // handles timeouts without requiring process-level signals.
        if (json)
        {
            char outPath[L_tmpnam], errPath[L_tmpnam];
            tmpnam(outPath); tmpnam(errPath);
            std::FILE* oldOut = std::freopen(outPath, "w", stdout);
            std::FILE* oldErr = std::freopen(errPath, "w", stderr);
            int code = 0;
            std::string capturedError;
            try
            {
                code = runScript(script, int(runArgs.size()), runArgs.data(), roblox, sandbox, timeout);
            }
            catch (const std::exception& e)
            {
                capturedError = e.what();
                code = 2;
            }
            if (oldOut) std::fflush(stdout);
            if (oldErr) std::fflush(stderr);
            // Restore original streams and read captured output.
            if (oldOut) { std::freopen("CONOUT$", "w", stdout); }
            if (oldErr) { std::freopen("CONOUT$", "w", stderr); }
            auto readText = [](const char* path) { std::ifstream f(path); std::ostringstream s; s << f.rdbuf(); return s.str(); };
            const std::string stdoutText = readText(outPath), stderrText = readText(errPath);
            std::remove(outPath); std::remove(errPath);
            const double durationMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - jsonStart).count();
            const bool cooperativelyTimedOut = code != 0 &&
                stderrText.rfind("execution timeout", 0) == 0;
            const char* kind = (code == 0 ? "success" : (cooperativelyTimedOut ? "timeout" : "script-error"));
            emitJson(kind, code == 0, stdoutText, stderrText,
                     code == 0 ? "" : (capturedError.empty() ? stderrText : capturedError),
                     code, durationMs, cooperativelyTimedOut, script);
            return code;
        }
#endif
        // Non-JSON mode: run directly and return the script's exit code.
        return runScript(script, int(runArgs.size()), runArgs.data(), roblox, sandbox, timeout);
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
