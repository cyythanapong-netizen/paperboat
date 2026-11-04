import { useState, type InputHTMLAttributes } from "react";

/** The number in `text`, if it is a whole, finite number that `valid` accepts. */
export function parseNumber(text: string, valid: (v: number) => boolean = () => true): number | undefined {
  if (text.trim() === "") return undefined;
  const v = Number(text);
  return Number.isFinite(v) && valid(v) ? v : undefined;
}

/**
 * A number you type freely: what you type stays while you edit (`-`, `-0.`, `1e-`, an empty
 * field), and the value changes whenever the text is a number `valid` accepts. Leaving the
 * field (or Enter) shows the value again; Escape too.
 */
export function NumberField({
  value,
  onChange,
  valid,
  ...rest
}: {
  value: number;
  onChange: (v: number) => void;
  valid?: (v: number) => boolean;
} & Omit<InputHTMLAttributes<HTMLInputElement>, "value" | "onChange" | "type">) {
  const [text, setText] = useState<string | null>(null); // null: not editing, show the value
  return (
    <input
      {...rest}
      type="text"
      inputMode="decimal"
      spellCheck={false}
      value={text ?? String(value)}
      onChange={(e) => {
        setText(e.target.value);
        const v = parseNumber(e.target.value, valid);
        if (v !== undefined && v !== value) onChange(v);
      }}
      onBlur={(e) => {
        setText(null);
        rest.onBlur?.(e);
      }}
      onKeyDown={(e) => {
        if (e.key === "Enter" || e.key === "Escape") e.currentTarget.blur();
        rest.onKeyDown?.(e);
      }}
    />
  );
}
