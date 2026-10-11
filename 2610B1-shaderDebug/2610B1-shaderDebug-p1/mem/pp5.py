

    else{ // python program, python code
      if(isPythonFile){ // debug in python code 
        if (editor.selection.isEmpty) { // get cursor text
          const pos = editor.selection.active;               
          const range = editor.document.getWordRangeAtPosition(pos);

          if (range){
            text = editor.document.getText(range); // get cursor text
            line = editor.selection.active.line + 1;

            expression = `print(${text})`;
            # expression = `print(round(${text}, 3), '<= ${text}', list(${text}.shape) if hasattr(${text}, 'shape') and ${text}.shape else '', '${line}')`;

            try {
              await session.customRequest('evaluate', {
                expression: expression,
                context: 'repl',
                frameId: frameId
              });
              
              //vscode.commands.executeCommand('workbench.panel.repl.view.focus');
            } catch (e) {
              //vscode.window.showErrorMessage('mem expression failed: ' + e.message);
              return;
            } 
          }                                    
          else{
            line = editor.selection.active.line;
            text = editor.document.lineAt(line).text;
            text = JSON.stringify(text);

            try {
              await session.customRequest('evaluate', {
                expression: text,
                context: 'repl',
                frameId: frameId
              });
              
              //vscode.commands.executeCommand('workbench.panel.repl.view.focus');
            } catch (e) {
              //vscode.window.showErrorMessage('mem expression failed: ' + e.message);
              return;
            } 
          }         
        } 
        else { // get selection
          text = editor.document.getText(editor.selection); // get selection
          text = JSON.stringify(text);          

          try {
            await session.customRequest('evaluate', {
              expression: text,
              context: 'repl',
              frameId: frameId
            });
          } catch (e) {
            //vscode.window.showErrorMessage('mem expression failed: ' + e.message);
            return;
          }             
        }
      }
    }