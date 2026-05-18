// lsp_server.cpp -- minimal LSP server implementation.

#include "lsp_server.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#  include <fcntl.h>
#  include <io.h>
#endif

#include <nlohmann/json.hpp>

#include "diagnostics.hpp"
#include "driver.hpp"
#include "tokens.hpp"

namespace schemlang {

using json = nlohmann::json;

namespace {

// ---------------------------------------------------------------------------
// stdin / stdout helpers. We rely on raw byte I/O so the LSP Content-Length
// framing isn't disturbed by any platform line-ending translation.
// ---------------------------------------------------------------------------

void configure_binary_io() {
#if defined(_WIN32)
    _setmode(_fileno(stdin),  _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
}

bool read_byte(int& out) {
    int c = std::fgetc(stdin);
    if (c == EOF) return false;
    out = c;
    return true;
}

// Read one LSP header line (terminated by CRLF or LF). Returns std::nullopt
// on end-of-stream before reading any bytes.
std::optional<std::string> read_header_line() {
    std::string line;
    int  c;
    bool any = false;
    while (read_byte(c)) {
        any = true;
        if (c == '\n') return line;
        if (c == '\r') {
            int next;
            if (!read_byte(next)) return line;
            if (next == '\n') return line;
            line.push_back(static_cast<char>(c));
            line.push_back(static_cast<char>(next));
            continue;
        }
        line.push_back(static_cast<char>(c));
    }
    return any ? std::optional<std::string>(line) : std::nullopt;
}

// Read N raw bytes from stdin.
bool read_n_bytes(std::size_t n, std::string& out) {
    out.clear();
    out.resize(n);
    std::size_t got = std::fread(out.data(), 1, n, stdin);
    if (got != n) { out.resize(got); return false; }
    return true;
}

std::optional<json> read_message() {
    std::size_t content_length = 0;
    bool        seen_header    = false;
    while (true) {
        auto line = read_header_line();
        if (!line) return std::nullopt;
        if (line->empty()) {
            if (!seen_header) continue;     // tolerate stray empty lines
            break;
        }
        seen_header = true;
        // Header lines are "Name: value".
        auto colon = line->find(':');
        if (colon == std::string::npos) continue;
        std::string name  = line->substr(0, colon);
        std::string value = line->substr(colon + 1);
        // Trim leading spaces from value.
        std::size_t k = 0;
        while (k < value.size() && (value[k] == ' ' || value[k] == '\t')) ++k;
        value.erase(0, k);
        // Case-insensitive compare for "content-length".
        if (name.size() == std::strlen("Content-Length")) {
            bool match = true;
            for (std::size_t i = 0; i < name.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(name[i]))
                    != std::tolower(static_cast<unsigned char>("Content-Length"[i]))) {
                    match = false; break;
                }
            }
            if (match) {
                content_length = static_cast<std::size_t>(std::strtoull(value.c_str(), nullptr, 10));
            }
        }
    }
    if (content_length == 0) return std::nullopt;
    std::string body;
    if (!read_n_bytes(content_length, body)) return std::nullopt;
    try {
        return json::parse(body);
    } catch (json::parse_error const&) {
        return std::nullopt;
    }
}

void write_message(const json& body) {
    std::string serialized = body.dump();
    std::fprintf(stdout, "Content-Length: %zu\r\n\r\n", serialized.size());
    std::fwrite(serialized.data(), 1, serialized.size(), stdout);
    std::fflush(stdout);
}

void log_stderr(std::string_view what) {
    std::fprintf(stderr, "[schemlang-lsp] %.*s\n",
                 static_cast<int>(what.size()), what.data());
}

// ---------------------------------------------------------------------------
// Source position helpers (LSP uses 0-based line/character).
// ---------------------------------------------------------------------------

json lsp_position(std::uint32_t line, std::uint32_t column) {
    // schemlang SourcePos is 1-based; LSP is 0-based.
    std::uint32_t l = (line > 0)   ? line - 1   : 0;
    std::uint32_t c = (column > 0) ? column - 1 : 0;
    return json{{"line", l}, {"character", c}};
}

int lsp_severity_for(Diagnostic::Level lvl) {
    switch (lvl) {
        case Diagnostic::Level::Error:   return 1;
        case Diagnostic::Level::Warning: return 2;
        case Diagnostic::Level::Note:    return 3;       // "Information"
    }
    return 1;
}

// ---------------------------------------------------------------------------
// Server state.
// ---------------------------------------------------------------------------

class LspServer {
public:
    int run() {
        configure_binary_io();
        log_stderr("server started");
        while (!exited_) {
            auto msg = read_message();
            if (!msg) {
                // Stream closed.
                if (!shutdown_requested_) {
                    log_stderr("stdin closed without shutdown");
                    return 1;
                }
                return 0;
            }
            try {
                handle(*msg);
            } catch (std::exception const& e) {
                log_stderr(std::string("exception: ") + e.what());
            }
        }
        return shutdown_requested_ ? 0 : 1;
    }

private:
    bool exited_            = false;
    bool shutdown_requested_= false;
    // uri -> {text, version}
    struct Document { std::string text; int version = 0; };
    std::unordered_map<std::string, Document> docs_;

    // -- I/O helpers -----------------------------------------------------
    void respond(const json& id, json result) {
        write_message(json{
            {"jsonrpc", "2.0"},
            {"id",      id},
            {"result",  std::move(result)},
        });
    }
    void respond_error(const json& id, int code, std::string msg) {
        write_message(json{
            {"jsonrpc", "2.0"},
            {"id",      id},
            {"error",   {{"code", code}, {"message", std::move(msg)}}},
        });
    }
    void notify(std::string method, json params) {
        write_message(json{
            {"jsonrpc", "2.0"},
            {"method",  std::move(method)},
            {"params",  std::move(params)},
        });
    }

    // -- Dispatch --------------------------------------------------------
    void handle(const json& msg) {
        if (!msg.contains("method")) return;
        std::string method = msg.at("method").get<std::string>();
        const json& id     = msg.contains("id") ? msg.at("id") : json();
        const json& params = msg.contains("params") ? msg.at("params") : json::object();

        if (method == "initialize")              { on_initialize(id, params); return; }
        if (method == "initialized")             { /* no-op */ return; }
        if (method == "shutdown")                { on_shutdown(id); return; }
        if (method == "exit")                    { on_exit(); return; }
        if (method == "textDocument/didOpen")    { on_did_open(params); return; }
        if (method == "textDocument/didChange")  { on_did_change(params); return; }
        if (method == "textDocument/didClose")   { on_did_close(params); return; }
        if (method == "textDocument/didSave")    { on_did_save(params); return; }
        // For other requests, respond with MethodNotFound; ignore unknown
        // notifications.
        if (!id.is_null()) {
            respond_error(id, /*MethodNotFound=*/ -32601,
                          "schemlang: method not supported: " + method);
        }
    }

    void on_initialize(const json& id, const json& /*params*/) {
        json caps = {
            // 1 == Full text sync; the client sends the entire document on
            // each change. Simpler than incremental and adequate here.
            {"textDocumentSync", 1},
            {"diagnosticProvider", {
                {"interFileDependencies", false},
                {"workspaceDiagnostics",  false},
            }},
        };
        respond(id, json{
            {"capabilities", std::move(caps)},
            {"serverInfo",   {{"name", "schemlang"}, {"version", "0.1.0"}}},
        });
    }

    void on_shutdown(const json& id) {
        shutdown_requested_ = true;
        respond(id, json(nullptr));
    }

    void on_exit() {
        exited_ = true;
    }

    static std::string filename_from_uri(std::string_view uri) {
        // Trim the "file://" scheme so diagnostic messages read nicely.
        std::string_view scheme = "file://";
        if (uri.substr(0, scheme.size()) == scheme) {
            return std::string(uri.substr(scheme.size()));
        }
        return std::string(uri);
    }

    void on_did_open(const json& params) {
        auto const& td = params.at("textDocument");
        std::string uri  = td.at("uri").get<std::string>();
        std::string text = td.at("text").get<std::string>();
        int version      = td.value("version", 0);
        docs_[uri] = {std::move(text), version};
        publish_diagnostics(uri);
    }

    void on_did_change(const json& params) {
        auto const& td = params.at("textDocument");
        std::string uri = td.at("uri").get<std::string>();
        int version     = td.value("version", 0);
        auto it = docs_.find(uri);
        if (it == docs_.end()) {
            log_stderr("didChange before didOpen for " + uri);
            docs_[uri] = {"", version};
            it = docs_.find(uri);
        }
        // Full sync: the last contentChanges entry has the entire text.
        if (params.contains("contentChanges") && !params.at("contentChanges").empty()) {
            auto const& ch = params.at("contentChanges").back();
            if (ch.contains("text")) {
                it->second.text = ch.at("text").get<std::string>();
            }
        }
        it->second.version = version;
        publish_diagnostics(uri);
    }

    void on_did_save(const json& params) {
        auto const& td  = params.at("textDocument");
        std::string uri = td.at("uri").get<std::string>();
        // If the save notification carried text, use it; otherwise keep
        // what we have. Either way, re-publish diagnostics.
        if (params.contains("text") && params.at("text").is_string()) {
            auto it = docs_.find(uri);
            if (it != docs_.end()) {
                it->second.text = params.at("text").get<std::string>();
            }
        }
        publish_diagnostics(uri);
    }

    void on_did_close(const json& params) {
        auto const& td  = params.at("textDocument");
        std::string uri = td.at("uri").get<std::string>();
        docs_.erase(uri);
        // Clear any diagnostics the client may be showing for this file.
        notify("textDocument/publishDiagnostics", json{
            {"uri",         uri},
            {"diagnostics", json::array()},
        });
    }

    void publish_diagnostics(const std::string& uri) {
        auto it = docs_.find(uri);
        if (it == docs_.end()) return;

        std::string filename = filename_from_uri(uri);
        auto result = check_source(filename, it->second.text);

        json arr = json::array();
        for (auto const& d : result.diagnostics) {
            // Range is empty (start == end). VS Code expands it visually
            // because we don't yet know the token length here. A future
            // improvement is to tag each Diagnostic with its end position.
            json range = {
                {"start", lsp_position(d.pos.line, d.pos.column)},
                {"end",   lsp_position(d.pos.line, d.pos.column + 1)},
            };
            arr.push_back({
                {"range",    std::move(range)},
                {"severity", lsp_severity_for(d.level)},
                {"source",   "schemlang"},
                {"message",  d.message},
            });
        }
        notify("textDocument/publishDiagnostics", json{
            {"uri",         uri},
            {"version",     it->second.version},
            {"diagnostics", std::move(arr)},
        });
    }
};

} // namespace

int run_lsp_server() {
    LspServer s;
    return s.run();
}

} // namespace schemlang
