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
const { applySettings } = require('./settings');
const path = require('path');
const fs = require('fs');
const { execFile } = require('child_process');
const { promisify } = require('util');
const execFileAsync = promisify(execFile);

LOG = 0; // 10/9 2026
ALWAYS = 1;

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

async function initialMem(session) {
    const editor = vscode.window.activeTextEditor;
    const frameId = await getFrameId(session);
    LOG && log('initialMem', 'getFrameId', frameId);
 
    let projectPath = '';
    const folder = vscode.workspace.getWorkspaceFolder(editor.document.uri);
    if (folder) {
        projectPath = folder.uri.fsPath.replace(/\\/g, '/');
        LOG && log('initialMem', 'folder projectPath', projectPath);
    }else{
        projectPath = path.dirname(editor.document.uri.fsPath).replace(/\\/g, '/');
        LOG && log('initialMem', 'projectPath', projectPath);
    }

    const extensionPath = __dirname.replace(/\\/g, '/');
    LOG && log('initialMem', 'extensionPath', extensionPath);

    expr = `-exec python import sys; sys.path[:0]=["${projectPath}/mem","${extensionPath}"]; import importlib, mem; importlib.reload(mem); from mem import *;`;
    LOG && log('initialMem', expr);

    try {
        await session.customRequest('evaluate', {
            expression: expr,
            context: 'repl',
            frameId: frameId
        });
   } catch (e) {
        log('Error initialMem', expr, e.message);
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
    if (editor.selection.isEmpty) { // TOKEN
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

    let expression;
    const isSingleWord = /^\S+$/.test(text.trim());

    if(isSingleWord){ // C++ Tocken
        expression = `value = mem(${JSON.stringify(text)}); print(value, ${JSON.stringify('<= ' + text)}, list(value.shape) if hasattr(value, 'shape') and value.shape else '', '|${line}|')`; 
    }
    else{ // Python code
        const lines = text.split(/\r?\n/).map(line => line.replace(/\s+$/, ''));
        expression = lines.join(' \n');
    }

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
        if (range) { // TOKEN
            const text = editor.document.getText(range);
            LOG && log('memCppPython TOKEN', 'range', JSON.stringify(range));
            LOG && log('memCppPython TOKEN', 'text', text);

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

function appBuildDirectory(editor) {
    const folder = vscode.workspace.getWorkspaceFolder(editor.document.uri);
    const root = folder ? folder.uri.fsPath : path.dirname(editor.document.uri.fsPath);
    return path.join(root, 'build');
}

function appExecutable(editor) {
    const programName = path.basename(editor.document.uri.fsPath).split('.')[0];
    if (!programName) throw new Error('The filename must contain a program name before the first dot.');
    return path.join(appBuildDirectory(editor), programName);
}

let buildErrorTerminal;
let buildErrorWriter;

async function showBuildError(error) {
    const output = [error.message, error.stdout, error.stderr]
        .filter(Boolean).map(value => String(value).trimEnd()).join('\n');
    if (vscode.debug.activeDebugSession) {
        vscode.debug.activeDebugConsole.appendLine(output);
        await vscode.commands.executeCommand('workbench.panel.repl.view.focus');
    } else {
        // F6 can fail before a debug session exists. A terminal keeps that
        // compiler output visible even when there is no active Debug Console.
        if (!buildErrorTerminal) {
            buildErrorWriter = new vscode.EventEmitter();
            buildErrorTerminal = vscode.window.createTerminal({
                name: 'mem: Build errors',
                pty: {
                    onDidWrite: buildErrorWriter.event,
                    open() {
                        buildErrorWriter.fire(output.replace(/\r?\n/g, '\r\n') + '\r\n');
                    },
                    close() {
                        buildErrorTerminal = undefined;
                        buildErrorWriter.dispose();
                        buildErrorWriter = undefined;
                    }
                }
            });
        } else {
            buildErrorWriter.fire(output.replace(/\r?\n/g, '\r\n') + '\r\n');
        }
        buildErrorTerminal.show();
    }
    vscode.window.showErrorMessage('mem: Build failed. See the compiler output.');
}

async function buildCpp(editor) {
    try {
        if (!await vscode.workspace.saveAll()) return false;
        const filename = editor.document.uri.fsPath;
        const source = filename;
        const folder = vscode.workspace.getWorkspaceFolder(editor.document.uri);
        const root = folder ? folder.uri.fsPath : path.dirname(filename);
        await fs.promises.mkdir(appBuildDirectory(editor), { recursive: true });
        const { stdout, stderr } = await execFileAsync(
            process.env.CXX || 'g++',
            ['-std=c++17', '-O0', '-g3', source, '-o', appExecutable(editor)],
            { cwd: root, maxBuffer: 10 * 1024 * 1024 }
        );
        if (stdout) vscode.debug.activeDebugConsole.appendLine(stdout.trimEnd());
        if (stderr) vscode.debug.activeDebugConsole.appendLine(stderr.trimEnd());
        return true;
    } catch (e) {
        await showBuildError(e);
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
        await fs.promises.mkdir(appBuildDirectory(editor), { recursive: true });
        const { stdout, stderr } = await execFileAsync(
            process.platform === 'win32' ? 'python' : 'python3',
            [path.join(__dirname, 'shader.py'), source, '--output', appExecutable(editor)],
            { cwd: root, maxBuffer: 10 * 1024 * 1024 }
        );
        if (stdout) vscode.debug.activeDebugConsole.appendLine(stdout.trimEnd());
        if (stderr) vscode.debug.activeDebugConsole.appendLine(stderr.trimEnd());
        return true;
    } catch (e) {
        await showBuildError(e);
        return false;
    }
}

async function buildKernel(editor, source = editor.document.uri.fsPath) {
    try {
        if (!await vscode.workspace.saveAll()) return false;
        const document = await vscode.workspace.openTextDocument(vscode.Uri.file(source));
        const dependencies = await findDependencies(document);
        const sources = [source, ...dependencies];
        const filename = editor.document.uri.fsPath;
        const output = appExecutable(editor);
        const buildDirectory = path.join(appBuildDirectory(editor), 'mem');
        await fs.promises.mkdir(buildDirectory, { recursive: true });
        const objects = [];
        const compiler = process.env.NVCC || 'nvcc';
        const run = async args => {
            log(compiler, ...args);
            const { stdout, stderr } = await execFileAsync(compiler, args,
                { cwd: path.dirname(source), maxBuffer: 10 * 1024 * 1024 });
            if (stdout) vscode.debug.activeDebugConsole.appendLine(stdout.trimEnd());
            if (stderr) vscode.debug.activeDebugConsole.appendLine(stderr.trimEnd());
        };
        for (const file of sources) {
            const object = path.join(buildDirectory, `${path.basename(file)}.o`);
            await run(['-G', '-g', '-dc', file, '-o', object]);
            objects.push(object);
        }
        await run(['-G', '-g', ...objects, '-o', output]);
        return true;
    } catch (e) {
        await showBuildError(e);
        return false;
    }
}

async function debugCpp(editor, debuggerType = 'cppdbg') {
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
            config.type === debuggerType && config.request === 'launch'
        );
        const defaultConfiguration = {
            type: debuggerType,
            request: 'launch',
            cwd: folder.uri.fsPath,
            ...(debuggerType === 'cppdbg' ? {
                MIMode: 'gdb',
                miDebuggerPath: process.env.GDB || 'gdb',
                externalConsole: false
            } : {
                debuggerPath: process.env.CUDA_GDB || '/usr/local/cuda/bin/cuda-gdb'
            })
        };
        if (!await vscode.workspace.saveAll()) return;
        const started = await vscode.debug.startDebugging(folder, {
            ...defaultConfiguration,
            ...configuration,
            name: `${debuggerType === 'cuda-gdb' ? 'CUDA' : 'C++'}: Debug ${programName}`,
            program: appExecutable(editor),
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
        if(!ALWAYS){
            if (!initializedSessions.has(session)) {
                LOG && log('initialMem(session) C++ program, initializedSessions');
                await initialMem(session);
                initializedSessions.add(session);
            }
        }

        if (!isPythonFile) { // C++ program, C++ file
            LOG && log('memCppCpp(session) C++ program, C++ file')
            await printStack(session);
            if(ALWAYS){
                await initialMem(session);
            }
            await memCppCpp(session);

        } else { // C++ program, Python file
            LOG && log('memCppPython(session) C++ program, Python file')
            await printStack(session);
            if(ALWAYS){
                await initialMem(session);
            }
            await memCppPython(session);
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

function inspectDependencies(source) {
    const clean = source.replace(
        /\/\*[\s\S]*?\*\/|\/\/[^\r\n]*|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|^[ \t]*#.*(?:\\\r?\n.*)*$/gm,
        text => text.replace(/[^\r\n]/g, ' ')
    );
    const definitions = new Set();
    const references = new Set();
    const keywords = new Set(['if', 'for', 'while', 'switch', 'catch', 'sizeof', 'alignof', 'decltype', 'static_assert']);
    for (const match of clean.matchAll(/\b([A-Za-z_]\w*)\s*(?:<<<[\s\S]*?>>>\s*)?\(/g)) {
        const name = match[1];
        if (keywords.has(name)) continue;
        let end = match.index + match[0].length;
        let depth = 1;
        while (end < clean.length && depth) {
            if (clean[end] === '(') depth++;
            if (clean[end] === ')') depth--;
            end++;
        }
        if (depth) continue;
        // A body distinguishes an implementation from a forward declaration.
        if (/^\s*(?:(?:const|override|final|noexcept)\s*)*\{/.test(clean.slice(end))) {
            definitions.add(name);
        } else {
            references.add(name);
        }
    }
    return { definitions, references };
}

async function findDependencies(document) {
    const folder = vscode.workspace.getWorkspaceFolder(document.uri);
    const root = folder ? folder.uri.fsPath : path.dirname(document.uri.fsPath);
    const uris = await vscode.workspace.findFiles(
        new vscode.RelativePattern(root, '**/*.{cu,cuh,c,cc,cpp,cxx,h,hpp,hxx}'),
        '**/{node_modules,.git,build,dist}/**'
    );
    const files = new Map();
    for (const uri of uris) {
        const candidate = await vscode.workspace.openTextDocument(uri);
        files.set(uri.fsPath, { ...inspectDependencies(candidate.getText()), source: candidate.getText() });
    }
    files.set(document.uri.fsPath, { ...inspectDependencies(document.getText()), source: document.getText() });
    const providers = new Map();
    for (const [filename, info] of files) {
        for (const name of info.definitions) {
            if (!providers.has(name)) providers.set(name, []);
            providers.get(name).push(filename);
        }
    }
    const includedSources = new Set();
    const found = new Set();
    const visited = new Set();
    const pending = [document.uri.fsPath];
    while (pending.length) {
        const filename = pending.pop();
        if (visited.has(filename)) continue;
        visited.add(filename);
        const info = files.get(filename);
        const clean = info.source.replace(/\/\*[\s\S]*?\*\/|\/\/[^\r\n]*/g, ' ');
        for (const include of clean.matchAll(/^[ \t]*#[ \t]*include[ \t]*["<]([^">\r\n]+)[">]/gm)) {
            const local = path.resolve(path.dirname(filename), include[1]);
            const workspacePath = path.resolve(root, include[1]);
            const header = files.has(local) ? local : files.has(workspacePath) ? workspacePath : undefined;
            if (!header) continue; // System/library headers are handled by nvcc.
            pending.push(header);
            if (/\.(cu|c|cc|cpp|cxx)$/i.test(header)) includedSources.add(header);
            if (/\.(h|hpp|hxx|cuh)$/i.test(header)) {
                const stem = header.replace(/\.(h|hpp|hxx|cuh)$/i, '');
                for (const extension of ['.cu', '.cpp', '.cc', '.cxx', '.c']) {
                    const implementation = stem + extension;
                    if (files.has(implementation) && implementation !== document.uri.fsPath) {
                        found.add(implementation);
                        pending.push(implementation);
                    }
                }
            }
        }
        for (const name of info.references) {
            if (info.definitions.has(name)) continue;
            for (const target of providers.get(name) || []) {
                if (target === document.uri.fsPath) continue;
                found.add(target);
                if (!visited.has(target)) pending.push(target);
            }
        }
    }
    return [...found].filter(file => /\.(cu|c|cc|cpp|cxx)$/i.test(file) && !includedSources.has(file)).sort();
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
    const isShaderFile = /\.(vert|frag|comp|glsl)$/i.test(filename);
    const isKernelFile = /\.(cu|cuh)$/i.test(filename);

    const sourceText = editor.document.getText().replace(
        /\/\*[\s\S]*?\*\/|\/\/[^\r\n]*/g,
        comment => comment.replace(/[^\r\n]/g, ' ')
    );

    const includeShaderFile = /^[ \t]*#[ \t]*include[ \t]*["<][^"<>\r\n]+\.(vert|frag|comp|glsl)[">]/im.test(sourceText);
    const includeKernelFile = /^[ \t]*#[ \t]*include[ \t]*["<][^"<>\r\n]+\.(cu|cuh)[">]/im.test(sourceText);

    if(isPythonFile){
        LOG && log('debugPython', 'isPythonFile', isPythonFile, filename)
        await debugPython(editor);
        return;
    }
    else if(isCppFile && !includeShaderFile && !includeKernelFile){
        LOG && log('buildCpp, debugCpp', 'isCppFile', isCppFile, 'includeShaderFile', includeShaderFile, 'includeKernelFile', includeKernelFile, filename)
        if (!await buildCpp(editor)) return;
        await debugCpp(editor);
    }
    else if(isShaderFile || includeShaderFile){
        // shader file or call shader file must have main()
        if(!isExistMain(filename)){
            LOG && log('isShaderFile', isShaderFile, 'includeShaderFile', includeShaderFile, !isExistMain, filename)
            return;
        }
        let shaderName = "";
        let source = "";
        if(isShaderFile){
            shaderName = path.basename(filename);
            source = `${filename}.cpp`;
        }
        else{
            shaderName = filename.replace(/\.(c|cpp|h)$/i, '');
            source = filename;
        }
        LOG && log('isShaderFile', isShaderFile, 'shaderName', shaderName, 'source', source)
            
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
        let source = filename;
        if (!isExistMain(filename)) {
            source = `${filename}.cu`;
            const wrapper = `#include <stdio.h>\n#include <cuda_runtime.h>\n#include "${path.basename(filename)}"\n\nint main()\n{\n    printf("hello mingjie!");\n    return 0;\n}\n`;
            try {
                await fs.promises.writeFile(source, wrapper, { encoding: 'utf8', flag: 'wx' });
            } catch (e) {
                if (e.code !== 'EEXIST') {
                    vscode.window.showErrorMessage(`mem: ${e.message}`);
                    return;
                }
            }
        }
        if (!await buildKernel(editor, source)) return;
        await debugCpp(editor, 'cuda-gdb');
    }
}

async function scriptPython() {
    const editor = vscode.window.activeTextEditor;
    if (!editor) {
        log('vscode.window.activeTextEditor; !editor return');
        return;
    }
    
    if (!await editor.document.save()) return;

    const filename = editor.document.uri.fsPath;
    const isPythonFile = editor.document.languageId === 'python' || /\.py$/i.test(filename);
    const isCppFile = /\.(c|cpp|h|hpp)$/i.test(filename);   
    const isShaderFile = /\.(vert|frag|comp|glsl)$/i.test(filename);
    const isKernelFile = /\.(cu|cuh)$/i.test(filename);

    if(isPythonFile){
        return filename;
    }
    
    const source = `${filename}.py`;
    const wrapper = "print('hello mingjie!')\n";
    try {
        await fs.promises.writeFile(source, wrapper, { encoding: 'utf8', flag: 'wx' });
    } catch (e) {
        if (e.code !== 'EEXIST') {
            log('Error creating script wrapper', e.message);
            vscode.window.showErrorMessage(`mem: ${e.message}`);
            return;
        }
    } 
    
    return source;
}

async function scriptDebug(filename) {
    const folder = vscode.workspace.getWorkspaceFolder(vscode.Uri.file(filename));
    if (!folder) {
        vscode.window.showErrorMessage('mem: Open the project folder before starting automatic capture.');
        return;
    }
    const scriptPath = (/\.py$/i.test(filename)
        ? filename
        : path.join(folder.uri.fsPath, 'mem', 'capture_volume_auto.py')).replace(/\\/g, '/');
    await fs.promises.access(scriptPath, fs.constants.R_OK);
    const configurations = vscode.workspace.getConfiguration('launch', folder.uri)
        .get('configurations', []);
    const configuration = configurations.find(config => config.name === 'GDB'
        && config.type === 'cppdbg' && config.request === 'launch'
        && (!config.MIMode || config.MIMode === 'gdb'));
    if (!configuration) {
        vscode.window.showErrorMessage('mem: Add a GDB launch configuration named "GDB" to launch.json.');
        return;
    }

    // Run inside GDB, where the gdb Python module and debug symbols exist.
    // GDB's source command treats quotes as literal filename characters.
    const scriptCommand = `python import runpy; runpy.run_path(${JSON.stringify(scriptPath)}, run_name='__main__')`;
    const setupCommands = (configuration.setupCommands || []).filter(command => {
        const match = /^\s*source\s+(.+?)\s*$/.exec(command.text || '');
        const sourcePath = match && match[1].replace(/^"(.*)"$/, '$1')
            .replace(/\$\{workspaceFolder\}/g, folder.uri.fsPath).replace(/\\/g, '/');
        return sourcePath !== scriptPath && command.text !== scriptCommand;
    });
    setupCommands.push({
        text: scriptCommand,
        description: 'Capture shader inputs every two seconds',
        ignoreFailures: false
    });
    const environment = (configuration.environment || [])
        .filter(entry => entry.name !== 'VOLUME_AUTO_CAPTURE');
    environment.push({ name: 'VOLUME_AUTO_CAPTURE', value: '1' });

    // Start explicitly and await configuration resolution and the build task.
    // This does not depend on the dropdown selection or a temporary provider.
    const started = await vscode.debug.startDebugging(folder, {
        ...configuration,
        name: 'GDB: Automatic volume capture',
        stopAtEntry: false,
        setupCommands,
        environment,
        internalConsoleOptions: 'openOnSessionStart'
    });
    if (!started) {
        vscode.window.showErrorMessage('mem: Could not start automatic volume capture.');
    }
}

async function cmdScript() {
    if (vscode.debug.activeDebugSession) {
        vscode.window.showErrorMessage('mem: Stop the current debug session before starting automatic capture.');
        return;
    }
    if (!await vscode.workspace.saveAll()) return;
    
    try {
        const filename = await scriptPython();
        if (!filename) return;
        await scriptDebug(filename);
    } catch (e) {
        log('Error cmdScript', e.message);
        vscode.window.showErrorMessage(`mem: ${e.message}`);
    }
}

async function activate(context) {
    await new Promise(resolve => setTimeout(resolve, 1000));

    if (await isOpeningProject(context)) {
        return;
    }

    await registerShaderDefinitions(vscode, context);
    await applySettings();

    const initializedSessions = new WeakSet();

    const cmd = vscode.commands.registerCommand('mem', async () => { // F8
        await cmdMem(initializedSessions);
    });

    const debugCmd = vscode.commands.registerCommand('debug', async () => { // F6
        await cmdDebug();
    });

    const scriptCmd = vscode.commands.registerCommand('script', async () => { // F7
        await cmdScript();
    });

    context.subscriptions.push(cmd, debugCmd, scriptCmd);
}

function deactivate() {}
module.exports = { activate, deactivate };
