import {
  useEffect,
  useLayoutEffect,
  useRef,
  useState,
  type ReactNode,
} from "react";
import { createPortal } from "react-dom";
import { ChevronDown, Check } from "lucide-react";
export function Slider({
  label,
  value,
  min = 0,
  max = 1,
  step = 0.01,
  onChange,
  format,
  help,
  disabled = false,
  editScale,
  numericMin,
}: {
  label: string;
  value: number;
  min?: number;
  max?: number;
  step?: number;
  onChange: (v: number) => void;
  format?: (v: number) => string;
  help?: string;
  disabled?: boolean;
  editScale?: number;
  numericMin?: number;
}) {
  const id = label.toLowerCase().replace(/[^a-z0-9]+/g, "-");
  const [editing, setEditing] = useState(false);
  const [draft, setDraft] = useState("");
  const cancelled = useRef(false);
  const scale = editScale ?? (format ? 1 : 100);
  const lower = numericMin ?? min * scale;
  const commit = () => {
    setEditing(false);
    if (cancelled.current) {
      cancelled.current = false;
      return;
    }
    const entered = Number(draft);
    if (draft.trim() === "" || !Number.isFinite(entered)) return;
    const displayed = format ? parseFloat(format(value)) : value * scale;
    if (entered === Number(displayed.toFixed(4))) return;
    const raw = Math.max(min, Math.min(max, Math.max(lower, entered) / scale));
    const rounded = Math.max(
      min,
      Math.min(max, min + Math.round((raw - min) / step) * step),
    );
    if (Math.abs(rounded - value) > step * 0.001)
      onChange(Number(rounded.toFixed(8)));
  };
  return (
    <div className="slider-control">
      <div className="slider-label">
        <label htmlFor={id} title={help}>
          {label}
        </label>
        {editing ? (
          <input
            className="slider-value-input"
            type="number"
            inputMode="decimal"
            aria-label={`${label} numeric value`}
            value={draft}
            min={lower}
            max={max * scale}
            step={step * scale}
            autoFocus
            onFocus={(e) => e.currentTarget.select()}
            onChange={(e) => setDraft(e.target.value)}
            onBlur={commit}
            onKeyDown={(e) => {
              e.stopPropagation();
              if (e.key === "Enter") {
                e.preventDefault();
                e.currentTarget.blur();
              }
              if (e.key === "Escape") {
                e.preventDefault();
                cancelled.current = true;
                e.currentTarget.blur();
              }
            }}
          />
        ) : (
          <button
            type="button"
            className="slider-value"
            disabled={disabled}
            aria-label={`Edit ${label} value`}
            title={`Type a value (${lower}–${max * scale})`}
            onClick={() => {
              cancelled.current = false;
              setDraft(
                String(
                  Number(
                    (format
                      ? parseFloat(format(value))
                      : value * scale
                    ).toFixed(4),
                  ),
                ),
              );
              setEditing(true);
            }}
          >
            <output htmlFor={id}>
              {format ? format(value) : `${Math.round(value * 100)}%`}
            </output>
          </button>
        )}
      </div>
      <input
        id={id}
        aria-label={label}
        type="range"
        min={min}
        max={max}
        step={step}
        value={value}
        disabled={disabled}
        onChange={(e) => onChange(Number(e.target.value))}
        style={
          {
            "--fill": `${((value - min) / (max - min)) * 100}%`,
          } as React.CSSProperties
        }
      />
    </div>
  );
}
export function Toggle({
  checked,
  onChange,
  label,
  small = false,
}: {
  checked: boolean;
  onChange: (v: boolean) => void;
  label: string;
  small?: boolean;
}) {
  return (
    <button
      type="button"
      role="switch"
      aria-checked={checked}
      aria-label={label}
      className={`toggle ${checked ? "on" : ""} ${small ? "small" : ""}`}
      onClick={() => onChange(!checked)}
    >
      <span />
    </button>
  );
}
export function Menu({
  children,
  label,
  icon,
  className = "",
  align = "right",
  ariaLabel,
  disabled = false,
}: {
  children: (close: () => void) => ReactNode;
  label: ReactNode;
  icon?: ReactNode;
  className?: string;
  align?: "left" | "right";
  ariaLabel?: string;
  disabled?: boolean;
}) {
  const [open, setOpen] = useState(false);
  const ref = useRef<HTMLDivElement>(null),
    popover = useRef<HTMLDivElement>(null);
  const [position, setPosition] = useState({ left: 0, top: 0, maxHeight: 500 });
  useLayoutEffect(() => {
    if (!open) return;
    const place = () => {
      const trigger =
        ref.current?.querySelector<HTMLButtonElement>(".menu-trigger");
      const menu = popover.current;
      if (!trigger || !menu) return;
      const r = trigger.getBoundingClientRect(),
        width = menu.offsetWidth;
      const below = innerHeight - r.bottom - 16,
        above = r.top - 16;
      const desired = menu.scrollHeight;
      const up = below < Math.min(desired, 220) && above > below;
      const maxHeight = Math.max(100, Math.min(520, up ? above : below));
      const left = Math.max(
        10,
        Math.min(
          innerWidth - width - 10,
          align === "left" ? r.left : r.right - width,
        ),
      );
      const top = up
        ? Math.max(8, r.top - Math.min(desired, maxHeight) - 8)
        : r.bottom + 8;
      setPosition({ left, top, maxHeight });
    };
    place();
    window.addEventListener("resize", place);
    document.addEventListener("scroll", place, true);
    return () => {
      window.removeEventListener("resize", place);
      document.removeEventListener("scroll", place, true);
    };
  }, [open, align]);
  useEffect(() => {
    if (!open) return;
    const down = (e: PointerEvent) => {
      if (
        !ref.current?.contains(e.target as Node) &&
        !popover.current?.contains(e.target as Node)
      )
        setOpen(false);
    };
    const key = (e: KeyboardEvent) => {
      if (
        e.key === "Escape" &&
        !(
          e.target instanceof HTMLInputElement &&
          e.target.classList.contains("slider-value-input")
        )
      ) {
        e.preventDefault();
        e.stopPropagation();
        setOpen(false);
        ref.current?.querySelector<HTMLButtonElement>(".menu-trigger")?.focus();
      } else if (
        ["ArrowDown", "ArrowUp", "Home", "End"].includes(e.key) &&
        !(e.target instanceof HTMLInputElement)
      ) {
        const items = [
          ...(popover.current?.querySelectorAll<HTMLElement>(
            '[role="menuitem"]:not(:disabled)',
          ) ?? []),
        ];
        if (!items.length) return;
        e.preventDefault();
        e.stopPropagation();
        const index = items.indexOf(document.activeElement as HTMLElement);
        const next =
          e.key === "Home"
            ? 0
            : e.key === "End"
              ? items.length - 1
              : e.key === "ArrowDown"
                ? (index + 1) % items.length
                : index < 0
                  ? items.length - 1
                  : (index - 1 + items.length) % items.length;
        items[next].focus();
      }
    };
    document.addEventListener("pointerdown", down);
    document.addEventListener("keydown", key, true);
    return () => {
      document.removeEventListener("pointerdown", down);
      document.removeEventListener("keydown", key, true);
    };
  }, [open]);
  return (
    <div ref={ref} className={`menu ${className}`}>
      <button
        className="menu-trigger"
        type="button"
        aria-expanded={open}
        aria-haspopup="menu"
        aria-label={ariaLabel}
        disabled={disabled}
        onClick={() => setOpen(!open)}
      >
        {icon}
        {label}
        <ChevronDown size={12} />
      </button>
      {open &&
        createPortal(
          <div
            ref={popover}
            className={`menu-popover ${className} ${align}`}
            role="menu"
            style={position}
          >
            {children(() => setOpen(false))}
          </div>,
          document.fullscreenElement ?? document.body,
        )}
    </div>
  );
}

export function MenuItem({
  children,
  onClick,
  icon,
  active,
  description,
}: {
  children: ReactNode;
  onClick: () => void;
  icon?: ReactNode;
  active?: boolean;
  description?: string;
}) {
  return (
    <button
      role="menuitem"
      className={`menu-item ${active ? "is-selected" : ""}`}
      onClick={onClick}
    >
      {icon}
      <span>
        {children}
        {description && <small>{description}</small>}
      </span>
      {active && <Check size={13} />}
    </button>
  );
}
export function SectionHeading({
  children,
  detail,
}: {
  children: ReactNode;
  detail?: ReactNode;
}) {
  return (
    <div className="section-heading">
      <h2>{children}</h2>
      {detail}
    </div>
  );
}
