
const vscode = require('vscode');
const path = require('path');
const fs = require('fs');

let LOG = 0; 

function log(...args) {
    const stack = new Error().stack;
    const m = stack.split('\n')[2]?.match(/:(\d+):\d+\)?$/);
    const line = m ? m[1] : '';

    vscode.debug.activeDebugConsole.appendLine(
        `>${line} ${args.join(' ')}`
    );
}

function readConfig() {
    const file = path.join(__dirname, 'config.json');

    try {
        const text = fs.readFileSync(file, 'utf8');
        return JSON.parse(text);
    } catch (e) {
        log('Error readConfig readFileSync', e.message)
        return { log: 1 };
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

async function openDebugConsoleOnStart() {
    const cfg = vscode.workspace.getConfiguration('launch');

    const configurations = cfg.get('configurations', []);

    let changed = false;

    for (const config of configurations) {
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

async function memNoPython(editor) {
    try {
        if (editor.selection.isEmpty) {
            LOG && log('memNoPython', 'editor.selection.isEmpty true');
            await vscode.commands.executeCommand('python.execInTerminal', editor.document.uri);
        } else {
            LOG && log('memNoPython', 'editor.selection.isEmpty false');
            await vscode.commands.executeCommand('python.execSelectionInTerminal');
        }
    } catch (e) {
        log('Error memNoPython', 'editor.selection.isEmpty', editor.selection.isEmpty, e.message);
    }
}

async function memCppCpp(session) {
    const editor = vscode.window.activeTextEditor;
    const line = editor.selection.active.line + 1;
    const frameId = await getFrameId(session);

    let text;
    if (editor.selection.isEmpty) {
        const pos = editor.selection.active;
        const range = editor.document.getWordRangeAtPosition(pos);
        LOG && log('memCppCpp', 'pos', pos, 'range', range);
        if (!range)
            return

        text = editor.document.getText(range);
        LOG && log('memCppCpp', line, text);
    } else {
        text = editor.document.getText(editor.selection);
    }

    expression = `${text} = mem('${text}'); print(${text}, '<= ${text}', list(${text}.shape) if hasattr(${text}, 'shape') and ${text}.shape else '', '|${line}|')`;
    expression = JSON.stringify(expression);

    const expr = `-exec python exec(${expression})`;
    LOG && log('memCppCpp', expr);

    try {
        await session.customRequest('evaluate', {
            expression: expr,
            context: 'repl',
            frameId: frameId
        });
    } catch (e) {
        log('Error memCppCpp', expr, e.message);
    }
}

async function memCppPython(session) {
    const editor = vscode.window.activeTextEditor;
    const line = editor.selection.active.line + 1;
    const frameId = await getFrameId(session);

    let text;
    if (editor.selection.isEmpty) {
        text = editor.document.getText();
        LOG && log('memCppPython', 'editor.document.getText()', text);
    } else {
        text = editor.document.getText(editor.selection);
        LOG && log('memCppPython', 'editor.document.getText(editor.selection)', text);
    }

    const lines = text.split(/\r?\n/).map(line => line.replace(/\s+$/, ''));
    expression = lines.join(' \n');
    expression = JSON.stringify(expression);

    const expr = `-exec python exec(${expression})`;
    LOG && log('memCppPython', expr);

    try {
        await session.customRequest('evaluate', {
            expression: expr,
            context: 'repl',
            frameId: frameId
        });
    } catch (e) {
        log('Error memCppPython', expr, e.message);
    }
}

async function memPythonPython(session) {
    const editor = vscode.window.activeTextEditor;
    const line = editor.selection.active.line + 1;
    const frameId = await getFrameId(session);

    if (editor.selection.isEmpty) {
        const pos = editor.selection.active;
        const range = editor.document.getWordRangeAtPosition(pos);
        if (range) {
            const text = editor.document.getText(range);
            expression = `print(${text}, '<= ${text}', list(${text}.shape) if hasattr(${text}, 'shape') and ${text}.shape else '', '⌊${pos.line + 1}⌉')`;
            LOG && log('memPythonPython', 'editor.document.getText(range)', text);
        } else {
            expression = editor.document.lineAt(pos.line).text;
            LOG && log('memPythonPython', 'editor.document.lineAt(pos.line).text', text);
        }
    } else {
        expression = editor.document.getText(editor.selection);
        LOG && log('memPythonPython', 'editor.document.getText(editor.selection)', text);
    }

    const expr = expression;
    LOG && log('memPythonPython', expr);

    try {
        await session.customRequest('evaluate', {
            expression: expr,
            context: 'repl',
            frameId: frameId
        });
    } catch (e) {
        log('Error memPythonPython', expr, e.message);
    }
}

async function activate(context) {
    const config = readConfig();
    LOG = config.log;

    LOG && log('disableCollapseIdenticalLines()');
    await disableCollapseIdenticalLines();

    LOG && log('openDebugConsoleOnStart()');
    await openDebugConsoleOnStart();

    const initializedSessions = new WeakSet();

    const cmd = vscode.commands.registerCommand('mem', async () => {
        const editor = vscode.window.activeTextEditor;
        if (!editor) {
            log('vscode.window.activeTextEditor; !editor return');
            return;
        }

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
                log('!session memNoPython(editor)');
                await memNoPython(editor);   
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
                initializedSessions.add(session);
                LOG && log('memInit(session) C++ program, initializedSessions'); 
                await memInit(session)
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
                LOG && log('memCppPython(session) Python program, Python file')
                await printStack(session)
                await memPythonPython(session);
            }
        }
    });
    context.subscriptions.push(cmd);
}

function deactivate() {}
module.exports = { activate, deactivate };
