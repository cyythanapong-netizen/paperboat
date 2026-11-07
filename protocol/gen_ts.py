"""Generate the shared protocol types (packages/shared/src/protocol.ts) from the JSON Schema
(protocol/schema/paperboat-0.schema.json), the source of truth for message shapes.

    python3 protocol/gen_ts.py           # write protocol.ts
    python3 protocol/gen_ts.py --check   # exit 1 if protocol.ts is out of date (CI)

Each $defs entry becomes an exported type of the same name; the messages each way become
the unions ServerMsg ($defs/toUI) and ClientMsg ($defs/toBackend). Descriptions become doc
comments. Knows only the parts of JSON Schema the protocol uses.
"""

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCHEMA = ROOT / "protocol" / "schema" / "paperboat-0.schema.json"
OUT = ROOT / "packages" / "shared" / "src" / "protocol.ts"


def doc(text: str | None, indent: str = "") -> str:
    if not text:
        return ""
    return f"{indent}/** {text} */\n"


def union(parts: list[str]) -> str:
    unique = list(dict.fromkeys(parts))
    return " | ".join(unique)


def ts(s: dict, defs: dict, indent: str = "") -> str:
    if "$ref" in s:
        return s["$ref"].split("/")[-1]
    if "const" in s:
        return json.dumps(s["const"])
    if "enum" in s:
        return union([json.dumps(v) for v in s["enum"]])
    if "oneOf" in s or "anyOf" in s:
        return union([ts(x, defs, indent) for x in s.get("oneOf", s.get("anyOf"))])
    t = s.get("type")
    if isinstance(t, list):
        return union([ts({**s, "type": x}, defs, indent) for x in t])
    if t == "string":
        return "string"
    if t in ("number", "integer"):
        return "number"
    if t == "boolean":
        return "boolean"
    if t == "array":
        item = ts(s.get("items", {}), defs, indent)
        n = s.get("minItems")
        if n is not None and n == s.get("maxItems") and n <= 4:
            return "[" + ", ".join([item] * n) + "]"
        return f"({item})[]" if "|" in item else f"{item}[]"
    if t == "object" or "properties" in s:
        props = s.get("properties", {})
        extra = s.get("additionalProperties")
        if not props:
            if isinstance(extra, dict):
                return f"Record<string, {ts(extra, defs, indent)}>"
            return "Record<string, unknown>"
        required = set(s.get("required", []))
        inner = indent + "  "
        lines = ["{"]
        for k, v in props.items():
            key = k if k.isidentifier() else json.dumps(k)
            lines.append(doc(v.get("description"), inner) + f"{inner}{key}{'' if k in required else '?'}: {ts(v, defs, inner)};")
        lines.append(indent + "}")
        return "\n".join(line for line in lines)
    return "unknown"


def messages(direction: dict, defs: dict) -> str:
    names = direction["properties"]["type"]["enum"]
    return "\n".join(f"  | {ts(defs[n], defs, '  ')}" for n in names)


def generate() -> str:
    schema = json.loads(SCHEMA.read_text())
    defs = schema["$defs"]
    out = [
        "// Generated from protocol/schema/paperboat-0.schema.json by protocol/gen_ts.py.",
        "// Do not edit: change the schema and run `npm run gen:protocol`.",
        "",
    ]
    skip = {"toUI", "toBackend"} | set(defs["toUI"]["properties"]["type"]["enum"]) | set(
        defs["toBackend"]["properties"]["type"]["enum"]
    )
    for name, s in defs.items():
        if name in skip:
            continue
        out.append(doc(s.get("description")) + f"export type {name} = {ts(s, defs)};")
        out.append("")
    out.append(doc(defs["toUI"].get("description")) + "export type ServerMsg =\n" + messages(defs["toUI"], defs) + ";")
    out.append("")
    out.append(doc(defs["toBackend"].get("description")) + "export type ClientMsg =\n" + messages(defs["toBackend"], defs) + ";")
    out.append("")
    return "\n".join(out)


def main() -> None:
    text = generate()
    if "--check" in sys.argv:
        if not OUT.exists() or OUT.read_text() != text:
            sys.exit("packages/shared/src/protocol.ts is out of date: run npm run gen:protocol")
        print("protocol.ts is current")
        return
    OUT.write_text(text)
    print(f"wrote {OUT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
