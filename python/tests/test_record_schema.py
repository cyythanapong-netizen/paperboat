"""The run record schema (#277): a record like the writer makes is valid, a broken one is not."""

import json
from pathlib import Path

import jsonschema
import pytest
from referencing import Registry, Resource

ROOT = Path(__file__).resolve().parents[2]
SCHEMA = ROOT / "protocol" / "schema"
PROTOCOL = json.loads((SCHEMA / "paperboat-0.schema.json").read_text())
RECORD = json.loads((SCHEMA / "record-0.schema.json").read_text())


def validator() -> jsonschema.Draft202012Validator:
    registry = Registry().with_resource("https://paperboat.dev/protocol/paperboat-0.schema.json", Resource.from_contents(PROTOCOL))
    return jsonschema.Draft202012Validator(RECORD, registry=registry)


def record() -> dict:
    return {
        "paperboat": 0,
        "run": {"id": "run-1", "status": "done", "started": "2026-11-10T14:48:54Z", "finished": "2026-11-10T14:49:04Z"},
        "backend": {"name": "rig", "version": "0.2.0"},
        "tool_versions": {"paperboat": "0.1.0-alpha.3"},
        "command": "paperboat run --record out",
        "flow": {"nodes": [], "edges": []},
        "variables": [],
        "settings": {"duration": 1.0},
        "signals": [
            {"type": "signal", "node": "show", "t": 0.0, "v": 1.0},
            {"type": "signal", "node": "scope", "port": "a", "t": 0.5, "v": [1.0, 2.0], "components": [{"id": "x"}, {"id": "y"}]},
        ],
        "scene": {
            "base": {"mass": {"id": "mass", "shape": "box", "pos": [0.0, 0.0, 0.0]}},
            "frames": [{"t": 0.5, "entities": [{"id": "mass", "pos": [1.0, 0.0, 0.0]}]}],
        },
    }


def test_a_written_record_validates():
    validator().validate(record())


def test_a_record_without_signals_or_of_a_newer_version_is_refused():
    with pytest.raises(jsonschema.ValidationError):
        validator().validate({**record(), "paperboat": 1})
    with pytest.raises(jsonschema.ValidationError):
        validator().validate({**record(), "signals": [{"node": "show", "t": 0.0, "v": 1.0}]})  # no type: not a signal message
