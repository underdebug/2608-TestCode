"""Update CMake and VS Code paths. Run: python3 buildpath.py."""

import argparse
import json
from pathlib import Path
import re


DEFAULT_BUILD_DIRECTORY = "${workspaceFolder}/../../../build/${workspaceFolderBasename}"


def update_project(project_dir, build_directory):
    cmake_path = project_dir / "CMakeLists.txt"
    settings_path = project_dir / ".vscode" / "settings.json"
    tasks_path = project_dir / ".vscode" / "tasks.json"
    cmake = cmake_path.read_text(encoding="utf-8")
    settings = json.loads(settings_path.read_text(encoding="utf-8")) if settings_path.exists() else {}
    tasks = json.loads(tasks_path.read_text(encoding="utf-8")) if tasks_path.exists() else None

    # A final property block overrides earlier output-directory settings.
    block = (
        "# Managed by buildpath.py\n"
        "set_target_properties(${PROJECT_NAME} PROPERTIES\n"
        "    CUDA_SEPARABLE_COMPILATION ON\n"
        "    RUNTIME_OUTPUT_DIRECTORY \"${CMAKE_SOURCE_DIR}\"\n"
        ")\n"
        "# End buildpath.py\n"
    )
    cmake = re.sub(
        r"(?m)^# Managed by buildpath\.py\n.*?^# End buildpath\.py\n?",
        "", cmake, flags=re.DOTALL,
    ).rstrip() + "\n\n" + block
    settings["cmake.buildDirectory"] = build_directory.rstrip("/")
    settings["C_Cpp.default.compileCommands"] = (
        settings["cmake.buildDirectory"] + "/compile_commands.json"
    )
    if tasks is not None:
        for task in tasks.get("tasks", []):
            command = task.get("command")
            if isinstance(command, str) and task.get("label") == "cmake":
                task["command"] = re.sub(
                    r'(\s-B\s+|\bcmake\s+--build\s+)("[^"\n]*"|\S+)',
                    lambda match: match.group(1) + '"' + settings["cmake.buildDirectory"] + '"',
                    command,
                )

    # Validate both inputs before changing either file.
    cmake_path.write_text(cmake, encoding="utf-8")
    settings_path.parent.mkdir(parents=True, exist_ok=True)
    settings_path.write_text(json.dumps(settings, indent=4) + "\n", encoding="utf-8")
    if tasks is not None:
        tasks_path.write_text(json.dumps(tasks, indent=4) + "\n", encoding="utf-8")
        print(f"Updated {tasks_path}")
    print(f"Updated {cmake_path}")
    print(f"Updated {settings_path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project-dir", type=Path, default=Path(__file__).resolve().parent)
    parser.add_argument("--build-directory", default=DEFAULT_BUILD_DIRECTORY)
    args = parser.parse_args()
    if not args.build_directory.strip():
        parser.error("--build-directory must not be empty")
    try:
        update_project(args.project_dir.resolve(), args.build_directory)
    except (OSError, ValueError) as error:
        parser.exit(1, f"buildpath.py: {error}\n")


if __name__ == "__main__":
    main()
