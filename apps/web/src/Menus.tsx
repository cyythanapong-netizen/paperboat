import { useEffect, useMemo, useRef, useState, type KeyboardEvent } from "react";
import { byMenu, describe, GROUP_TITLES, isEnabled, MENUS, rank, SEARCH_CHORD, type Command, type MenuId } from "./commands";

/**
 * The menu bar (#78): one menu per `MenuId` that has commands. An ARIA menubar: Tab enters it once,
 * ←/→ move between menus, ↓ or Enter opens one, ↑/↓ move in it, Esc closes. Disabled items stay and say
 * why in their tooltip. Keys here are the menu's own, never forwarded to the backend (`data-own-keys`).
 */
export function MenuBar({ commands }: { commands: Command[] }) {
  const menus = byMenu(commands);
  const [open, setOpen] = useState<MenuId | null>(null);
  const [rover, setRover] = useState(0); // the one title Tab lands on
  const root = useRef<HTMLDivElement>(null);
  const focusFirst = useRef(false); // opened with the keyboard: move into the menu

  const titles = () => [...(root.current?.querySelectorAll<HTMLButtonElement>(".menu-title") ?? [])];
  const items = () => [...(root.current?.querySelectorAll<HTMLElement>('.menu-item:not([aria-disabled="true"])') ?? [])];

  useEffect(() => {
    if (open && focusFirst.current) {
      focusFirst.current = false;
      items()[0]?.focus();
    }
  }, [open]);

  // A press outside closes.
  useEffect(() => {
    if (!open) return;
    const down = (e: PointerEvent) => !root.current?.contains(e.target as Node) && setOpen(null);
    window.addEventListener("pointerdown", down);
    return () => window.removeEventListener("pointerdown", down);
  }, [open]);

  const openMenu = (id: MenuId, byKey: boolean) => {
    focusFirst.current = byKey;
    setOpen(id);
  };

  const onKey = (e: KeyboardEvent<HTMLDivElement>) => {
    const ids = menus.map((m) => m.id);
    const target = e.target as HTMLElement;
    const onTitle = target.classList.contains("menu-title");
    const here = onTitle ? (target.dataset.menu as MenuId) : open;
    if (!here) return;
    const step = (d: number) => {
      const list = items();
      const i = list.indexOf(document.activeElement as HTMLElement);
      list[(i + d + list.length) % list.length]?.focus();
    };
    switch (e.key) {
      case "ArrowRight":
      case "ArrowLeft": {
        e.preventDefault();
        const next = ids[(ids.indexOf(here) + (e.key === "ArrowRight" ? 1 : -1) + ids.length) % ids.length];
        if (open) openMenu(next, true);
        else titles()[ids.indexOf(next)]?.focus();
        break;
      }
      case "ArrowDown":
        e.preventDefault();
        if (onTitle) openMenu(here, true);
        else step(1);
        break;
      case "ArrowUp":
        e.preventDefault();
        if (onTitle) openMenu(here, true);
        else step(-1);
        break;
      case "Home":
      case "End":
        if (open && !onTitle) {
          e.preventDefault();
          const list = items();
          list[e.key === "Home" ? 0 : list.length - 1]?.focus();
        }
        break;
      case "Escape":
        if (open) {
          e.preventDefault();
          e.stopPropagation(); // closing a menu isn't also Escape for the canvas
          setOpen(null);
          titles()[ids.indexOf(here)]?.focus();
        }
        break;
      case "Tab":
        setOpen(null);
        break;
    }
  };

  const activate = (c: Command) => {
    if (!isEnabled(c)) return;
    setOpen(null);
    (document.activeElement as HTMLElement | null)?.blur(); // the keys go back to the page
    c.run();
  };

  return (
    <div className="menubar" role="menubar" aria-label="Menus" data-own-keys ref={root} onKeyDown={onKey}>
      {menus.map((m, i) => (
        <div className="menu-wrap" key={m.id}>
          <button
            className="menu-title"
            role="menuitem"
            aria-haspopup="menu"
            aria-expanded={open === m.id}
            data-menu={m.id}
            tabIndex={i === rover ? 0 : -1}
            onFocus={() => setRover(i)}
            onClick={(e) => (open === m.id ? setOpen(null) : openMenu(m.id, e.detail === 0))}
            onPointerEnter={() => open && open !== m.id && setOpen(m.id)}
          >
            {m.label}
          </button>
          {open === m.id && (
            <div className="menu" role="menu" aria-label={m.label}>
              {m.items.map((c, j) => {
                const first = j === 0 || m.items[j - 1].group !== c.group;
                const on = isEnabled(c);
                const radio = c.group in GROUP_TITLES;
                return (
                  <div key={c.id} role="none">
                    {first && j > 0 && <div className="menu-sep" role="separator" />}
                    {first && GROUP_TITLES[c.group] && <div className="menu-group" role="presentation">{GROUP_TITLES[c.group]}</div>}
                    <button
                      className={`menu-item${c.checked ? " on" : ""}`}
                      role={c.checked === undefined ? "menuitem" : radio ? "menuitemradio" : "menuitemcheckbox"}
                      aria-checked={c.checked}
                      aria-disabled={on ? undefined : true}
                      tabIndex={-1}
                      title={on ? undefined : c.why}
                      onClick={() => activate(c)}
                    >
                      <span className="menu-label">{c.label}</span>
                      <span className="menu-side">
                        {c.meta && <span className="menu-meta">{c.meta}</span>}
                        {c.shortcut && <kbd>{c.shortcut}</kbd>}
                      </span>
                    </button>
                  </div>
                );
              })}
            </div>
          )}
        </div>
      ))}
    </div>
  );
}

/**
 * The command search (Ctrl+K): a field floating at the top centre, over everything, listing every
 * command; type to narrow, ↑/↓ and Enter to run, Esc to close. It opens only from its shortcut (and the
 * View menu), so the bar has no search well. Focus goes back to where it was.
 */
export function CommandSearch({ commands, onClose }: { commands: Command[]; onClose: () => void }) {
  const [query, setQuery] = useState("");
  const [index, setIndex] = useState(0);
  const root = useRef<HTMLDivElement>(null);
  const list = useRef<HTMLUListElement>(null);
  const results = useMemo(() => rank(query, commands.filter((c) => !c.quiet)), [query, commands]);
  const at = Math.min(index, Math.max(0, results.length - 1));

  useEffect(() => {
    const before = document.activeElement as HTMLElement | null;
    return () => {
      if (before?.isConnected) before.focus?.();
    };
  }, []);
  useEffect(() => {
    const down = (e: PointerEvent) => !root.current?.contains(e.target as Node) && onClose();
    window.addEventListener("pointerdown", down);
    return () => window.removeEventListener("pointerdown", down);
  }, [onClose]);
  useEffect(() => {
    list.current?.children[at]?.scrollIntoView({ block: "nearest" });
  }, [at]);

  const menuName = (c: Command) => MENUS.find((m) => m.id === c.menu)?.label ?? "";
  const choose = (c: Command | undefined) => {
    if (!c || !isEnabled(c)) return;
    onClose();
    c.run();
  };
  const onKey = (e: KeyboardEvent) => {
    if (e.key === "ArrowDown" || e.key === "ArrowUp") {
      e.preventDefault();
      setIndex((at + (e.key === "ArrowDown" ? 1 : -1) + results.length) % Math.max(1, results.length));
    } else if (e.key === "Enter") {
      e.preventDefault();
      choose(results[at]);
    } else if (e.key === "Escape") {
      e.preventDefault();
      e.stopPropagation();
      onClose();
    }
  };

  return (
    <div className="cmd-search" role="dialog" aria-label="Command search" data-own-keys ref={root}>
      <input
        autoFocus
        className="cmd-input"
        role="combobox"
        aria-expanded="true"
        aria-controls="cmd-results"
        aria-activedescendant={results[at] ? `cmd-${results[at].id}` : undefined}
        aria-label="Search commands"
        placeholder={`Search commands (${describe(SEARCH_CHORD)})`}
        spellCheck={false}
        value={query}
        onChange={(e) => {
          setQuery(e.target.value);
          setIndex(0);
        }}
        onKeyDown={onKey}
      />
      <ul id="cmd-results" className="cmd-list" role="listbox" ref={list}>
        {results.map((c, i) => {
          const on = isEnabled(c);
          return (
            <li
              key={c.id}
              id={`cmd-${c.id}`}
              role="option"
              aria-selected={i === at}
              aria-disabled={on ? undefined : true}
              className={`cmd-item${i === at ? " on" : ""}`}
              title={on ? undefined : c.why}
              onPointerMove={() => setIndex(i)}
              onClick={() => choose(c)}
            >
              <span className="menu-label">{c.label}</span>
              <span className="menu-side">
                {!on && c.why ? <span className="menu-meta">{c.why}</span> : <span className="menu-meta">{menuName(c)}</span>}
                {c.shortcut && <kbd>{c.shortcut}</kbd>}
              </span>
            </li>
          );
        })}
        {results.length === 0 && <li className="cmd-empty">No command matches “{query}”.</li>}
      </ul>
    </div>
  );
}
