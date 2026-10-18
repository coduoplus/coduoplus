// competitive_spec.cpp - see competitive_spec.h. No platform headers: the Windows mod
// and the Linux server library compile this very file.

#include "shared/competitive_spec.h"

namespace patches {

namespace {

bool token_ok(const std::string& t) {
    if (t.empty() || t.size() > 31) return false;
    for (char c : t) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                        c == '_' || c == '.' || c == '-' || c == '+';
        if (!ok) return false;
    }
    return true;
}

}  // namespace

std::string competitive_build_spec(const std::string& text, std::vector<std::string>* rejected) {
    std::string spec;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(pos, e - pos);
        pos = e + 1;
        // tokens separated by blanks; a comment is a WHOLE line (upstream: an inline
        // comment would become part of the value)
        std::vector<std::string> tok;
        size_t i = 0;
        while (i < line.size()) {
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) ++i;
            size_t j = i;
            while (j < line.size() && line[j] != ' ' && line[j] != '\t' && line[j] != '\r') ++j;
            if (j > i) tok.push_back(line.substr(i, j - i));
            i = j;
        }
        if (tok.empty() || tok[0][0] == '#' || tok[0][0] == ';' || tok[0][0] == '/') continue;
        bool ok = (tok.size() == 2 || tok.size() == 3);
        for (const std::string& t : tok) ok = ok && token_ok(t);
        if (!ok) {
            if (rejected) rejected->push_back(line);
            continue;
        }
        std::string entry = tok[0] + "=" + tok[1];
        if (tok.size() == 3) entry += ":" + tok[2];
        if (!spec.empty()) spec += " ";
        spec += entry;
    }
    return spec;
}

bool competitive_chunk_spec(const std::string& spec, std::vector<std::string>& parts) {
    parts.assign(COMP_SV_CHUNKS, std::string());
    size_t p = 0;
    for (int i = 0; i < COMP_SV_CHUNKS && p < spec.size(); ++i) {
        size_t len = spec.size() - p;
        if (len > (size_t)COMP_SV_CHUNK_MAX) {
            len = COMP_SV_CHUNK_MAX;
            while (len > 0 && spec[p + len] != ' ') --len;   // cut before an entry
            if (len == 0) return false;                       // one entry > chunk: refuse
        }
        parts[i] = spec.substr(p, len);
        p += len;
        while (p < spec.size() && spec[p] == ' ') ++p;
    }
    return p >= spec.size();
}

std::string competitive_chunk_name(int i) {
    return i == 0 ? std::string(COMP_SPEC_CVAR) : std::string(COMP_SPEC_CVAR) + std::to_string(i + 1);
}

}  // namespace patches
