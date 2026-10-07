// The Rawframe debug type (ADR-0066's debugger bridge, D461): VS Code starts
// `rawframe-debug`, which speaks the Debug Adapter Protocol and attaches to a
// running game's tooling endpoint. Breakpoints are set on functions (the
// Breakpoints view's "+"), by name, with the module or without it.

const { debug, workspace, DebugAdapterExecutable } = require("vscode");

// The program run is the user's to name, in their own settings: a
// workspace's settings, which come with whatever was cloned, never are.
const adapters = {
    createDebugAdapterDescriptor() {
        const named = workspace.getConfiguration("rawframe").inspect("debugPath");
        const command = (named && named.globalValue) || "rawframe-debug";
        return new DebugAdapterExecutable(command, []);
    },
};

function activate(context) {
    context.subscriptions.push(debug.registerDebugAdapterDescriptorFactory("rawframe", adapters));
}

function deactivate() {}

module.exports = { activate, deactivate };
