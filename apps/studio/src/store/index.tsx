// The StudioApi (contracts §8.2): every edit is an omc mutation followed by a re-read, so the
// model on screen is what omc says and the .mo stays OMEdit-compatible. Undo and redo are the
// shared undo.ts over the model's parts and wires; going back replays the difference through omc.

import { useUndo, type IconDef } from "@paperboat/shared";
import type { Edge, Node } from "@xyflow/react";
import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import type { Connection, ExportProgress, ExportResult, Placement, StudioApi, StudioModel } from "../model/types.ts";
import type { OmcClient, OmcStatus } from "../omc/omc.ts";
import { parseValue } from "../omc/parse.ts";
import { restoreExprs, type Graph, type PartSnap } from "./diff.ts";
import * as x from "./exprs.ts";
import { classLook, type IconCache } from "./looks.ts";
import { readModel } from "./read.ts";
import { freshName, splicePlan, throughPorts } from "./splice.ts";
import { entryExprs, connectExprs, iconPortExprs, removeEntryExprs, removeExprs, setParamExprs } from "./plans.ts";
import { endOf, split } from "./wires.ts";

type PartNode = Node<{ part: PartSnap }>;
type WireEdge = Edge<{ a: string; b: string; points?: [number, number][] }>;

/** What the canvas, library and quick-add do beyond the StudioApi Lane B sees. */
export type Actions = {
  notice: string;
  say(words: string): void;
  move(name: string, to: Placement): Promise<void>;
  remove(names: string[], wires: Connection[]): Promise<void>;
  /** Places `cls` at `at` with a fresh name; returns the name. */
  place(cls: string, at: Placement): Promise<string>;
  /** The splice plan for `cls` into `wire`, or the words why not. */
  planSplice(wire: Connection, cls: string): Promise<{ line: string; apply(): Promise<void> } | { why: string }>;
  undo(): void;
  redo(): void;
  canUndo: boolean;
  canRedo: boolean;
  /** .mo text of the model, for the split view. */
  source(): Promise<string>;
  /** The generated backend without writing it: path and reason, and the files for the ODE path. */
  preview(): Promise<{ path: "ode" | "fmu"; reason: string; files: Record<string, string>; notices: string[] }>;
  /** A local check run: every variable over [0, stop]. */
  simulate(stop: number): Promise<{ t: number[]; series: Record<string, (number | null)[]>; names: string[] }>;
};

const asGraph = (m: StudioModel): Graph => ({
  parts: Object.fromEntries(m.components.map((c) => [c.name, { class: c.class, params: c.params, placement: c.placement }])),
  wires: m.connections.map((c) => ({ a: c.a, b: c.b, ...(c.points ? { points: c.points } : {}) })),
});

export function useStudioStore(omc: OmcClient, status: OmcStatus): { api: StudioApi | null; actions: Actions; error: string } {
  const [model, setModel] = useState<StudioModel | null>(null);
  const [selection, setSelection] = useState<string[]>([]);
  const [notice, setNotice] = useState("");
  const [error, setError] = useState("");
  const looks = useRef<IconCache>(new Map());
  const current = useRef<StudioModel | null>(null);
  current.current = model;
  const queue = useRef<Promise<unknown>>(Promise.resolve());

  const say = useCallback((words: string) => setNotice(words), []);

  // One edit at a time: the queue keeps an undo replay and a drag from interleaving.
  const enqueue = useCallback(<T,>(job: () => Promise<T>): Promise<T> => {
    const next = queue.current.then(job, job);
    queue.current = next.catch(() => undefined);
    return next;
  }, []);

  const refresh = useCallback(async () => {
    const m = current.current;
    if (!m) return;
    setModel(await readModel(omc, m.name, m.file, looks.current));
  }, [omc]);

  /** omc's words for a failed call: what it said since the last one. */
  const run = useCallback(
    async (exprs: string[]) => {
      for (const expr of exprs) {
        const answer = await omc.call(expr);
        if (answer === "false" || answer === "") {
          const why = (await omc.call("getErrorString()").catch(() => "")).replace(/^"|"$/g, "").replace(/\\n/g, "\n").trim();
          throw new Error(why || `omc refused ${expr.slice(0, expr.indexOf("("))}`);
        }
      }
      const m = current.current;
      if (m) await omc.op("save", { model: m.name }).catch(() => undefined);
      await refresh();
    },
    [omc, refresh],
  );

  const edit = useCallback(
    (exprs: string[]) =>
      enqueue(() => run(exprs)).then(
        () => setNotice(""),
        (e: Error) => {
          setNotice(e.message);
          void enqueue(refresh); // omc may have applied some of it; show what it has
          throw e;
        },
      ),
    [enqueue, run, refresh],
  );

  // Open the server's model once the connection is up.
  useEffect(() => {
    let live = true;
    omc.op<{ file: string; model: string }>("open").then(
      async (opened) => {
        const m = await readModel(omc, opened.model, opened.file, looks.current);
        if (live) setModel(m);
      },
      (e: Error) => live && setError(e.message),
    );
    return () => {
      live = false;
    };
  }, [omc]);

  // --- undo over the model's parts and wires ---
  const nodes = useMemo<PartNode[]>(
    () => (model?.components ?? []).map((c) => ({ id: c.name, position: { x: c.placement.x, y: c.placement.y }, data: { part: { class: c.class, params: c.params, placement: c.placement } } })),
    [model],
  );
  const edges = useMemo<WireEdge[]>(
    () => (model?.connections ?? []).map((c) => ({ id: `${c.a}|${c.b}`, source: split(c.a).part, target: split(c.b).part, data: { a: c.a, b: c.b, ...(c.points ? { points: c.points } : {}) } })),
    [model],
  );
  const wanted = useRef<{ nodes?: PartNode[]; edges?: WireEdge[] }>({});
  const replay = useCallback(() => {
    queueMicrotask(() => {
      const { nodes: n, edges: e } = wanted.current;
      const m = current.current;
      if (!n || !e || !m) return;
      wanted.current = {};
      const then: Graph = { parts: Object.fromEntries(n.map((p) => [p.id, p.data.part])), wires: e.map((w) => ({ a: w.data!.a, b: w.data!.b, ...(w.data!.points ? { points: w.data!.points } : {}) })) };
      void edit(restoreExprs(m.name, asGraph(m), then)).catch(() => undefined);
    });
  }, [edit]);
  const history = useUndo<PartNode, WireEdge>(
    nodes,
    edges,
    (v) => {
      wanted.current.nodes = v as PartNode[];
      replay();
    },
    (v) => {
      wanted.current.edges = v as WireEdge[];
      replay();
    },
    model?.name,
  );
  // useUndo hands out new functions every render; the actions keep one pair
  const steps = useRef(history);
  steps.current = history;
  const undo = useCallback(() => steps.current.undo(), []);
  const redo = useCallback(() => steps.current.redo(), []);
  const { canUndo, canRedo } = history;

  // --- the StudioApi ---
  const api = useMemo<StudioApi | null>(() => {
    if (!model) return null;
    const name = model.name;
    return {
      model,
      selection,
      status,
      omc,
      select: (names) => setSelection((prev) => (prev.join() === names.join() ? prev : names)),
      async setParam(path, value) {
        return edit(setParamExprs(model, path, value));
      },
      async addComponent(cls, partName, at) {
        await edit([x.addComponent(name, cls, partName, at)]);
      },
      async connect(a, b) {
        const plan = connectExprs(model, a, b);
        if ("error" in plan) return plan.error;
        try {
          await edit(plan.exprs);
          return null;
        } catch (e) {
          return (e as Error).message;
        }
      },
      async addEntry(kind, target) {
        await edit(entryExprs(model, kind, target));
      },
      async removeEntry(entry) {
        await edit(removeEntryExprs(model, entry));
      },
      async setIcon(icon: IconDef) {
        await enqueue(async () => {
          await omc.op("set_icon", { model: name, icon });
          for (const expr of iconPortExprs(model, icon)) await omc.call(expr);
          await omc.op("save", { model: name });
          await refresh();
        });
      },
      async exportBackend(out: string, onProgress: (p: ExportProgress) => void): Promise<ExportResult> {
        return enqueue(async () => {
          if (!model.file) throw new Error("save the model to a file first");
          const done = await omc.op<{ out: string; path: "ode" | "fmu"; reason: string }>("generate", { source: model.file, model: name, out }, (stage, fraction) =>
            onProgress({ stage: stage as ExportProgress["stage"], fraction }),
          );
          return { out: done.out, path: done.path, reason: done.reason };
        });
      },
    };
  }, [model, selection, status, omc, edit, enqueue, refresh]);

  const actions = useMemo<Actions>(
    () => ({
      notice,
      say,
      async move(partName, to) {
        const m = current.current;
        const part = m?.components.find((c) => c.name === partName);
        if (!m || !part || (part.placement.x === to.x && part.placement.y === to.y)) return;
        await edit([x.updateComponent(m.name, part.class, partName, to)]).catch(() => undefined);
      },
      async remove(names, wires) {
        const m = current.current;
        if (!m) return;
        await edit(removeExprs(m, names, wires)).catch(() => undefined);
        setSelection([]);
      },
      async place(cls, at) {
        const m = current.current!;
        const partName = freshName(cls, m.components.map((c) => c.name));
        await edit([x.addComponent(m.name, cls, partName, at)]).catch(() => undefined);
        return partName;
      },
      async planSplice(wire, cls) {
        const m = current.current!;
        const look = await classLook(omc, looks.current, cls, "");
        const ports = look.icon?.ports ?? [];
        const wireEnd = endOf(m, wire.a);
        if (!wireEnd) return { why: "that wire has an end that is gone" };
        const through = throughPorts(ports, { ...m.connectors, ...look.connectors }, wireEnd.cls);
        if ("why" in through) return { why: `${cls.slice(cls.lastIndexOf(".") + 1)} can't go in this wire: ${through.why}` };
        const pa = m.components.find((c) => c.name === split(wire.a).part);
        const pb = m.components.find((c) => c.name === split(wire.b).part);
        // causal wires run output -> input, physical ones left to right
        const reversed = wireEnd.kind === "acausal" ? (pa?.placement.x ?? 0) > (pb?.placement.x ?? 0) : wireEnd.kind === "input";
        const mid: [number, number] = pa && pb ? [(pa.placement.x + pb.placement.x) / 2, (pa.placement.y + pb.placement.y) / 2] : [0, 0];
        const partName = freshName(cls, m.components.map((c) => c.name));
        const plan = splicePlan(m.name, wire, cls, partName, through, mid, reversed);
        return { line: plan.line, apply: () => edit(plan.exprs).catch(() => undefined) };
      },
      undo,
      redo,
      canUndo,
      canRedo,
      preview() {
        const m = current.current;
        if (!m?.file) return Promise.reject(new Error("the model has no file yet"));
        // queued behind edits: generating reloads the file, so a half-saved edit must not be in flight
        return enqueue(() => omc.op<{ path: "ode" | "fmu"; reason: string; files?: Record<string, string>; notices?: string[] }>("generate", { source: m.file, model: m.name, dry: true })).then((r) => ({ path: r.path, reason: r.reason, files: r.files ?? {}, notices: r.notices ?? [] }));
      },
      simulate(stop) {
        const m = current.current;
        if (!m) return Promise.reject(new Error("no model"));
        return omc.op("simulate", { model: m.name, stop });
      },
      async source() {
        const m = current.current;
        if (!m) return "";
        return String(parseValue(await omc.call(`list(${m.name})`)));
      },
    }),
    [notice, say, edit, omc, undo, redo, canUndo, canRedo],
  );

  return { api, actions, error };
}

