#include "lumora.h"

#include <string>
#include <vector>

// Mirrors the official Luau CLI's normalizePath() (CLI/src/FileUtils.cpp).
// Both runtimes must render the same chunk name for the same script path so
// runtime errors, stack traces and debug.info() locations agree. Notably a
// relative path such as "main.lua" is normalized to "./main.lua", "./" is
// resolved away, and a leading ".." is preserved.
static std::vector<std::string_view> splitPath(std::string_view path)
{
    // Same splitting rules as the reference implementation: both '/' and '\'
    // are separators, and empty components are kept (the empty component at
    // index 0 of an absolute path is what re-materializes the leading '/').
    std::vector<std::string_view> components;
    size_t pos = 0;
    size_t nextPos = path.find_first_of("\\/", pos);
    while (nextPos != std::string_view::npos)
    {
        components.push_back(path.substr(pos, nextPos - pos));
        pos = nextPos + 1;
        nextPos = path.find_first_of("\\/", pos);
    }
    components.push_back(path.substr(pos));
    return components;
}

static bool isAbsolutePath(std::string_view path)
{
    // Luau's CLI treats both POSIX paths and Windows drive-qualified paths as
    // absolute.  Keeping the drive prefix here is important for require and
    // for error chunk names when Lumora is invoked from CMake/PowerShell.
    return (!path.empty() && (path[0] == '/' || path[0] == '\\')) ||
           (path.size() >= 3 && ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
            path[1] == ':' && (path[2] == '/' || path[2] == '\\'));
}

std::string normalizeChunkPath(std::string_view path)
{
    const std::vector<std::string_view> components = splitPath(path);
    std::vector<std::string_view> normalizedComponents;

    const bool isAbsolute = isAbsolutePath(path);

    // 1. Normalize path components (drop "." and empty, resolve "..")
    // For POSIX paths component 0 is the empty segment before '/', while for
    // Windows it is the drive prefix (for example, "C:").  In both cases it
    // belongs to the output prefix and must not be joined a second time.
    const size_t startIndex = isAbsolute ? 1 : 0;
    for (size_t i = startIndex; i < components.size(); i++)
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

    // 2. Add correct prefix to formatted path
    if (isAbsolute)
    {
        normalizedPath += components[0];
        normalizedPath += "/";
    }
    else if (normalizedComponents.empty() || normalizedComponents[0] != "..")
    {
        normalizedPath += "./";
    }

    // 3. Join path components to form the normalized path
    for (size_t i = 0; i < normalizedComponents.size(); i++)
    {
        if (i != 0)
            normalizedPath += "/";
        normalizedPath += normalizedComponents[i];
    }
    if (normalizedPath.size() >= 2 && normalizedPath[normalizedPath.size() - 1] == '.' && normalizedPath[normalizedPath.size() - 2] == '.')
        normalizedPath += "/";

    return normalizedPath;
}
