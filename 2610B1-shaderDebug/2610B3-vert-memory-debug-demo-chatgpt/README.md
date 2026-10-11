# C++ shader header demo

Linux 示例，需要 CMake、Python 3、g++、GDB，以及 VS Code 的 Microsoft C/C++ 扩展。

## VS Code 调试

1. 在 VS Code 中打开整个项目文件夹。
2. 在 `main.cpp` 的 `square(input)` 调用行设置断点。
3. 按 F5，选择 `Debug C++ shader demo (GDB)`。
4. VS Code 自动执行 CMake 配置和构建，然后启动 GDB。
5. 在断点处按 F11 进入 `square()`；将打开原始的 `shaders/example.vert`。
6. 用 F10 单步执行，在变量窗口查看 `x` 和初始化后的 `result`。

`launch.json` → `CMake: build` → `CMake: configure` → `CMakeLists.txt`。

## 命令行

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
./build/shader_cpp_demo
gdb ./build/shader_cpp_demo
```

预期输出：`square(3) = 9`。

## 构建过程

`scripts/compile_in_memory.py` 作为 CMake 的 C++ compiler launcher，读取源码，
在内存中将 shader 的 `#version 450` 转换成 `//version 450`，并展开 main.cpp 中
唯一的 `#include "shaders/example.vert"`。组合后的源码通过标准输入传给编译器，不保存修改后的源码。
对象文件和可执行文件仍写入 build。该脚本是此示例专用的展开器，不是通用 C++ 预处理器。

插入的 `#line` 指令将 shader 代码映射到原始 shader 路径和行号，随后恢复 main.cpp
的路径和行号。因此可以直接在 `shaders/example.vert` 第 4 行或 main.cpp 第 6 行打断点。
修改 shader 或 launcher 后，CMake 会重新编译。移动项目后应重新构建，以更新绝对路径。
VS Code F5 调试支持此映射；静态 IntelliSense 直接读原始文件，仍可能提示 GLSL 指令错误。

```text
(gdb) break shaders/example.vert:4
(gdb) run
(gdb) print x
(gdb) next
(gdb) print result
```

预期分别看到 x = 3、result = 9。
本示例将共享函数编译为 CPU C++，不生成 SPIR-V，也不运行 GPU shader。
原始 .vert 包含完整 GLSL main 入口。Python 在内存中将 main() 重命名为 shader()，
并去掉此示例的 layout(location = ...) in 限定符。shader_cpu.hpp 提供 CPU 版本的
vec3、vec4 和 gl_Position，main.cpp 调用 shader()。原始 .vert 文件保持不变。
此兼容层仅支持当前示例使用的 GLSL 操作，不是通用 GLSL 编译器。

F9 添加/取消断点；F5 启动/继续；F10 单步执行；F11 进入函数。
