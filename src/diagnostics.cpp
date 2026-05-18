#include "diagnostics.hpp"

#include <algorithm>
#include <cstdio>

namespace schemlang {

void DiagnosticBag::error(std::string filename, SourcePos pos, std::string message) {
    items_.push_back({Diagnostic::Level::Error, std::move(filename), pos, std::move(message)});
    ++error_count_;
}

void DiagnosticBag::warning(std::string filename, SourcePos pos, std::string message) {
    items_.push_back({Diagnostic::Level::Warning, std::move(filename), pos, std::move(message)});
}

void DiagnosticBag::note(std::string filename, SourcePos pos, std::string message) {
    items_.push_back({Diagnostic::Level::Note, std::move(filename), pos, std::move(message)});
}

static const char* level_label(Diagnostic::Level lvl) {
    switch (lvl) {
        case Diagnostic::Level::Error:   return "error";
        case Diagnostic::Level::Warning: return "warning";
        case Diagnostic::Level::Note:    return "note";
    }
    return "?";
}

// Return the substring of `source` covering the 1-based line number, with
// no trailing newline. Returns empty if the line is out of range.
static std::string_view line_text(std::string_view source, std::uint32_t line_no) {
    if (line_no == 0) return {};
    std::size_t start = 0;
    std::uint32_t current = 1;
    while (current < line_no && start < source.size()) {
        auto nl = source.find('\n', start);
        if (nl == std::string_view::npos) return {};
        start = nl + 1;
        ++current;
    }
    if (current != line_no) return {};
    auto nl = source.find('\n', start);
    auto end = (nl == std::string_view::npos) ? source.size() : nl;
    return source.substr(start, end - start);
}

void DiagnosticBag::print(std::string_view source) const {
    for (auto const& d : items_) {
        std::fprintf(stderr, "%s:%u:%u: %s: %s\n",
                     d.filename.empty() ? "<input>" : d.filename.c_str(),
                     d.pos.line, d.pos.column,
                     level_label(d.level), d.message.c_str());
        auto line = line_text(source, d.pos.line);
        if (!line.empty()) {
            std::fprintf(stderr, "  %.*s\n",
                         static_cast<int>(line.size()), line.data());
            // Caret line; clamp column to 1..line.size()+1.
            std::uint32_t col = std::max<std::uint32_t>(1, d.pos.column);
            std::fputs("  ", stderr);
            for (std::uint32_t i = 1; i < col; ++i) std::fputc(' ', stderr);
            std::fputc('^', stderr);
            std::fputc('\n', stderr);
        }
    }
}

} // namespace schemlang
