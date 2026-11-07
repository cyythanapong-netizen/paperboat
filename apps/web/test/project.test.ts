import assert from "node:assert/strict";
import { test } from "node:test";
import type { Graph } from "../src/protocol.ts";
import {
  addRecent,
  ago,
  buildProject,
  parseProject,
  PROJECT_EXT,
  PROJECT_VERSION,
  projectFileName,
  projectName,
  RECENT_MAX,
  stringify,
  type Stored,
} from "../src/project.ts";

const flow: Graph = {
  nodes: [
    { id: "gain-1", type: "gain", params: { k: 2 }, pos: [10, 20] },
    { id: "sum-1", type: "sum", pos: [200, 20] },
  ],
  edges: [{ from: ["gain-1", "y"], to: ["sum-1", "a"] }],
};

const ok = (text: string) => {
  const r = parseProject(text);
  assert.ok("project" in r, "error" in r ? r.error : "");
  return r.project;
};
const bad = (data: unknown) => {
  const r = parseProject(typeof data === "string" ? data : JSON.stringify(data));
  assert.ok("error" in r, "expected an error");
  return r.error;
};

test("a project is built from its parts and leaves out what is empty", () => {
  const p = buildProject({ backend: "cart-pole", flow, station: null, variables: [], run: {} });
  assert.deepEqual(Object.keys(p), ["paperboat", "backend", "flow"]);
  assert.equal(p.paperboat, PROJECT_VERSION);
  const full = buildProject({
    backend: "cart-pole",
    flow,
    station: { objects: [{ id: "cart-1", type: "cartpole" }] },
    variables: [{ name: "L", type: "number", value: 1 }],
    dashboard: { instruments: [] } as never,
    run: { duration: 5 },
  });
  assert.deepEqual(Object.keys(full), ["paperboat", "backend", "flow", "station", "variables", "dashboard", "run"]);
});

test("what is written is read back unchanged", () => {
  const p = buildProject({ backend: "b", flow, variables: [{ name: "x", type: "boolean", value: true }], run: { speed: 2 } });
  assert.deepEqual(ok(stringify(p)), p);
});

test("the text is indented and ends with a newline, so a diff reads", () => {
  const text = stringify(buildProject({ backend: "b", flow }));
  assert.ok(text.includes('\n  "backend": "b"'));
  assert.ok(text.endsWith("}\n"));
});

test("a file that is not JSON, or not a project, says so", () => {
  assert.match(bad("{nope"), /not valid JSON/);
  assert.match(bad([1, 2]), /no "paperboat" version/);
  assert.match(bad({ flow }), /no "paperboat" version/);
});

test("a newer format is refused and says which", () => {
  const e = bad({ paperboat: PROJECT_VERSION + 1, backend: "b", flow });
  assert.match(e, /format 1/);
  assert.match(e, /newer Paperboat/);
  assert.match(bad({ paperboat: "0", backend: "b", flow }), /whole number/);
  assert.match(bad({ paperboat: -1, backend: "b", flow }), /whole number/);
});

test("the backend and the flow are required", () => {
  assert.match(bad({ paperboat: 0, flow }), /which backend/);
  assert.match(bad({ paperboat: 0, backend: "b" }), /flow must be an object/);
  assert.match(bad({ paperboat: 0, backend: "b", flow: { nodes: [] } }), /flow\.edges/);
});

test("damage is named by where it is", () => {
  const base = { paperboat: 0, backend: "b" };
  assert.match(bad({ ...base, flow: { nodes: [{ id: "a" }], edges: [] } }), /flow\.nodes\[0\] needs an id and a type/);
  assert.match(bad({ ...base, flow: { nodes: [], edges: [{ from: ["a"], to: ["b", "c"] }] } }), /flow\.edges\[0\]/);
  assert.match(bad({ ...base, flow: { nodes: [{ id: "a", type: "t", pos: [1] }], edges: [] } }), /pos must be two numbers/);
  assert.match(bad({ ...base, flow: { nodes: [], edges: [], groups: [{ id: "g", kind: "weird", nodes: [] }] } }), /flow\.groups\[0\]/);
  assert.match(bad({ ...base, flow, station: { objects: [{ id: "x" }] } }), /station\.objects\[0\]/);
  assert.match(bad({ ...base, flow, variables: [{ name: "x", type: "color" }] }), /variables\[0\]/);
  assert.match(bad({ ...base, flow, dashboard: {} }), /dashboard\.instruments/);
  assert.match(bad({ ...base, flow, run: { duration: "long" } }), /run\.duration/);
  assert.match(bad({ ...base, flow, ui: "not-an-obj" }), /ui must be an object/);
  assert.match(bad({ ...base, flow, ui: { lock: "not-an-obj" } }), /ui\.lock must be an object/);
  assert.match(bad({ ...base, flow, ui: { lock: { arrange: "yes" } } }), /ui\.lock\.arrange must be a boolean/);
  assert.match(bad({ ...base, flow, ui: { lock: { pages: [] } } }), /ui\.lock\.pages must be a non-empty list/);
  assert.match(bad({ ...base, flow, ui: { lock: { pages: [123] } } }), /ui\.lock\.pages must be a non-empty list/);
  assert.match(bad({ ...base, flow, ui: { lock: { modes: [] } } }), /ui\.lock\.modes must be a non-empty list/);
  assert.match(bad({ ...base, flow, ui: { lock: { passcode: { algo: "sha256" } } } }), /ui\.lock\.passcode needs algo, salt, and hash/);
  assert.match(bad({ ...base, flow, run: { duration: "long" } }), /^The project is damaged: /);
});

test("ui.lock is preserved in buildProject and validated by parseProject", () => {
  const lock = {
    arrange: true,
    pages: ["station", "dashboard"],
    modes: ["navigator"],
    passcode: { algo: "sha256", salt: "1234567890abcdef", hash: "abcdef1234567890" },
  };
  const p = buildProject({ backend: "b", flow, ui: { lock } });
  assert.deepEqual(p.ui?.lock, lock);
  const parsed = ok(stringify(p));
  assert.deepEqual(parsed.ui?.lock, lock);
});

test("a file name is made safe and ends in the project extension", () => {
  assert.equal(projectFileName("balance"), `balance${PROJECT_EXT}`);
  assert.equal(projectFileName("a/b:c*d"), `a-b-c-d${PROJECT_EXT}`);
  assert.equal(projectFileName("  "), `project${PROJECT_EXT}`);
  assert.equal(projectFileName("..hidden"), `hidden${PROJECT_EXT}`);
});

test("a project's name is its file's without the extension", () => {
  assert.equal(projectName("balance.ppj"), "balance");
  assert.equal(projectName("Balance.PPJ"), "Balance");
  assert.equal(projectName("balance.paperboat.json"), "balance");
  assert.equal(projectName("Balance.PAPERBOAT.JSON"), "Balance");
  assert.equal(projectName("notes.json"), "notes");
  assert.equal(projectName("plain"), "plain");
  assert.equal(projectName(".json"), "Untitled");
});

test("time since is said in the largest sensible unit", () => {
  const now = 10_000_000_000;
  assert.equal(ago(now - 20_000, now), "just now");
  assert.equal(ago(now - 5 * 60_000, now), "5 min ago");
  assert.equal(ago(now - 2 * 3_600_000, now), "2 h ago");
  assert.equal(ago(now - 3 * 86_400_000, now), "3 d ago");
  assert.equal(ago(now + 5_000, now), "just now", "a clock that is slightly off");
});

test("recent projects: newest first, one per name, at most five", () => {
  const entry = (name: string, at: number): Stored => ({ name, savedAt: at, project: buildProject({ backend: "b", flow }) });
  let list: Stored[] = [];
  for (let i = 0; i < 7; i++) list = addRecent(list, entry(`p${i}`, i));
  assert.equal(list.length, RECENT_MAX);
  assert.deepEqual(list.map((s) => s.name), ["p6", "p5", "p4", "p3", "p2"]);
  const again = addRecent(list, entry("p4", 99));
  assert.deepEqual(again.map((s) => s.name), ["p4", "p6", "p5", "p3", "p2"]);
  assert.equal(again[0].savedAt, 99, "the newer copy replaces the older");
});

test("recent projects: a huge project doesn't push out the one just saved", () => {
  const big = (name: string): Stored => ({
    name,
    savedAt: 1,
    project: buildProject({ backend: "b", flow: { nodes: [{ id: "a", type: "t", params: { text: "x".repeat(1_400_000) } }], edges: [] } }),
  });
  const list = addRecent(addRecent([], big("one")), big("two"));
  assert.deepEqual(list.map((s) => s.name), ["two"], "the older one makes way: the list stays under the cap");
});
