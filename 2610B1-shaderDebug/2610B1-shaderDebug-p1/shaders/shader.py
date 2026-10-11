"""Generate C++ headers beside the shader sources in this directory."""

from pathlib import Path
import re


def generate_headers():
    shader_dir = Path(__file__).resolve().parent
    for source in sorted(shader_dir.iterdir()):
        if not source.is_file() or source.suffix not in {".vert", ".frag", ".glsl"}:
            continue
        code = source.read_text(encoding="utf-8")
        code = re.sub(r"^(\s*#[^\r\n]*)", lambda match: match[0].replace("#", "//", 1), code, flags=re.MULTILINE)
        code = re.sub(r"\bmain\s*(?=\()", "_main", code)
        header = source.with_name(source.name + ".hpp")
        if not header.exists() or header.read_text(encoding="utf-8") != code:
            header.write_text(code, encoding="utf-8")


if __name__ == "__main__":
    generate_headers()
