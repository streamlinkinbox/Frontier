import { useEffect, useRef, useState, type ReactNode } from "react";
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
}) {
  const id = label.toLowerCase().replace(/[^a-z0-9]+/g, "-");
  return (
    <div className="slider-control">
      <div className="slider-label">
        <label htmlFor={id} title={help}>
          {label}
        </label>
        <output htmlFor={id}>
          {format ? format(value) : `${Math.round(value * 100)}%`}
        </output>
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
  const [open, setOpen] = useState(false),
    ref = useRef<HTMLDivElement>(null);
  useEffect(() => {
    if (!open) return;
    const down = (e: PointerEvent) => {
      if (!ref.current?.contains(e.target as Node)) setOpen(false);
    };
    const key = (e: KeyboardEvent) => {
      if (e.key === "Escape") setOpen(false);
    };
    document.addEventListener("pointerdown", down);
    document.addEventListener("keydown", key);
    return () => {
      document.removeEventListener("pointerdown", down);
      document.removeEventListener("keydown", key);
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
      {open && (
        <div className={`menu-popover ${align}`} role="menu">
          {children(() => setOpen(false))}
        </div>
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
    <button role="menuitem" className="menu-item" onClick={onClick}>
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
