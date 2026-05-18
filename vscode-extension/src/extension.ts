// Schemlang VS Code extension entry point.
//
// Activates on .schemlang files, spawns the `schemlang` binary with the
// `--lsp` flag, and bridges it to VS Code through vscode-languageclient.
// The C++ server publishes textDocument/publishDiagnostics back to the
// editor; the client renders them as squiggles in the Problems pane.

import * as fs from "fs";
import * as path from "path";
import {
    ExtensionContext, OutputChannel, workspace, window, WorkspaceFolder,
} from "vscode";
import {
    LanguageClient,
    LanguageClientOptions,
    ServerOptions,
    TransportKind,
} from "vscode-languageclient/node";

let client: LanguageClient | undefined;
let log: OutputChannel | undefined;

const EXE_SUFFIX  = process.platform === "win32" ? ".exe" : "";
const BINARY_NAME = `schemlang${EXE_SUFFIX}`;

// Relative paths under a project root where the user is likely to have
// built the native binary. Order matters: most common first.
const RELATIVE_BUILD_CANDIDATES = [
    `build/bin/${BINARY_NAME}`,
    `build/${BINARY_NAME}`,
    `build-debug/bin/${BINARY_NAME}`,
    `build-release/bin/${BINARY_NAME}`,
    `out/bin/${BINARY_NAME}`,
];

// How many levels we'll walk *up* from a workspace folder when searching.
// 6 is plenty — workspace = examples/, build/ is one up. Six gives slack
// if the user opens, say, examples/std/.
const MAX_UP_LEVELS = 6;

function existsAndExecutable(p: string): boolean {
    try {
        const st = fs.statSync(p);
        if (!st.isFile()) return false;
        if (process.platform !== "win32") {
            fs.accessSync(p, fs.constants.X_OK);
        }
        return true;
    } catch {
        return false;
    }
}

function searchPath(name: string): string | null {
    const PATH = process.env.PATH ?? "";
    const sep  = process.platform === "win32" ? ";" : ":";
    for (const dir of PATH.split(sep)) {
        if (!dir) continue;
        const candidate = path.join(dir, name);
        if (existsAndExecutable(candidate)) return candidate;
    }
    return null;
}

// Generate ancestor directories of `start`, including `start` itself,
// stopping at the filesystem root or after `levels` iterations.
function* walkUp(start: string, levels: number): Iterable<string> {
    let cur = path.resolve(start);
    for (let i = 0; i <= levels; ++i) {
        yield cur;
        const next = path.dirname(cur);
        if (next === cur) return;
        cur = next;
    }
}

// Resolve the schemlang binary using (in order):
//   1. The configured `schemlang.serverPath`, if it points at an existing
//      file (absolute or workspace-relative).
//   2. Any RELATIVE_BUILD_CANDIDATE under any open workspace folder or
//      any of its ancestor directories.
//   3. Any RELATIVE_BUILD_CANDIDATE under the extension folder or any
//      ancestor (this covers running the dev host where the workspace is
//      a subfolder of the project, e.g. `examples/`).
//   4. The configured name (default "schemlang") on $PATH.
function resolveServerPath(
    configured: string,
    folders: readonly WorkspaceFolder[] | undefined,
    extensionPath: string,
): { command: string; tried: string[]; resolved: boolean } {
    const tried: string[] = [];

    const tryFile = (p: string): string | null => {
        tried.push(p);
        return existsAndExecutable(p) ? p : null;
    };

    const isDefaultName =
        !configured || configured === "schemlang" || configured === BINARY_NAME;

    if (!isDefaultName) {
        if (path.isAbsolute(configured)) {
            const hit = tryFile(configured);
            if (hit) return { command: hit, tried, resolved: true };
        } else {
            for (const folder of folders ?? []) {
                const hit = tryFile(path.join(folder.uri.fsPath, configured));
                if (hit) return { command: hit, tried, resolved: true };
            }
        }
    }

    const seen = new Set<string>();
    const searchRoot = (root: string) => {
        for (const dir of walkUp(root, MAX_UP_LEVELS)) {
            if (seen.has(dir)) continue;
            seen.add(dir);
            for (const rel of RELATIVE_BUILD_CANDIDATES) {
                const hit = tryFile(path.join(dir, rel));
                if (hit) return hit;
            }
        }
        return null;
    };

    for (const folder of folders ?? []) {
        const hit = searchRoot(folder.uri.fsPath);
        if (hit) return { command: hit, tried, resolved: true };
    }
    const hitExt = searchRoot(extensionPath);
    if (hitExt) return { command: hitExt, tried, resolved: true };

    const onPath = searchPath(configured || BINARY_NAME);
    if (onPath) {
        tried.push(`$PATH:${configured || BINARY_NAME}`);
        return { command: onPath, tried, resolved: true };
    }

    return { command: configured || BINARY_NAME, tried, resolved: false };
}

export function activate(context: ExtensionContext) {
    log = window.createOutputChannel("Schemlang");
    context.subscriptions.push(log);

    const config     = workspace.getConfiguration("schemlang");
    const configured = config.get<string>("serverPath", "schemlang");

    const { command, tried, resolved } = resolveServerPath(
        configured, workspace.workspaceFolders, context.extensionPath,
    );

    log.appendLine(`configured serverPath = ${JSON.stringify(configured)}`);
    log.appendLine(`extensionPath         = ${context.extensionPath}`);
    log.appendLine("workspace folders:");
    for (const f of workspace.workspaceFolders ?? []) {
        log.appendLine(`  - ${f.uri.fsPath}`);
    }
    log.appendLine("paths tried:");
    for (const t of tried) log.appendLine(`  - ${t}`);
    log.appendLine(`resolved              = ${resolved}`);
    log.appendLine(`launching             = ${command} --lsp`);

    const serverOptions: ServerOptions = {
        run:   { command, args: ["--lsp"], transport: TransportKind.stdio },
        debug: { command, args: ["--lsp"], transport: TransportKind.stdio },
    };

    const clientOptions: LanguageClientOptions = {
        documentSelector: [{ scheme: "file", language: "schemlang" }],
        synchronize: {
            fileEvents: workspace.createFileSystemWatcher("**/*.schemlang"),
        },
        outputChannel: log,
    };

    client = new LanguageClient(
        "schemlang",
        "Schemlang Language Server",
        serverOptions,
        clientOptions,
    );

    client.start().catch(err => {
        const triedList = tried.length
            ? `\n\nLooked in:\n  ${tried.join("\n  ")}`
            : "";
        window.showErrorMessage(
            `Failed to launch schemlang LSP server '${command}': ${err}` +
            triedList +
            `\n\nSet "schemlang.serverPath" in settings to an absolute path, ` +
            `or build the server with \`cmake --build build\` from the workspace root.`,
            { modal: false },
        );
    });

    context.subscriptions.push({ dispose: () => client?.stop() });
}

export function deactivate(): Thenable<void> | undefined {
    return client?.stop();
}
