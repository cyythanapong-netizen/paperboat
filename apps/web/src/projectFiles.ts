// Reading and writing a project file in the browser (#82). Where the File System Access API
// exists (Chromium) Save writes back to the same file; elsewhere Save downloads a copy and Open
// uses a file input. Both paths give the same results to the app.

/** The part of a file handle we use. */
export type FileHandle = {
  name: string;
  createWritable: () => Promise<{ write: (data: string) => Promise<void>; close: () => Promise<void> }>;
  getFile: () => Promise<File>;
};

type Types = { description: string; accept: Record<string, string[]> }[];
declare global {
  interface Window {
    showSaveFilePicker?: (o: { suggestedName?: string; types?: Types }) => Promise<FileHandle>;
    showOpenFilePicker?: (o: { multiple?: boolean; types?: Types }) => Promise<FileHandle[]>;
  }
}

const TYPES: Types = [{ description: "Paperboat project", accept: { "application/json": [".json"] } }];
const cancelled = (e: unknown) => e instanceof DOMException && e.name === "AbortError";

/**
 * Save `text`. With a `handle` (from an earlier save) it writes to that file; else asks where, or
 * downloads. Returns the file's name and its handle, or null when the user cancelled.
 */
export async function saveText(text: string, suggestedName: string, handle: FileHandle | null): Promise<{ name: string; handle?: FileHandle } | null> {
  if (window.showSaveFilePicker) {
    try {
      const h = handle ?? (await window.showSaveFilePicker({ suggestedName, types: TYPES }));
      const w = await h.createWritable();
      await w.write(text);
      await w.close();
      return { name: h.name, handle: h };
    } catch (e) {
      if (cancelled(e)) return null;
      throw e;
    }
  }
  const url = URL.createObjectURL(new Blob([text], { type: "application/json" }));
  const a = document.createElement("a");
  a.href = url;
  a.download = suggestedName;
  document.body.append(a);
  a.click();
  a.remove();
  window.setTimeout(() => URL.revokeObjectURL(url), 10_000);
  return { name: suggestedName };
}

/** Ask for a file and read it as text; null when the user cancelled. */
export async function pickText(): Promise<{ name: string; text: string } | null> {
  if (window.showOpenFilePicker) {
    try {
      const [h] = await window.showOpenFilePicker({ multiple: false, types: TYPES });
      const file = await h.getFile();
      return { name: file.name, text: await file.text() };
    } catch (e) {
      if (cancelled(e)) return null;
      throw e;
    }
  }
  return new Promise((resolve, reject) => {
    const input = document.createElement("input");
    input.type = "file";
    input.accept = ".json,application/json";
    input.hidden = true;
    const done = (v: { name: string; text: string } | null) => {
      input.remove();
      resolve(v);
    };
    input.addEventListener("change", () => {
      const file = input.files?.[0];
      if (!file) return done(null);
      file.text().then((text) => done({ name: file.name, text }), (e) => (input.remove(), reject(e)));
    });
    input.addEventListener("cancel", () => done(null));
    document.body.append(input);
    input.click();
  });
}

/** The first file of a drop, read as text; null when the drop holds no file. */
export async function droppedText(e: DragEvent): Promise<{ name: string; text: string } | null> {
  const file = e.dataTransfer?.files?.[0];
  return file ? { name: file.name, text: await file.text() } : null;
}

/** True while a drag carries files (and not one of the app's own cards). */
export const draggingFiles = (e: DragEvent) => !!e.dataTransfer?.types.includes("Files");
