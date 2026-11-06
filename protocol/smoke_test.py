"""Protocol smoke test for any Paperboat backend.

Connects to a running backend, checks the handshake, then runs the backend's own starter
graph (from graph.load), a graph with a cycle, one with a block the backend doesn't have,
and a cancelled run. Every message either way is checked against the protocol's JSON
Schema (schema/paperboat-0.schema.json). Knows no domain, so every backend (simulation or
not, any language, the C++ engine) must pass it. Exits non-zero on failure.
"""

import argparse
import asyncio
import json
import sys
from pathlib import Path

import jsonschema
from websockets.asyncio.client import ClientConnection, connect

import os
os.environ["NO_PROXY"] = "localhost,127.0.0.1"
os.environ["no_proxy"] = "localhost,127.0.0.1"

SCHEMA = json.loads((Path(__file__).parent / "schema" / "paperboat-0.schema.json").read_text())
TO_UI = jsonschema.Draft202012Validator({**SCHEMA, "$ref": "#/$defs/toUI"})
TO_BACKEND = jsonschema.Draft202012Validator({**SCHEMA, "$ref": "#/$defs/toBackend"})

def check(cond: bool, what: str) -> None:
    if not cond:
        print(f"FAIL: {what}")
        sys.exit(1)


def conforms(validator: jsonschema.Draft202012Validator, msg: dict, way: str) -> None:
    errors = sorted(validator.iter_errors(msg), key=lambda e: e.path)
    check(not errors, f"{way} {msg.get('type')} breaks the schema: " + "; ".join(
        f"{'/'.join(map(str, e.path)) or '(message)'}: {e.message[:120]}" for e in errors[:3]))


async def recv(ws: ClientConnection) -> dict:
    msg = json.loads(await asyncio.wait_for(ws.recv(), timeout=20))
    conforms(TO_UI, msg, "backend sent")
    return msg


async def send(ws: ClientConnection, msg: dict, check_schema: bool = True) -> None:
    if check_schema:  # off only to send a deliberately bad message
        conforms(TO_BACKEND, msg, "test would send")
    await ws.send(json.dumps(msg))


def cycle_graph(blocks: list[dict]) -> dict:
    """Two copies of any block with a signal input and output, wired into a loop.

    Step ports are skipped: a loop of steps is allowed (Repeat), a loop of signals is not.
    """
    def signal_ports(ports: list[dict]) -> list[str]:
        return [p["id"] for p in ports if p.get("type", "signal") != "step"]

    # Linked blocks belong to station objects, so they can't be copied freely; step blocks
    # (even with signal ports) read their inputs after the pass, so they never loop: skip both.
    # A block without feedthrough (an integrator, a delay) may close a loop: skip it too.
    def plain(b: dict) -> bool:
        return (not b.get("linked") and b.get("feedthrough", True)
                and all(p.get("type", "signal") != "step" for p in b["inputs"] + b["outputs"]))
    b = next((b for b in blocks if plain(b) and signal_ports(b["inputs"]) and signal_ports(b["outputs"])), None)
    if b is None:
        print("FAIL: catalog has no block with both a signal input and output")
        sys.exit(1)
    i, o = signal_ports(b["inputs"])[0], signal_ports(b["outputs"])[0]
    return {
        "nodes": [{"id": "c1", "type": b["type"], "params": {}}, {"id": "c2", "type": b["type"], "params": {}}],
        "edges": [{"from": ["c1", o], "to": ["c2", i]}, {"from": ["c2", o], "to": ["c1", i]}],
    }


async def run_until_finished(ws: ClientConnection, graph: dict, cancel_after: float | None = None,
                             settings: dict | None = None, check_schema: bool = True) -> list[dict]:
    await send(ws, {"type": "graph.run", "graph": graph, **({"settings": settings} if settings else {})}, check_schema)
    if cancel_after is not None:
        async def cancel() -> None:
            await asyncio.sleep(cancel_after)
            await send(ws, {"type": "run.cancel"})
        asyncio.create_task(cancel())
    msgs = []
    while True:
        msg = await recv(ws)
        msgs.append(msg)
        if msg["type"] == "run.finished":
            return msgs


async def main() -> None:
    parser = argparse.ArgumentParser(description="Paperboat protocol smoke test")
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()

    async with connect(f"ws://localhost:{args.port}", proxy=None) as ws:
        # The handshake, up to graph.load: hello and catalog first, then the scene; a backend
        # with variables also sends variables.load and variables.values, in among them.
        first = {}
        while "graph.load" not in first:
            msg = await recv(ws)
            check(msg["type"] not in first or msg["type"] == "variables.values", f"{msg['type']} twice in the handshake")
            first[msg["type"]] = msg
        check(list(first)[:2] == ["hello", "catalog"], "hello, then catalog, come first")
        check(set(first) <= {"hello", "catalog", "scene", "variables.load", "variables.values", "graph.load"},
              f"unexpected handshake messages: {sorted(set(first))}")
        hello, catalog, loaded = first["hello"], first["catalog"], first["graph.load"]
        scene = first.get("scene", {"type": "missing", "entities": []})
        check(hello["protocol"] == "paperboat/0", "hello")
        check(catalog["type"] == "catalog" and len(catalog["blocks"]) > 0, "catalog")
        # A built-in scene must show something; a graph or web station may have no entities.
        view = hello.get("station", {}).get("view", "scene")
        check(scene["type"] == "scene" and (len(scene["entities"]) > 0 or view != "scene"), "scene")
        check(loaded["type"] == "graph.load" and "nodes" in loaded["graph"], "graph.load")
        blocks = {b["type"]: b for b in catalog["blocks"]}
        starter = loaded["graph"]
        for n in starter["nodes"]:
            check(n["type"] in blocks, f"starter graph uses unknown block {n['type']}")
        monitors = {n["id"] for n in starter["nodes"] if blocks[n["type"]]["kind"] == "monitor"}
        # A descriptor may have no starter graph: then only the handshake and cycle checks run.
        empty = not starter["nodes"]
        if not empty:
            check(len(monitors) > 0, "starter graph has no monitor node")
        print(f"ok  handshake: {hello['engine']}, {len(blocks)} blocks, {len(scene['entities'])} entities, "
              f"starter graph of {len(starter['nodes'])} nodes")

        if catalog.get("objects"):
            station = await recv(ws)
            check(station["type"] == "station.load", "station.load after graph.load (catalog has objects)")
            ids = {o["id"] for o in station["station"]["objects"]}
            linked = [n["id"] for n in starter["nodes"] if blocks[n["type"]].get("linked")]
            check(all(i in ids for i in linked), "a linked block in the starter graph has no station object")
            print(f"ok  station: {len(ids)} objects, {len(linked)} linked blocks")

        if empty:
            print("--  starter graph is empty: starter run and cancel skipped")
        else:
            await starter_and_cancel(ws, starter, monitors)

        msgs = await run_until_finished(ws, cycle_graph(catalog["blocks"]))
        check(msgs[-1]["status"] == "failed", f"cycle run status {msgs[-1]['status']}")
        print("ok  cycle rejected")

        # A block the backend doesn't have (an old graph, another backend's): the run fails
        # and names it, so the UI can mark it.
        lost = {"nodes": [{"id": "lost-1", "type": "no-such-block-type", "params": {"k": 2}}], "edges": []}
        msgs = await run_until_finished(ws, lost)
        check(msgs[-1]["status"] == "failed", f"unknown block run status {msgs[-1]['status']}")
        check(any(m["type"] == "node.state" and m["node"] == "lost-1" and m["state"] == "failed" for m in msgs),
              "unknown block not reported as node.state failed")
        print("ok  unknown block named")

    print("PASS")


async def starter_and_cancel(ws: ClientConnection, starter: dict, monitors: set[str]) -> None:
    """Run the starter graph to the end, then again with a cancel."""
    msgs = await run_until_finished(ws, starter)
    signals = [m for m in msgs if m["type"] == "signal" and m["node"] in monitors]
    scene_updates = [m for m in msgs if m["type"] in ("scene", "scene.patch")]
    progress = max((m["fraction"] for m in msgs if m["type"] == "run.progress"), default=0)
    check(msgs[-1]["status"] == "done", f"starter run status {msgs[-1]['status']}")
    check({m["node"] for m in signals} == monitors, "not every monitor node got signals")
    check(len(scene_updates) >= 1, "run never updated the scene")
    check(progress >= 0.99, f"progress only reached {progress}")
    print(f"ok  starter run: {len(signals)} signals, {len(scene_updates)} scene updates, progress {progress:.2f}")

    msgs = await run_until_finished(ws, starter, cancel_after=0.5)
    # A one-shot backend may finish before the cancel arrives; that is fine.
    status = msgs[-1]["status"]
    check(status in ("cancelled", "done"), f"cancel run status {status}")
    print("ok  cancel" if status == "cancelled" else "ok  cancel (run finished first)")

    # The UI may change a run's length and speed; a bad value fails the run, not the backend.
    msgs = await run_until_finished(ws, starter, settings={"duration": 0.1, "speed": 0})
    check(msgs[-1]["status"] == "done", f"run with settings: status {msgs[-1]['status']}")
    msgs = await run_until_finished(ws, starter, settings={"dt": 0}, check_schema=False)
    check(msgs[-1]["status"] == "failed", "a run with dt = 0 did not fail")
    print("ok  run settings")


if __name__ == "__main__":
    asyncio.run(main())
