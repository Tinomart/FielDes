/*
libfive: a CAD kernel for modeling with implicit functions
This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include "libfive/step/step_parser.hpp"
#include "libfive/step/step_progress.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace libfive {
namespace step {

bool Value::isNumber() const { return std::holds_alternative<double>(v); }
bool Value::isString() const { return std::holds_alternative<std::string>(v); }
bool Value::isRef() const { return std::holds_alternative<Ref>(v); }
bool Value::isList() const { return std::holds_alternative<ValueList>(v); }

double Value::asNumber(double fallback) const
{
    return isNumber() ? std::get<double>(v) : fallback;
}
std::string Value::asString() const
{
    return isString() ? std::get<std::string>(v) : std::string();
}
int Value::asRef(int fallback) const
{
    return isRef() ? std::get<Ref>(v).id : fallback;
}
static const ValueList EMPTY_LIST;
const ValueList& Value::asList() const
{
    return isList() ? std::get<ValueList>(v) : EMPTY_LIST;
}

const ValueList* Entity::find(const std::string& keyword) const
{
    auto itr = aspects.find(keyword);
    return itr == aspects.end() ? nullptr : &itr->second;
}

////////////////////////////////////////////////////////////////////////////
// A small hand-rolled parser for STEP Part 21's argument syntax.  This is
// deliberately narrow: it only needs to round-trip the subset of the
// format that appears in real exported files (numbers, quoted strings,
// enumerations, entity references, nested lists, $ / *, and "typed value"
// wrappers like LENGTH_MEASURE(5.)).  It is not a validating parser.

class Reader
{
public:
    Reader(const std::string& s) : s(s), i(0), n(s.size()) {}

    void skipWs()
    {
        for (;;) {
            while (i < n && std::isspace(static_cast<unsigned char>(s[i]))) i++;
            // Skip comments: /* ... */
            if (i + 1 < n && s[i] == '/' && s[i + 1] == '*') {
                i += 2;
                while (i + 1 < n && !(s[i] == '*' && s[i + 1] == '/')) i++;
                i = std::min(n, i + 2);
                continue;
            }
            break;
        }
    }

    bool eof() const { return i >= n; }
    char peek() const { return i < n ? s[i] : '\0'; }

    // Parses one value at the current position.
    Value parseValue()
    {
        skipWs();
        Value out;
        if (eof()) { out.v = Omitted::UNSET; return out; }

        char c = peek();
        if (c == '\'') {
            out.v = parseString();
        } else if (c == '#') {
            i++;
            out.v = Ref{ parseInt() };
        } else if (c == '(') {
            out.v = parseList();
        } else if (c == '$') {
            i++;
            out.v = Omitted::UNSET;
        } else if (c == '*') {
            i++;
            out.v = Omitted::DERIVED;
        } else if (c == '.') {
            // Enumeration/boolean, e.g. .T. or .PLANE_SURFACE.
            i++;
            std::string tok;
            while (i < n && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_')) {
                tok += s[i++];
            }
            if (i < n && s[i] == '.') i++;
            out.v = tok;
        } else if (c == '-' || c == '+' || std::isdigit(static_cast<unsigned char>(c))) {
            out.v = parseNumber();
        } else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            // Identifier -- either a bare keyword (rare outside of entity
            // heads) or a "typed value" wrapper like LENGTH_MEASURE(5.).
            std::string ident;
            while (i < n && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_')) {
                ident += s[i++];
            }
            skipWs();
            if (i < n && s[i] == '(') {
                Value inner;
                inner.v = parseList();
                const auto& list = inner.asList();
                out = list.size() == 1 ? list[0] : inner;
            } else {
                out.v = ident;
            }
        } else {
            // Unrecognized character; skip it so we don't spin forever.
            i++;
            out.v = Omitted::UNSET;
        }
        return out;
    }

    std::string parseString()
    {
        std::string out;
        i++;  // opening quote
        while (i < n) {
            if (s[i] == '\'') {
                if (i + 1 < n && s[i + 1] == '\'') { out += '\''; i += 2; continue; }
                i++;
                break;
            }
            out += s[i++];
        }
        return out;
    }

    int parseInt()
    {
        std::string tok;
        while (i < n && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '-' || s[i] == '+')) {
            tok += s[i++];
        }
        return tok.empty() ? -1 : std::atoi(tok.c_str());
    }

    double parseNumber()
    {
        std::string tok;
        while (i < n) {
            char ch = s[i];
            if (std::isdigit(static_cast<unsigned char>(ch)) || ch == '.' || ch == '-' ||
                ch == '+' || ch == 'e' || ch == 'E') {
                tok += ch;
                i++;
            } else {
                break;
            }
        }
        return tok.empty() ? 0.0 : std::atof(tok.c_str());
    }

    ValueList parseList()
    {
        ValueList out;
        i++;  // opening (
        skipWs();
        if (i < n && s[i] == ')') { i++; return out; }
        for (;;) {
            out.push_back(parseValue());
            skipWs();
            if (i < n && s[i] == ',') { i++; continue; }
            if (i < n && s[i] == ')') { i++; }
            break;
        }
        return out;
    }

    // Parses one KEYWORD(args) pair (no trailing ';' consumed).
    bool parseKeywordArgs(std::string& keyword, ValueList& args)
    {
        skipWs();
        keyword.clear();
        while (i < n && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_')) {
            keyword += s[i++];
        }
        skipWs();
        if (i >= n || s[i] != '(') return !keyword.empty();
        args = parseList();
        return true;
    }

private:
    const std::string& s;
    size_t i, n;
};

////////////////////////////////////////////////////////////////////////////

bool Document::load(const std::string& path, std::string& error)
{
    angleFactorCached = false;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "Could not open file";
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string text = ss.str();

    // Narrow to the DATA...ENDSEC section if present; otherwise scan the
    // whole file (harmless, since only "#N=..." records are recognized).
    auto dataPos = text.find("DATA;");
    size_t start = (dataPos == std::string::npos) ? 0 : dataPos + 5;

    size_t i = start;
    const size_t n = text.size();
    size_t parsedCount = 0;
    while (i < n) {
        if ((++parsedCount & 1023) == 0)
            progress::setRead(double(i - start) / double(std::max<size_t>(1, n - start)));
        // Skip to the next entity head ("#<digits>=")
        while (i < n && text[i] != '#') i++;
        if (i >= n) break;
        size_t idStart = ++i;
        while (i < n && std::isdigit(static_cast<unsigned char>(text[i]))) i++;
        if (i == idStart) continue;  // stray '#'
        int id = std::atoi(text.substr(idStart, i - idStart).c_str());

        while (i < n && std::isspace(static_cast<unsigned char>(text[i]))) i++;
        if (i >= n || text[i] != '=') continue;
        i++;

        while (i < n && std::isspace(static_cast<unsigned char>(text[i]))) i++;
        if (i >= n) break;

        Entity e;
        e.id = id;

        if (text[i] == '(') {
            // Complex entity: a run of KEYWORD(args) pairs inside one
            // extra pair of parens, e.g. "(A()B(1,2)C());"
            i++;  // opening (
            // Complex-entity bodies are parsed by hand (rather than via
            // Reader) since Reader owns its own cursor over the whole
            // string and we need to walk this nested region directly.
            size_t sub = i;
            while (sub < n && text[sub] != ')' ) {
                std::string keyword;
                while (sub < n && (std::isalnum(static_cast<unsigned char>(text[sub])) || text[sub] == '_')) {
                    keyword += text[sub++];
                }
                while (sub < n && std::isspace(static_cast<unsigned char>(text[sub]))) sub++;
                if (sub < n && text[sub] == '(') {
                    // Find the matching close paren for this argument list
                    // by depth counting (respecting quoted strings).
                    size_t argStart = sub;
                    int depth = 0;
                    bool inStr = false;
                    size_t k = sub;
                    for (; k < n; k++) {
                        char c = text[k];
                        if (inStr) {
                            if (c == '\'' && !(k + 1 < n && text[k + 1] == '\'')) inStr = false;
                            else if (c == '\'') k++;  // escaped quote
                            continue;
                        }
                        if (c == '\'') { inStr = true; continue; }
                        if (c == '(') depth++;
                        else if (c == ')') { depth--; if (depth == 0) { k++; break; } }
                    }
                    std::string argText = text.substr(argStart, k - argStart);
                    Reader ar(argText);
                    ValueList args = ar.parseList();
                    if (!keyword.empty()) {
                        e.aspects[keyword] = std::move(args);
                        if (e.type.empty()) e.type = keyword;
                    }
                    sub = k;
                } else if (!keyword.empty()) {
                    e.aspects[keyword] = {};
                    if (e.type.empty()) e.type = keyword;
                }
                while (sub < n && std::isspace(static_cast<unsigned char>(text[sub]))) sub++;
            }
            i = (sub < n) ? sub + 1 : sub;  // past the outer ')'
        } else {
            // Simple entity: KEYWORD(args)
            std::string keyword;
            while (i < n && (std::isalnum(static_cast<unsigned char>(text[i])) || text[i] == '_')) {
                keyword += text[i++];
            }
            while (i < n && std::isspace(static_cast<unsigned char>(text[i]))) i++;
            if (i < n && text[i] == '(') {
                size_t argStart = i;
                int depth = 0;
                bool inStr = false;
                size_t k = i;
                for (; k < n; k++) {
                    char c = text[k];
                    if (inStr) {
                        if (c == '\'' && !(k + 1 < n && text[k + 1] == '\'')) inStr = false;
                        else if (c == '\'') k++;
                        continue;
                    }
                    if (c == '\'') { inStr = true; continue; }
                    if (c == '(') depth++;
                    else if (c == ')') { depth--; if (depth == 0) { k++; break; } }
                }
                std::string argText = text.substr(argStart, k - argStart);
                Reader ar(argText);
                e.aspects[keyword] = ar.parseList();
                e.type = keyword;
                i = k;
            }
        }

        if (!e.type.empty() || !e.aspects.empty()) {
            entities[id] = std::move(e);
        }

        // Advance past the terminating ';' if present
        while (i < n && text[i] != ';' && text[i] != '#') i++;
        if (i < n && text[i] == ';') i++;
    }

    return true;
}

const Entity* Document::get(int id) const
{
    auto itr = entities.find(id);
    return itr == entities.end() ? nullptr : &itr->second;
}

std::vector<const Entity*> Document::ofType(const std::string& type) const
{
    std::vector<const Entity*> out;
    for (auto& [id, e] : entities) {
        if (e.type == type || e.aspects.count(type)) {
            out.push_back(&e);
        }
    }
    return out;
}

double Document::planeAngleFactor() const
{
    if (angleFactorCached) return angleFactor;
    angleFactorCached = true;
    angleFactor = 1.0;
    // The plane-angle unit in force is whichever PLANE_ANGLE_UNIT a
    // representation context's GLOBAL_UNIT_ASSIGNED_CONTEXT lists: plain
    // SI_UNIT(.RADIAN.) (factor 1), or a CONVERSION_BASED_UNIT such as
    // 'DEGREE' whose factor is a PLANE_ANGLE_MEASURE_WITH_UNIT(value, #rad).
    for (const Entity* ctx : ofType("GLOBAL_UNIT_ASSIGNED_CONTEXT")) {
        const ValueList* units = ctx->find("GLOBAL_UNIT_ASSIGNED_CONTEXT");
        if (!units || units->empty() || !(*units)[0].isList()) continue;
        for (const Value& u : (*units)[0].asList()) {
            const Entity* ue = get(u.asRef());
            if (!ue || !ue->find("PLANE_ANGLE_UNIT")) continue;
            const ValueList* conv = ue->find("CONVERSION_BASED_UNIT");
            if (!conv) return angleFactor;  // SI radian
            if (conv->size() < 2) continue;
            const Entity* me = get((*conv)[1].asRef());
            if (!me) continue;
            const ValueList* margs = me->find("PLANE_ANGLE_MEASURE_WITH_UNIT");
            if (!margs || margs->empty() || !(*margs)[0].isNumber()) continue;
            double f = (*margs)[0].asNumber();
            if (f > 0.0) angleFactor = f;
            return angleFactor;
        }
    }
    return angleFactor;
}

}  // namespace step
}  // namespace libfive
