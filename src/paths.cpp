#include "lumora.h"

#include <string>
#include <vector>

// Mirrors the official Luau CLI's normalizePath() (CLI/src/FileUtils.cpp).
// Both runtimes must render the same chunk name for the same script path so
// runtime errors, stack traces and debug.info() locations agree. Notably a
// relative path such as "main.lua" is normalized to "./main.lua" and "./" is
// resolved away, but a leading "../" is preserved.
static void splitPath(std::string_view path, std::vector<std::string_view>& out)
{
    size_t start = 0;
    for (size_t i = 0; i <= path.size(); ++i)
    {
        if (i == path.size() || path[i] == '/')
        {
            if (i > start)
                out.push_back(path.substr(start, i - start));
            start = i + 1;
        }
    }
}

static bool isAbsolutePath(std::string_view path)
{
    return !path.empty() && path[0] == '/';
}

std::string normalizeChunkPath(std::string_view path)
{
    std::vector<std::string_view> components;
    splitPath(path, components);
    std::vector<std::string_view> normalizedComponents;

    const bool isAbsolute = isAbsolutePath(path);

    for (size_t i = isAbsolute ? 1 : 0; i < components.size(); ++i)
    {
        const std::string_view component = components[i];
        if (component == "..")
        {
            if (normalizedComponents.empty())
            {
                if (!isAbsolute)
                    normalizedComponents.emplace_back("..");
            }
            else if (normalizedComponents.back() == "..")
            {
                normalizedComponents.emplace_back("..");
            }
            else
            {
                normalizedComponents.pop_back();
            }
        }
        else if (!component.empty() && component != ".")
        {
            normalizedComponents.emplace_back(component);
        }
    }

    std::string normalizedPath;
    if (isAbsolute)
    {
        normalizedPath += components[0];
        normalizedPath += "/";
    }
    else if (normalizedComponents.empty() || normalizedComponents[0] != "..")
    {
        normalizedPath += "./";
    }

    for (size_t i = 0; i < normalizedComponents.size(); ++i)
    {
        if (i != 0)
            normalizedPath += "/";
        normalizedPath += normalizedComponents[i];
    }
    if (normalizedPath.size() >= 2 && normalizedPath[normalizedPath.size() - 1] == '.' && normalizedPath[normalizedPath.size() - 2] == '.')
        normalizedPath += "/";

    return normalizedPath;
}
