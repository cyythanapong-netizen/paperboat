import { useEffect, type RefObject } from "react";
import { SEP } from "./groups";
import { directionName, directionOf, directionTitle, keepAttr as keep, writeAttr as write, typeFromClasses } from "./portDirection";

const HANDLE = ".react-flow__handle";

/**
 * Says which way each port on the Flow canvas goes (#204): every dot's hover title and accessible
 * name lead with "Input" or "Output" (and its type), and so does the hover title of a block's port
 * row. The dots are drawn by `PortHandle` in @paperboat/shared, so this adds the words to the DOM it
 * renders: what React wrote (a unit hint, why a wire won't fit, a compact plate's port label) is
 * kept after the direction, and is picked up again whenever React changes it. Connector ports
 * (`.ip`, acausal: both ways at once) are left as they are.
 */
export function usePortDirections(root: RefObject<HTMLElement | null>) {
  useEffect(() => {
    const el = root.current;
    if (!el) return;
    const observer = new MutationObserver((records) => {
      const changed = new Set<Element>();
      const added = new Set<Element>();
      for (const r of records) {
        if (r.type === "attributes") changed.add(r.target as Element);
        else r.addedNodes.forEach((n) => n instanceof Element && added.add(n));
      }
      // A changed dot or row is redone (with a row, its dot: the type is the row's); new nodes, all through.
      changed.forEach((e) => (e.matches(".port") ? annotateWithin(e) : e.matches(HANDLE) && annotateHandle(e as HTMLElement)));
      added.forEach(annotateWithin);
    });
    annotateWithin(el);
    observer.observe(el, { subtree: true, childList: true, attributes: true, attributeFilter: ["class", "title", "aria-label"] });
    return () => observer.disconnect();
  }, [root]);
}

/** `el` and everything in it that is a port dot or a block's port row. */
function annotateWithin(el: Element) {
  if (el.matches(HANDLE)) annotateHandle(el as HTMLElement);
  else if (el.matches(".port")) annotateRow(el as HTMLElement);
  el.querySelectorAll<HTMLElement>(`${HANDLE}, .port`).forEach((e) => (e.matches(HANDLE) ? annotateHandle(e) : annotateRow(e)));
}

const dirOfHandle = (h: HTMLElement) => (h.classList.contains("target") ? directionOf("target") : h.classList.contains("source") ? directionOf("source") : null);
const typeOf = (h: HTMLElement) => typeFromClasses(h.classList) ?? typeFromClasses(h.closest(".port, .boundary")?.classList ?? []);

function annotateHandle(h: HTMLElement) {
  const dir = dirOfHandle(h);
  if (!dir || h.classList.contains("ip")) return;
  const type = typeOf(h);
  const label = keep(h, "aria-label") || (h.dataset.handleid ?? "").replace(SEP, ".");
  write(h, "aria-label", directionName(dir, label, type));
  write(h, "title", directionTitle(dir, type, keep(h, "title")));
}

function annotateRow(row: HTMLElement) {
  const dir = row.classList.contains("in") ? "in" : row.classList.contains("out") ? "out" : null;
  if (!dir) return;
  write(row, "title", directionTitle(dir, typeFromClasses(row.classList), keep(row, "title")));
}
