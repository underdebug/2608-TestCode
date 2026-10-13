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
const { registerShaderDefinitions } = require('./definitions');
const path = require('path');
const fs = require('fs');
const { execFile } = require('child_process');
const { promisify } = require('util');
const execFileAsync = promisify(execFile);

let LOG = 0; 

function log(...args) {
    const stack = new Error().stack;
    const m = stack.split('\n')[2]?.match(/:(\d+):\d+\)?$/);
    const line = m ? m[1] : '';

    vscode.debug.activeDebugConsole.appendLine(
        `>${line} ${args.join(' ')}`
    );
}

async function printStack(session) {
    const editor = vscode.window.activeTextEditor;
    const line = editor.selection.active.line + 1;

    let stack;
    try {
        LOG && log('printStack', line, "getStackFunction(session)");
        stack = await getStackFunction(session);
        vscode.debug.activeDebugConsole.appendLine(`${line} ${stack}`);        
    } catch (e) {
        log('Error printStack', line, stack, e.message);
    }
}

async function getFrameId(session) {
    const item = vscode.debug.activeStackItem;
    if (item && typeof item.frameId === 'number') {
        return item.frameId;
    }
    try {
        let tid = item && typeof item.threadId === 'number' ? item.threadId : undefined;
        if (tid === undefined) {
            const threads = await session.customRequest('threads');
            tid = threads && threads.threads && threads.threads[0] && threads.threads[0].id;
        }
        if (tid === undefined) return undefined;
        const st = await session.customRequest('stackTrace', {
            threadId: tid, startFrame: 0, levels: 1
        });
        return st && st.stackFrames && st.stackFrames[0] && st.stackFrames[0].id;
    } catch (e) {
        return undefined;
    }
}

async function getStackFunction(session) {
    const item = vscode.debug.activeStackItem;
    let threadId = item && item.session === session ? item.threadId : undefined;
    if (threadId === undefined) {
        const response = await session.customRequest('threads');
        threadId = response.threads && response.threads[0] && response.threads[0].id;
    }
    if (threadId === undefined) {
        log("getStackFunction, threadId === undefined, Error")
        return;
    }
    const response = await session.customRequest('stackTrace', {
        threadId,
        startFrame: 0,
        levels: 4
    });
    return (response.stackFrames || []).slice(0, 4)
        .map(frame => frame.name.replace(/\(.*$/, '').trim())
        .join(' <- ');
}

async function disableCollapseIdenticalLines() {
    const cfg = vscode.workspace.getConfiguration('debug');

    await cfg.update(
        'console.collapseIdenticalLines',
        false,
        vscode.ConfigurationTarget.Global
    );
}

async function isOpeningProject(context) {
    let openingProject = false;
    const openProject = async (editor) => {
        if (openingProject) return false;
        openingProject = true;
        try {
            return await openProjectFromEditor(editor);
        } catch (e) {
            log('Error opening project folder', e.message);
            return false;
        } finally {
            openingProject = false;
        }
    };
    context.subscriptions.push(
        vscode.window.onDidChangeActiveTextEditor(openProject)
    );
    return await openProject(vscode.window.activeTextEditor);
}

async function setLauchJson() {
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
        ['vert', 'frag', 'glsl']
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

async function memInit(session) {
    const editor = vscode.window.activeTextEditor;
    const frameId = await getFrameId(session);
    LOG && log('memInit', 'getFrameId', frameId);
 
    let projectPath = '';
    const folder = vscode.workspace.getWorkspaceFolder(editor.document.uri);
    if (folder) {
        projectPath = folder.uri.fsPath.replace(/\\/g, '/');
        LOG && log('memInit', 'folder projectPath', projectPath);
    }else{
        projectPath = path.dirname(editor.document.uri.fsPath).replace(/\\/g, '/');
        LOG && log('memInit', 'projectPath', projectPath);
    }

    const extensionPath = __dirname.replace(/\\/g, '/');
    LOG && log('memInit', 'extensionPath', extensionPath);

    expr = `-exec python import sys; sys.path[:0]=["${projectPath}/mem","${extensionPath}"]; import importlib, mem; importlib.reload(mem); from mem import *;`;
    LOG && log('memInit', expr);

    try {
        await session.customRequest('evaluate', {
            expression: expr,
            context: 'repl',
            frameId: frameId
        });
   } catch (e) {
        log('Error memInit', expr, e.message);
    }
}

async function runPython(editor) {
    try {
        const folder = vscode.workspace.getWorkspaceFolder(editor.document.uri);
        const started = await vscode.debug.startDebugging(folder, {
            name: 'Python: Current File (Ctrl+F8)',
            type: 'debugpy',
            request: 'launch',
            program: editor.document.uri.fsPath,
            cwd: path.dirname(editor.document.uri.fsPath),
            console: 'internalConsole',
            internalConsoleOptions: 'openOnSessionStart',
            redirectOutput: true,
            noDebug: true
        });
        if (!started) {
            vscode.window.showErrorMessage('mem: Could not run the Python file.');
        }
    } catch (e) {
        log('Error runPython', e.message);
        vscode.window.showErrorMessage(`mem: ${e.message}`);
    }
}

async function memCppCpp(session) {
    const editor = vscode.window.activeTextEditor;
    const line = editor.selection.active.line + 1;
    const frameId = await getFrameId(session);

    let text;
    if (editor.selection.isEmpty) { // TOCKEN
        const pos = editor.selection.active;
        const range = editor.document.getWordRangeAtPosition(pos);
        LOG && log('memCppCpp', 'pos', pos, 'range', range);
        if (!range)
            return

        text = editor.document.getText(range);
        LOG && log('memCppCpp', line, text);
    } else { // SELECTION
        text = editor.document.getText(editor.selection);
    }

    let expression = `value = mem(${JSON.stringify(text)}); print(value, ${JSON.stringify('<= ' + text)}, list(value.shape) if hasattr(value, 'shape') and value.shape else '', '|${line}|')`;
    expression = JSON.stringify(expression);

    const expr = `-exec python exec(${expression})`;
    try {
        const response = await session.customRequest('evaluate', {
            expression: expr,
            context: 'repl',
            frameId: frameId
        });
        if (response && response.result) {
            vscode.debug.activeDebugConsole.appendLine(response.result);
        }
    } catch (e) {
        log('Error memCppCpp', expr, e.message);
    }
}

async function memCppPython(session) {
    const editor = vscode.window.activeTextEditor;
    const line = editor.selection.active.line + 1;
    const frameId = await getFrameId(session);

    let text;
    if (editor.selection.isEmpty) { // ALL TEXT
        text = editor.document.getText(); 
        LOG && log('memCppPython', 'editor.document.getText()', text);
    } else { // SELECTION
        text = editor.document.getText(editor.selection);
        LOG && log('memCppPython', 'editor.document.getText(editor.selection)', text);
    }

    const lines = text.split(/\r?\n/).map(line => line.replace(/\s+$/, ''));
    expression = lines.join(' \n');
    expression = JSON.stringify(expression);

    const expr = `-exec python exec(${expression})`;
    LOG && log('memCppPython', expr);

    try {
        const response = await session.customRequest('evaluate', {
            expression: expr,
            context: 'repl',
            frameId: frameId
        });
        if (response && response.result) {
            vscode.debug.activeDebugConsole.appendLine(response.result);
        }
    } catch (e) {
        log('Error memCppPython', expr, e.message);
    }
}

async function memPythonPython(session) {
    const editor = vscode.window.activeTextEditor;
    // if (!editor.selection.isEmpty) {
    //     await vscode.commands.executeCommand('editor.debug.action.selectionToRepl');
    //     return;
    // }
    
    const frameId = await getFrameId(session);
    let expression;

    if (editor.selection.isEmpty) {
        const pos = editor.selection.active;
        const range = editor.document.getWordRangeAtPosition(pos);
        if (range) { // TOCKEN
            const text = editor.document.getText(range);
            LOG && log('memCppPython TOCKEN', 'range', JSON.stringify(range));
            LOG && log('memCppPython TOCKEN', 'text', text);

            const inspectCode = [
                'import numpy as np',
                'display = value',
                "size = ''",
                'try:',
                '    data = np.asarray(value)',
                '    if isinstance(value, (list, tuple)) or hasattr(value, "shape"):',
                "        size = list(data.shape) if data.shape else ''",
                '    if np.issubdtype(data.dtype, np.number):',
                '        display = data.round(3) if data.shape else data.round(3).item()',
                '    if isinstance(display, np.ndarray):',
                '        display = np.array2string(display, max_line_width=1000, threshold=100, edgeitems=50, precision=3, suppress_small=True)',
                'except (TypeError, ValueError):',
                '    size = "ragged" if isinstance(value, (list, tuple)) else ""',
                'print(display, label, size, location)'
            ].join('\n');
            expression = `exec(${JSON.stringify(inspectCode)}, {'value': ${text}, 'label': ${JSON.stringify(`<= ${text}`)}, 'location': ${JSON.stringify(`⌊${pos.line + 1}⌉`)}})`;
     
        } else { // ALL TEXT
            expression = editor.document.getText(); 
            LOG && log('memPythonPython', 'editor.document.getText()', expression);

        }
    } else { // SELECTION
        expression = editor.document.getText(editor.selection);
        LOG && log('memPythonPython', 'editor.document.getText(editor.selection)', expression);
    }

    const expr = expression;
    LOG && log('memPythonPython', expr);

    try {
        const response = await session.customRequest('evaluate', {
            expression: expr,
            context: 'repl',
            frameId: frameId
        });
        if (response && typeof response.result === 'string' && response.result.length > 0) {
            vscode.debug.activeDebugConsole.appendLine(response.result);
        }
    } catch (e) {
        log('Error memPythonPython', expr, e.message);
    }
}

async function openProjectFromEditor(editor) {
    if (vscode.workspace.workspaceFolders?.length || !editor
        || editor.document.uri.scheme !== 'file') return false;

    let directory = path.dirname(editor.document.uri.fsPath);
    while (true) {
        try {
            if (fs.statSync(path.join(directory, '.vscode')).isDirectory()) {
                await vscode.commands.executeCommand(
                    'vscode.openFolder', vscode.Uri.file(directory), false
                );
                return true;
            }
        } catch (e) {
            if (e.code !== 'ENOENT' && e.code !== 'ENOTDIR') {
                log('Error finding project folder', e.message);
                return false;
            }
        }
        const parent = path.dirname(directory);
        if (parent === directory) return false;
        directory = parent;
    }
}

async function debugPython(editor) {
    try {
        const folder = vscode.workspace.getWorkspaceFolder(editor.document.uri);
        const started = await vscode.debug.startDebugging(folder, {
            name: 'Python: Current File (F8)',
            type: 'debugpy',
            request: 'launch',
            program: editor.document.uri.fsPath,
            cwd: path.dirname(editor.document.uri.fsPath),
            console: 'internalConsole',
            internalConsoleOptions: 'openOnSessionStart',
            redirectOutput: true,
            noDebug: false
        });
        if (!started) {
            vscode.window.showErrorMessage('mem: Could not start the Python debugger.');
        }
    } catch (e) {
        log('Error debugPython', e.message);
        vscode.window.showErrorMessage(`mem: ${e.message}`);
    }
}

async function buildCpp(editor) {
    try {
        if (!await vscode.workspace.saveAll()) return false;
        const filename = editor.document.uri.fsPath;
        const source = filename;
        const folder = vscode.workspace.getWorkspaceFolder(editor.document.uri);
        const root = folder ? folder.uri.fsPath : path.dirname(filename);
        const programName = path.basename(filename).split('.')[0];
        if (!programName) throw new Error('The filename must contain a program name before the first dot.');
        const { stdout, stderr } = await execFileAsync(
            process.env.CXX || 'g++',
            ['-std=c++17', '-O0', '-g3', source, '-o', path.join(path.dirname(filename), programName)],
            { cwd: root, maxBuffer: 10 * 1024 * 1024 }
        );
        if (stdout) vscode.debug.activeDebugConsole.appendLine(stdout.trimEnd());
        if (stderr) vscode.debug.activeDebugConsole.appendLine(stderr.trimEnd());
        return true;
    } catch (e) {
        log('Error buildCpp', e.message);
        if (e.stdout) vscode.debug.activeDebugConsole.appendLine(e.stdout.trimEnd());
        if (e.stderr) vscode.debug.activeDebugConsole.appendLine(e.stderr.trimEnd());
        await vscode.commands.executeCommand('workbench.panel.repl.view.focus');
        vscode.window.showErrorMessage('mem: Build failed. See the Debug Console for details.');
        return false;
    }
}

async function buildShader(editor) {
    try {
        if (!await vscode.workspace.saveAll()) return false;
        const filename = editor.document.uri.fsPath;
        const source = /\.(cpp|cc|cxx|c|h|hpp\+\+)$/i.test(filename)
            ? filename : `${filename}.cpp`;
        const folder = vscode.workspace.getWorkspaceFolder(editor.document.uri);
        const root = folder ? folder.uri.fsPath : path.dirname(filename);
        const programName = path.basename(filename).split('.')[0];
        if (!programName) throw new Error('The filename must contain a program name before the first dot.');
        const { stdout, stderr } = await execFileAsync(
            process.platform === 'win32' ? 'python' : 'python3',
            [path.join(__dirname, 'shader.py'), source, '--output', path.join(path.dirname(filename), programName)],
            { cwd: root, maxBuffer: 10 * 1024 * 1024 }
        );
        if (stdout) vscode.debug.activeDebugConsole.appendLine(stdout.trimEnd());
        if (stderr) vscode.debug.activeDebugConsole.appendLine(stderr.trimEnd());
        return true;
    } catch (e) {
        log('Error buildShader', e.message);
        if (e.stdout) vscode.debug.activeDebugConsole.appendLine(e.stdout.trimEnd());
        if (e.stderr) vscode.debug.activeDebugConsole.appendLine(e.stderr.trimEnd());
        await vscode.commands.executeCommand('workbench.panel.repl.view.focus');
        vscode.window.showErrorMessage('mem: Build failed. See the Debug Console for details.');
        return false;
    }
}

async function buildKernel(editor) {
    try {
        if (!await vscode.workspace.saveAll()) return false;
        const filename = editor.document.uri.fsPath;
        const source = /\.(cpp|cc|cxx|c|h|hpp\+\+)$/i.test(filename)
            ? filename : `${filename}.cpp`;
        const folder = vscode.workspace.getWorkspaceFolder(editor.document.uri);
        const root = folder ? folder.uri.fsPath : path.dirname(filename);
        const programName = path.basename(filename).split('.')[0];
        if (!programName) throw new Error('The filename must contain a program name before the first dot.');
        const { stdout, stderr } = await execFileAsync(
            process.platform === 'win32' ? 'python' : 'python3',
            [path.join(__dirname, 'kernel.py'), source, '--output', path.join(path.dirname(filename), programName)],
            { cwd: root, maxBuffer: 10 * 1024 * 1024 }
        );
        if (stdout) vscode.debug.activeDebugConsole.appendLine(stdout.trimEnd());
        if (stderr) vscode.debug.activeDebugConsole.appendLine(stderr.trimEnd());
        return true;
    } catch (e) {
        log('Error buildKernel', e.message);
        if (e.stdout) vscode.debug.activeDebugConsole.appendLine(e.stdout.trimEnd());
        if (e.stderr) vscode.debug.activeDebugConsole.appendLine(e.stderr.trimEnd());
        await vscode.commands.executeCommand('workbench.panel.repl.view.focus');
        vscode.window.showErrorMessage('mem: Build failed. See the Debug Console for details.');
        return false;
    }
}

async function buildCpp(editor) {
    try {
        if (!await vscode.workspace.saveAll()) return false;
        const filename = editor.document.uri.fsPath;
        const source = filename;
        const folder = vscode.workspace.getWorkspaceFolder(editor.document.uri);
        const root = folder ? folder.uri.fsPath : path.dirname(filename);
        const programName = path.basename(filename).split('.')[0];
        if (!programName) throw new Error('The filename must contain a program name before the first dot.');
        const { stdout, stderr } = await execFileAsync(
            process.env.CXX || 'g++',
            ['-std=c++17', '-O0', '-g3', source, '-o', path.join(path.dirname(filename), programName)],
            { cwd: root, maxBuffer: 10 * 1024 * 1024 }
        );
        if (stdout) vscode.debug.activeDebugConsole.appendLine(stdout.trimEnd());
        if (stderr) vscode.debug.activeDebugConsole.appendLine(stderr.trimEnd());
        return true;
    } catch (e) {
        log('Error buildCpp', e.message);
        if (e.stdout) vscode.debug.activeDebugConsole.appendLine(e.stdout.trimEnd());
        if (e.stderr) vscode.debug.activeDebugConsole.appendLine(e.stderr.trimEnd());
        await vscode.commands.executeCommand('workbench.panel.repl.view.focus');
        vscode.window.showErrorMessage('mem: Build failed. See the Debug Console for details.');
        return false;
    }
}

async function debugCpp(editor) {
    try {
        const programName = path.basename(editor.document.uri.fsPath).split('.')[0];
        if (!programName) {
            vscode.window.showErrorMessage('mem: The filename must contain a program name before the first dot.');
            return;
        }
        const folder = vscode.workspace.getWorkspaceFolder(editor.document.uri);
        if (!folder) {
            vscode.window.showErrorMessage('mem: Open the project folder before debugging C++.');
            return;
        }
        const configurations = vscode.workspace
            .getConfiguration('launch', folder.uri)
            .get('configurations', []);
        const configuration = configurations.find(config =>
            config.type === 'cppdbg' && config.request === 'launch'
        );
        if (!configuration) {
            vscode.window.showErrorMessage('mem: Add a cppdbg launch configuration to .vscode/launch.json.');
            return;
        }
        if (!await vscode.workspace.saveAll()) return;
        const started = await vscode.debug.startDebugging(folder, {
            ...configuration,
            name: `C++: Debug ${programName}`,
            program: path.join(path.dirname(editor.document.uri.fsPath), programName),
            internalConsoleOptions: 'openOnSessionStart',
            stopAtEntry: false
        });
        if (!started) {
            vscode.window.showErrorMessage('mem: Could not start the C++ debugger.');
        }
    } catch (e) {
        log('Error debugCpp', e.message);
        vscode.window.showErrorMessage(`mem: ${e.message}`);
    }
}

async function cmdMem(initializedSessions) {
    const editor = vscode.window.activeTextEditor;
    if (!editor) {
        log('vscode.window.activeTextEditor; !editor return');
        return;
    }

    if (!await editor.document.save()) return;

    let isPythonFile = true;
    {
        const langId = editor.document.languageId;
        const ext = path.extname(editor.document.uri.fsPath).toLowerCase();
        isPythonFile = langId === 'python' || ext === '.py';
    }
    LOG && log('isPythonFile', isPythonFile);

    const session = vscode.debug.activeDebugSession;
    if (!session) {
        if (isPythonFile){
            log('!session runPython(editor)');
            await runPython(editor);   
        }  
        LOG && log('!session return');         
        return;
    }

    let isPythonProgram = false;
    {
        const session_type = (session.type || '').toLowerCase();
        if (['debugpy', 'python', 'pythonexperimental'].includes(session_type)) {
            isPythonProgram = true;
        }
    }
    LOG && log('isPythonProgram', isPythonProgram);    

    if (!isPythonProgram) { // C++ program
        if (!initializedSessions.has(session)) {
            LOG && log('memInit(session) C++ program, initializedSessions');
            await memInit(session);
            initializedSessions.add(session);
        }

        if (!isPythonFile) { // C++ program, C++ file
            LOG && log('memCppCpp(session) C++ program, C++ file')
            await printStack(session)
            await memCppCpp(session)

        } else { // C++ program, Python file
            LOG && log('memCppPython(session) C++ program, Python file')
            await printStack(session)
            await memCppPython(session)
        }
        
    } else { // Python program
        if (isPythonFile) { // Python program, Python file
            LOG && log('memPythonPython(session) Python program, Python file')
            await printStack(session)
            await memPythonPython(session);
        }
    }
}

function isExistMain(filename) {
    try {
        const source = fs.readFileSync(filename, 'utf8').replace(
            /\/\*[\s\S]*?\*\/|\/\/[^\r\n]*|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'/g,
            ' '
        );
        return /\bmain\s*\(/.test(source);
    } catch (e) {
        log('Error isExistMain readFileSync', e.message);
        return false;
    }
}

async function cmdDebug() {
    const editor = vscode.window.activeTextEditor;
    if (!editor) {
        log('vscode.window.activeTextEditor; !editor return');
        return;
    }
    
    if (!await editor.document.save()) return;

    const filename = editor.document.uri.fsPath;
    const isPythonFile = editor.document.languageId === 'python' || /\.py$/i.test(filename);
    const isCppFile = /\.(c|cpp|h|hpp)$/i.test(filename);   
    const isShaderFile = /\.(vert|frag|geom|tesc|tese|comp|glsl)$/i.test(filename);
    const isKernelFile = /\.(cu|cuh)$/i.test(filename);

    const sourceText = editor.document.getText().replace(
        /\/\*[\s\S]*?\*\/|\/\/[^\r\n]*/g,
        comment => comment.replace(/[^\r\n]/g, ' ')
    );

    const includeShaderFile = /^[ \t]*#[ \t]*include[ \t]*["<][^"<>\r\n]+\.(vert|frag|geom|tesc|tese|comp|glsl)[">]/im.test(sourceText);
    const includeKernelFile = /^[ \t]*#[ \t]*include[ \t]*["<][^"<>\r\n]+\.(cu|cuh)[">]/im.test(sourceText);

    if(isPythonFile){
        await debugPython(editor);
        return;
    }
    else if(isCppFile && !includeShaderFile && !includeKernelFile){
        if (!await buildCpp(editor)) return;
        await debugCpp(editor);
    }
    else if(isShaderFile || includeShaderFile){
        if(!isExistMain(filename)){
            return;
        }
        let shaderName;
        let source;
        if(isShaderFile){
            shaderName = path.basename(filename);
            source = `${filename}.cpp`;
        }
        else{
            shaderName = filename.replace(/\.(c|cpp|h)$/i, '');
            source = filename;
        }
            
        const wrapper = `#include <stdio.h>\n#include "shader.h"\n#include "${shaderName}"\n\nint main() {\n    printf("hello mingjie!");\n    shader();\n    return 0;\n}\n`;
        try {
            await fs.promises.writeFile(source, wrapper, { encoding: 'utf8', flag: 'wx' });
        } catch (e) {
            if (e.code !== 'EEXIST') {
                log('Error creating shader C++ wrapper', e.message);
                vscode.window.showErrorMessage(`mem: ${e.message}`);
                return;
            }
        }     
        
        if (!await buildShader(editor)) return;
        await debugCpp(editor);
    }
    else if(isKernelFile || includeKernelFile){
        if (isKernelFile) {
            const kernelName = path.basename(filename);
            const source = `${filename}.cpp`;
            const wrapper = `#include <stdio.h>\n#include "kernel.h"\n#include "${kernelName}"\n\nint main() {\n    printf("hello mingjie!");\n    kernel();\n    return 0;\n}\n`;
            try {
                await fs.promises.writeFile(source, wrapper, { encoding: 'utf8', flag: 'wx' });
            } catch (e) {
                if (e.code !== 'EEXIST') {
                    log('Error creating kernel C++ wrapper', e.message);
                    vscode.window.showErrorMessage(`mem: ${e.message}`);
                    return;
                }
            }
        }

        if (!await buildKernel(editor)) return;
        await debugCpp(editor);
    }        
}

async function activate(context) {
    registerShaderDefinitions(vscode, context);
    if (await isOpeningProject(context)) {
        return;
    }

    LOG && log('disableCollapseIdenticalLines()');
    await disableCollapseIdenticalLines();

    LOG && log('setLauchJson()');
    await setLauchJson();

    LOG && log('setSettingJson()');
    await setSettingJson();

    const initializedSessions = new WeakSet();

    const cmd = vscode.commands.registerCommand('mem', async () => {
        await cmdMem(initializedSessions);
    });

    const debugCmd = vscode.commands.registerCommand('debug', async () => {
        await cmdDebug();
    });

    context.subscriptions.push(cmd, debugCmd);
}

function deactivate() {}
module.exports = { activate, deactivate };
