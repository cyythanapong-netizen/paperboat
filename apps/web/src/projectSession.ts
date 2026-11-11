// Projects (#82): the whole session as one file. Its title and file, whether it is saved, opening
// (with one Undo), saving, recent projects, a project dropped on the window or sent by the backend,
// and the autosave offered back after a reload. What a project holds is put in place by `load`.

import { useEffect, useRef, useState } from "react";
import type { EngineState } from "./engine";
import type { DashboardLayout, FlowType, Project, RunSettings, ServerMsg, Station, Variables } from "./protocol";
import { addRecent, ago, buildProject, clearAutosave, loadAutosave, loadRecent, parseProject, projectFileName, projectName, saveAutosave, saveRecent, stringify, type Stored } from "./project";
import { draggingFiles, droppedText, pickText, saveText, type FileHandle } from "./projectFiles";
import type { NoticeSpec } from "./ProjectUI";
import type { LockConfig } from "./lock";
import { toFlow } from "./flowGraph";
import type { FlowNode, Wire } from "./flowModel";

/** What a project is made of, as the app holds it now. */
export type ProjectParts = {
  nodes: FlowNode[];
  edges: Wire[];
  station: Station | null;
  variables: Variables;
  dashSaved: DashboardLayout | undefined;
  runSettings: RunSettings;
  projectLock: LockConfig | null;
  /** The Flow sheet's flow type, if one was chosen (#9). */
  flowChoice: FlowType | null;
};

export function useProjectSession(
  state: EngineState,
  subscribe: (f: (m: ServerMsg) => void) => () => void,
  log: (level: "info" | "warn" | "error", text: string) => void,
  { nodes, edges, station, variables, dashSaved, runSettings, projectLock, flowChoice }: ProjectParts,
  load: (p: Project) => void,
) {
  // A project is the Flow, the Station, the variables, the Dashboard layout and the run settings. `projectKey` is
  // that, as text, a moment after the last change; it is "unsaved" while it differs from `baseline`, which is
  // what the backend started with, or what was last saved or opened.
  const [projectTitle, setProjectTitle] = useState("Untitled");
  /** The name last loaded or saved; a rename is unsaved until the next save. */
  const [savedTitle, setSavedTitle] = useState("Untitled");
  const fileHandle = useRef<FileHandle | null>(null); // where Save writes, once the user chose a file (Chromium)
  const [notice, setNotice] = useState<NoticeSpec | null>(null);
  const [undoOpen, setUndoOpen] = useState<(() => void) | null>(null); // Edit ▸ Undo open project
  const [recent, setRecent] = useState<Stored[]>(loadRecent);
  const [lastSession, setLastSession] = useState<Stored | undefined>(undefined); // unsaved work found at load
  const [restoreChecked, setRestoreChecked] = useState(false);
  const [baseline, setBaseline] = useState<string | null>(null);
  const [projectKey, setProjectKey] = useState("");
  const snapshot = (): Project =>
    buildProject({
      backend: state.engine,
      flow: toFlow(nodes, edges, flowChoice),
      station,
      variables,
      dashboard: dashSaved,
      run: runSettings,
      ui: projectLock ? { lock: projectLock } : undefined,
    });
  const latestProject = useRef<Project | null>(null);
  const dirty = baseline !== null && projectKey !== "" && (projectKey !== baseline || projectTitle !== savedTitle);
  const live = useRef({ snapshot, projectTitle, baseline, dirty, state });
  live.current = { snapshot, projectTitle, baseline, dirty, state };

  useEffect(() => {
    if (!state.engine) return;
    const t = window.setTimeout(() => {
      const p = snapshot();
      latestProject.current = p;
      setProjectKey(JSON.stringify(p));
    }, 250);
    return () => window.clearTimeout(t);
  }, [nodes, edges, station, variables, dashSaved, runSettings, projectLock, flowChoice, state.engine]); // eslint-disable-line react-hooks/exhaustive-deps -- `snapshot` reads exactly these
  // Another backend: a new, unnamed project.
  useEffect(() => {
    setProjectTitle("Untitled");
    setSavedTitle("Untitled");
    fileHandle.current = null;
    setBaseline(null);
    setRestoreChecked(false);
    setLastSession(undefined);
  }, [state.engine]);
  // The backend loaded its starter again: with nothing changed, clean is whatever that settles to.
  useEffect(() => {
    if (!live.current.dirty) setBaseline(null);
  }, [state.loaded]);
  useEffect(() => {
    if (baseline !== null || projectKey === "") return;
    const t = window.setTimeout(() => setBaseline(projectKey), 500);
    return () => window.clearTimeout(t);
  }, [projectKey, baseline]);
  useEffect(() => {
    document.title = `${dirty ? "● " : ""}${projectTitle} · Paperboat`;
  }, [projectTitle, dirty]);

  /** Put a project in place of what is open. `rebase`: what counts as unsaved against (null: what is loaded). */
  const applyProject = (p: Project, name: string, rebase: string | null = null) => {
    load(p);
    setProjectTitle(name);
    setSavedTitle(name);
    setBaseline(rebase);
  };
  // The backend can send an initial project on connect (#126).
  useEffect(
    () =>
      subscribe((m) => {
        if (m.type === "project.load") {
          const name = m.name || projectName(m.project.backend || "project");
          applyProject(m.project, name);
        }
      }),
    [subscribe, state.blocks, state.station],
  );
  const remember = (p: Project, name: string) =>
    setRecent((prev) => {
      const next = addRecent(prev, { name, savedAt: Date.now(), project: p });
      saveRecent(next);
      return next;
    });
  const fail = (title: string, e: unknown) => {
    const msg = e instanceof Error ? e.message : String(e);
    log("error", `${title}: ${msg}`);
  };

  /** Open a project, after a word if it comes from another backend or replaces unsaved changes; one Undo brings back what was open. */
  const openProject = (p: Project, name: string) => {
    const warnings: string[] = [];
    if (p.backend !== state.engine) warnings.push(`This project was made on “${p.backend}” and you are connected to “${state.engine}”. Blocks this backend lacks stay as lost blocks, with their params and wires.`);
    if (live.current.dirty) warnings.push("The open project has unsaved changes. Opening replaces them; Undo brings them back.");
    const go = () => {
      const { snapshot: before, projectTitle: beforeTitle, baseline: beforeBase } = live.current;
      const was = before();
      const handle = fileHandle.current;
      applyProject(p, name);
      fileHandle.current = null;
      remember(p, name);
      log("info", `Opened “${name}” (Edit ▸ Undo open project brings back what was open)`);
      setUndoOpen(() => () => {
        applyProject(was, beforeTitle, beforeBase);
        fileHandle.current = handle;
        setUndoOpen(null);
      });
    };
    if (warnings.length === 0) go();
    else setNotice({ title: "Open this project?", text: warnings, actions: [{ label: "Open", primary: true, run: go }, { label: "Cancel" }] });
  };
  const openText = (text: string, file: string) => {
    const r = parseProject(text);
    if ("error" in r) {
      log("error", `Can't open ${file}: ${r.error}. Nothing was changed.`);
    } else openProject(r.project, projectName(file));
  };
  const openFile = async () => {
    try {
      const f = await pickText();
      if (f) openText(f.text, f.name);
    } catch (e) {
      fail("Can't open the file", e);
    }
  };
  const saveProject = async (as: boolean) => {
    const { snapshot: take, projectTitle: title } = live.current;
    const p = take();
    try {
      const r = await saveText(stringify(p), projectFileName(title === "Untitled" ? state.engine || "project" : title), as ? null : fileHandle.current);
      if (!r) return; // cancelled
      fileHandle.current = r.handle ?? null;
      const name = projectName(r.name);
      setProjectTitle(name);
      setSavedTitle(name);
      setBaseline(JSON.stringify(p));
      remember(p, name);
      clearAutosave(state.engine);
      setLastSession(undefined);
    } catch (e) {
      fail("Can't save", e);
    }
  };
  // A rename is a project change: next Save writes the new .ppj name (and asks for it, since
  // an existing file handle still points at the old file).
  const renameProject = (name: string) => {
    const next = name.trim();
    if (!next || next === projectTitle) return;
    setProjectTitle(next);
    fileHandle.current = null;
  };
  // Dropping a project file on the window opens it (the app's own cards are not files).
  const openTextNow = useRef(openText);
  openTextNow.current = openText;
  useEffect(() => {
    const over = (e: DragEvent) => draggingFiles(e) && e.preventDefault();
    const drop = (e: DragEvent) => {
      if (!draggingFiles(e)) return;
      e.preventDefault();
      droppedText(e).then((f) => f && openTextNow.current(f.text, f.name), (err) => fail("Can't open the file", err));
    };
    window.addEventListener("dragover", over);
    window.addEventListener("drop", drop);
    return () => {
      window.removeEventListener("dragover", over);
      window.removeEventListener("drop", drop);
    };
  }, []); // eslint-disable-line react-hooks/exhaustive-deps -- `fail` only sets state
  // Unsaved work is kept as it happens, and offered back after a reload.
  const restore = (s: Stored) => {
    applyProject(s.project, s.name, live.current.baseline);
    fileHandle.current = null;
    setLastSession(undefined);
  };
  useEffect(() => {
    if (baseline === null || restoreChecked || !state.engine) return;
    setRestoreChecked(true);
    const s = loadAutosave(state.engine);
    if (!s || JSON.stringify(s.project) === baseline) return clearAutosave(state.engine);
    setLastSession(s);
    setNotice({
      title: "Restore your last session?",
      text: [`“${s.name}”, ${ago(s.savedAt, Date.now())}, had changes that weren't saved.`],
      actions: [
        { label: "Restore", primary: true, run: () => restore(s) },
        { label: "Discard", run: () => (clearAutosave(s.project.backend), setLastSession(undefined)) },
        { label: "Not now" },
      ],
    });
  }, [baseline, restoreChecked, state.engine]); // eslint-disable-line react-hooks/exhaustive-deps -- once per backend
  useEffect(() => {
    if (!restoreChecked || !state.engine || baseline === null || !latestProject.current) return;
    if (dirty) saveAutosave(state.engine, { name: projectTitle, savedAt: Date.now(), project: latestProject.current });
    else if (!lastSession) clearAutosave(state.engine);
  }, [projectKey, dirty, restoreChecked]); // eslint-disable-line react-hooks/exhaustive-deps -- when the project changes

  return { projectTitle, dirty, notice, setNotice, undoOpen, recent, lastSession, restore, openProject, openFile, saveProject, renameProject };
}
