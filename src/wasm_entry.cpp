// wasm_entry.cpp -- Emscripten / Embind bindings for the schemlang core.
//
// Compiled only under emscripten. Exposes two functions to JavaScript:
//
//   schemlang.check(source: string, filename?: string)
//       -> { ok: bool,
//            diagnostics: [{ severity: int, line: int, column: int,
//                            endLine: int, endColumn: int, message: string }] }
//
//   schemlang.tokenize(source: string, filename?: string)
//       -> { ok: bool,
//            tokens: [{ kind: string, text: string, line: int, column: int }],
//            diagnostics: [...] }
//
// Severity is the LSP convention: 1 = Error, 2 = Warning, 3 = Information.

#if defined(__EMSCRIPTEN__)

#include <string>
#include <string_view>

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include "driver.hpp"
#include "tokens.hpp"

namespace em = emscripten;

namespace {

int severity_int(schemlang::Diagnostic::Level lvl) {
    switch (lvl) {
        case schemlang::Diagnostic::Level::Error:   return 1;
        case schemlang::Diagnostic::Level::Warning: return 2;
        case schemlang::Diagnostic::Level::Note:    return 3;
    }
    return 1;
}

em::val diagnostic_to_val(schemlang::Diagnostic const& d) {
    em::val o = em::val::object();
    o.set("severity",  severity_int(d.level));
    o.set("line",      d.pos.line);
    o.set("column",    d.pos.column);
    o.set("endLine",   d.pos.line);
    o.set("endColumn", d.pos.column + 1);
    o.set("message",   d.message);
    o.set("file",      d.filename);
    return o;
}

em::val diagnostics_to_array(const std::vector<schemlang::Diagnostic>& diags) {
    em::val arr = em::val::array();
    for (std::size_t i = 0; i < diags.size(); ++i) {
        arr.set(static_cast<unsigned>(i), diagnostic_to_val(diags[i]));
    }
    return arr;
}

em::val js_check(std::string source, std::string filename) {
    if (filename.empty()) filename = "<input>";
    auto out = schemlang::check_source(filename, source);
    em::val obj = em::val::object();
    obj.set("ok",          out.ok);
    obj.set("diagnostics", diagnostics_to_array(out.diagnostics));
    return obj;
}

em::val js_tokenize(std::string source, std::string filename) {
    if (filename.empty()) filename = "<input>";
    auto out = schemlang::tokenize_source(filename, source);
    em::val arr = em::val::array();
    for (std::size_t i = 0; i < out.tokens.size(); ++i) {
        auto const& t = out.tokens[i];
        em::val o = em::val::object();
        o.set("kind",   schemlang::token_kind_name(t.kind));
        o.set("text",   t.text);
        o.set("line",   t.pos.line);
        o.set("column", t.pos.column);
        arr.set(static_cast<unsigned>(i), o);
    }
    em::val obj = em::val::object();
    obj.set("ok",          out.ok);
    obj.set("tokens",      arr);
    obj.set("diagnostics", diagnostics_to_array(out.diagnostics));
    return obj;
}

} // namespace

EMSCRIPTEN_BINDINGS(schemlang) {
    em::function("check",    &js_check);
    em::function("tokenize", &js_tokenize);
}

#endif // __EMSCRIPTEN__
