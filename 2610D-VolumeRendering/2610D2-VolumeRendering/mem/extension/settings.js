// MIT License

// Copyright (c) 2026 Mingjie Zhang (mingjie2026@gmail.com)

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

const vscode = require('vscode');

async function disableCollapseIdenticalLines() {
    const cfg = vscode.workspace.getConfiguration('debug');

    await cfg.update(
        'console.collapseIdenticalLines',
        false,
        vscode.ConfigurationTarget.Global
    );
}

async function setLaunchJson() {
    const cfg = vscode.workspace.getConfiguration('launch');

    const configurations = cfg.get('configurations', []);

    let changed = false;

    for (const config of configurations) {
        if (config.type === 'debugpy' && config.console !== 'internalConsole') {
            config.console = 'internalConsole';
            changed = true;
        }
        if (config.type === 'debugpy' && config.redirectOutput !== true) {
            config.redirectOutput = true;
            changed = true;
        }
        if (config.internalConsoleOptions !== 'openOnSessionStart') {
            config.internalConsoleOptions = 'openOnSessionStart';
            changed = true;
        }
    }

    if (changed) {
        await cfg.update(
            'configurations',
            configurations,
            vscode.ConfigurationTarget.Workspace
        );
    }
}

async function setSettingJson() {
    if (!vscode.workspace.workspaceFolders?.length) return;

    const cfg = vscode.workspace.getConfiguration();
    const target = vscode.ConfigurationTarget.Workspace;
    const shaderAssociations = Object.fromEntries(
        ['vert', 'frag', 'comp', 'glsl']
            .map(extension => [`*.${extension}`, 'cpp'])
    );
    const associations = cfg.get('files.associations', {});
    if (Object.entries(shaderAssociations).some(([key, value]) => associations[key] !== value)) {
        await cfg.update('files.associations', {
            ...associations,
            ...shaderAssociations
        }, target);
    }

    const settings = {
        // GLSL uses C++ highlighting, but its syntax is not valid C++.
        'C_Cpp.errorSquiggles': 'disabled',
        'debug.console.collapseIdenticalLines': false,
        'debug.allowBreakpointsEverywhere': true,
        'editor.tabSize': 4,
        'editor.insertSpaces': true,
        'editor.detectIndentation': false
    };

    for (const [key, value] of Object.entries(settings)) {
        if (cfg.inspect(key)?.workspaceValue !== value) {
            await cfg.update(key, value, target);
        }
    }

    const javascript = cfg.inspect('[javascript]')?.workspaceValue || {};
    const javascriptSettings = {
        'editor.tabSize': 4,
        'editor.insertSpaces': true,
        'editor.detectIndentation': false
    };
    if (Object.entries(javascriptSettings).some(([key, value]) => javascript[key] !== value)) {
        await cfg.update('[javascript]', {
            ...javascript,
            ...javascriptSettings
        }, target);
    }
}

async function applySettings() {
    await disableCollapseIdenticalLines();

    await setLaunchJson();

    await setSettingJson();
}

module.exports = { applySettings };
