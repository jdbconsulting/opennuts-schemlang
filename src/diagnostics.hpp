// diagnostics.hpp -- error reporting machinery.

#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "tokens.hpp"

namespace schemlang {

struct Diagnostic {
    enum class Level { Error, Warning, Note };
    Level       level = Level::Error;
    std::string filename;
    SourcePos   pos;
    std::string message;
};

// A bundle of diagnostics returned by every phase. Empty errors() means
// "phase succeeded".
class DiagnosticBag {
public:
    void error(std::string filename, SourcePos pos, std::string message);
    void warning(std::string filename, SourcePos pos, std::string message);
    void note(std::string filename, SourcePos pos, std::string message);

    bool has_errors() const noexcept { return error_count_ > 0; }
    std::size_t error_count() const noexcept { return error_count_; }
    const std::vector<Diagnostic>& items() const noexcept { return items_; }

    // Pretty-print all diagnostics to stderr. Uses the (optional) `source`
    // text to render the offending line and a caret marker.
    void print(std::string_view source) const;

private:
    std::vector<Diagnostic> items_;
    std::size_t             error_count_ = 0;
};

// A parser exception used to unwind to the nearest recovery point.
class ParseError : public std::runtime_error {
public:
    ParseError(SourcePos pos, std::string what)
        : std::runtime_error(std::move(what)), pos_(pos) {}
    SourcePos where() const noexcept { return pos_; }

private:
    SourcePos pos_;
};

} // namespace schemlang
