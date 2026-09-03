#include "lumora.h"

#include <cstdio>
#include <string>

std::string jsonEscape(const std::string& value)
{
    std::string out = "\"";
    for (unsigned char c : value)
    {
        if (c == '\\') out += "\\\\";
        else if (c == '"') out += "\\\"";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 32) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); out += b; }
        else out += char(c);
    }
    return out + "\"";
}
