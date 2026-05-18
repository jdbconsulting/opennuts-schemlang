// main.cpp -- schemlang CLI entry point.
//
// Usage:
//   schemlang --tokens FILE   print the tokenized form of FILE
//   schemlang --check  FILE   parse FILE and report any syntax errors
//
// The two modes share the same preprocessor + lexer + layout passes; only
// the final step (print vs parse) differs.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "diagnostics.hpp"
#include "driver.hpp"
#include "lsp_server.hpp"
#include "tokens.hpp"

namespace {

void print_usage(std::FILE* out) {
    std::fputs(
        "schemlang -- syntax checker for the Schemlang DSL\n"
        "\n"
        "Usage:\n"
        "  schemlang --tokens FILE     print the tokenized form of FILE\n"
        "  schemlang --check  FILE     parse FILE and report syntax errors\n"
        "  schemlang --lsp             run as a Language Server Protocol\n"
        "                              server, reading/writing JSON-RPC on\n"
        "                              stdin/stdout\n"
        "  schemlang --help            show this help\n"
        "\n"
        "FILE is a path to a .schemlang source file. With no errors, --check\n"
        "exits 0 and prints \"OK\".\n",
        out);
}

bool read_file(std::string_view path, std::string& out) {
    std::ifstream in{std::string(path), std::ios::binary};
    if (!in) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

void print_tokens(std::string_view /*filename*/,
                  const std::vector<schemlang::Token>& tokens) {
    for (auto const& t : tokens) {
        std::string lexeme;
        switch (t.kind) {
            case schemlang::TokenKind::Newline:    lexeme = "<NEWLINE>"; break;
            case schemlang::TokenKind::Indent:     lexeme = "<INDENT>";  break;
            case schemlang::TokenKind::Dedent:     lexeme = "<DEDENT>";  break;
            case schemlang::TokenKind::EndOfFile:  lexeme = "<EOF>";     break;
            case schemlang::TokenKind::StringLit:  {
                lexeme.reserve(t.text.size() + 2);
                lexeme.push_back('"');
                for (char c : t.text) {
                    switch (c) {
                        case '"':  lexeme += "\\\""; break;
                        case '\\': lexeme += "\\\\"; break;
                        case '\n': lexeme += "\\n";  break;
                        case '\r': lexeme += "\\r";  break;
                        case '\t': lexeme += "\\t";  break;
                        default:   lexeme.push_back(c);
                    }
                }
                lexeme.push_back('"');
                break;
            }
            default:                               lexeme = t.text; break;
        }
        std::printf("%-22s %4u:%-4u  %s\n",
                    schemlang::token_kind_name(t.kind),
                    t.pos.line, t.pos.column,
                    lexeme.c_str());
    }
}

enum class Mode { None, Tokens, Check, Lsp };

} // namespace

int main(int argc, char** argv) {
    Mode mode = Mode::None;
    std::string_view filename;

    for (int i = 1; i < argc; ++i) {
        std::string_view a{argv[i]};
        if (a == "--help" || a == "-h") {
            print_usage(stdout);
            return 0;
        }
        if (a == "--lsp") {
            mode = Mode::Lsp;
        } else if (a == "--tokens" && i + 1 < argc) {
            mode = Mode::Tokens;
            filename = argv[++i];
        } else if (a == "--check" && i + 1 < argc) {
            mode = Mode::Check;
            filename = argv[++i];
        } else if (a.starts_with("--tokens=")) {
            mode = Mode::Tokens;
            filename = a.substr(std::strlen("--tokens="));
        } else if (a.starts_with("--check=")) {
            mode = Mode::Check;
            filename = a.substr(std::strlen("--check="));
        } else if (filename.empty() && !a.starts_with("--")) {
            // Allow positional file with implicit --check.
            mode = (mode == Mode::None) ? Mode::Check : mode;
            filename = a;
        } else {
            std::fprintf(stderr, "schemlang: unknown argument '%.*s'\n",
                         static_cast<int>(a.size()), a.data());
            print_usage(stderr);
            return 2;
        }
    }

    if (mode == Mode::Lsp) {
        return schemlang::run_lsp_server();
    }

    if (mode == Mode::None || filename.empty()) {
        print_usage(stderr);
        return 2;
    }

    std::string source;
    if (!read_file(filename, source)) {
        std::fprintf(stderr, "schemlang: cannot read '%.*s': %s\n",
                     static_cast<int>(filename.size()), filename.data(),
                     std::strerror(errno));
        return 1;
    }

    if (mode == Mode::Tokens) {
        auto out = schemlang::tokenize_source(filename, source);
        print_tokens(filename, out.tokens);
        if (!out.ok) {
            schemlang::DiagnosticBag bag;
            for (auto const& d : out.diagnostics) {
                if (d.level == schemlang::Diagnostic::Level::Error)        bag.error(d.filename, d.pos, d.message);
                else if (d.level == schemlang::Diagnostic::Level::Warning) bag.warning(d.filename, d.pos, d.message);
                else                                                       bag.note(d.filename, d.pos, d.message);
            }
            bag.print(source);
            return 1;
        }
        return 0;
    }

    // --check
    auto out = schemlang::check_source(filename, source);
    if (!out.ok) {
        schemlang::DiagnosticBag bag;
        for (auto const& d : out.diagnostics) {
            if (d.level == schemlang::Diagnostic::Level::Error)        bag.error(d.filename, d.pos, d.message);
            else if (d.level == schemlang::Diagnostic::Level::Warning) bag.warning(d.filename, d.pos, d.message);
            else                                                       bag.note(d.filename, d.pos, d.message);
        }
        bag.print(source);
        std::fprintf(stderr, "%zu error(s)\n", bag.error_count());
        return 1;
    }
    std::printf("OK\n");
    return 0;
}
