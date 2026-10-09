import React, { useEffect, useRef, useState } from "react";
import { Palette } from "lucide-react";
import { Slider } from "./MaterialControls.jsx";
import "./colourPicker.css";
export function hexToHSV(hex) {
  const rgb = [1, 3, 5].map((i) => parseInt(hex.slice(i, i + 2), 16) / 255),
    max = Math.max(...rgb),
    min = Math.min(...rgb),
    d = max - min;
  let h = 0;
  if (d) {
    if (max === rgb[0]) h = ((rgb[1] - rgb[2]) / d) % 6;
    else if (max === rgb[1]) h = (rgb[2] - rgb[0]) / d + 2;
    else h = (rgb[0] - rgb[1]) / d + 4;
    h = (h * 60 + 360) % 360;
  }
  return { h, s: max ? d / max : 0, v: max };
}
export function hsvToHex(h, s, v) {
  h = ((h % 360) + 360) % 360;
  s = Math.min(1, Math.max(0, s));
  v = Math.min(1, Math.max(0, v));
  const c = v * s,
    x = c * (1 - Math.abs(((h / 60) % 2) - 1)),
    m = v - c;
  const rgb =
    h < 60
      ? [c, x, 0]
      : h < 120
        ? [x, c, 0]
        : h < 180
          ? [0, c, x]
          : h < 240
            ? [0, x, c]
            : h < 300
              ? [x, 0, c]
              : [c, 0, x];
  return (
    "#" +
    rgb
      .map((a) =>
        Math.round((a + m) * 255)
          .toString(16)
          .padStart(2, "0"),
      )
      .join("")
  );
}
export default function ColourPicker({
  value,
  onChange,
  ariaLabel = "Colour",
  disabled = false,
  swatches = [],
}) {
  const hsv = hexToHSV(value),
    [hue, setHue] = useState(hsv.h),
    [hex, setHex] = useState(value),
    pointer = useRef(null);
  useEffect(() => {
    setHex(value);
    const next = hexToHSV(value);
    if (next.s > 0.001 && next.v > 0.001) setHue(next.h);
  }, [value]);
  const pick = (s, v) => onChange(hsvToHex(hue, s, v));
  const sample = (e) => {
    const box = e.currentTarget.getBoundingClientRect();
    pick(
      Math.min(1, Math.max(0, (e.clientX - box.left) / box.width)),
      1 - Math.min(1, Math.max(0, (e.clientY - box.top) / box.height)),
    );
  };
  return (
    <section
      className="editor-colour-picker tp-shared-controls"
      aria-label={`${ariaLabel} picker`}
    >
      <header>
        <Palette size={12} />
        <span>Colour picker</span>
        <small>sRGB</small>
      </header>
      <div
        className="colour-picker-square"
        role="slider"
        tabIndex={disabled ? -1 : 0}
        aria-label={`${ariaLabel} saturation and brightness`}
        aria-disabled={disabled}
        aria-valuemin={0}
        aria-valuemax={100}
        aria-valuenow={Math.round(hsv.s * 100)}
        aria-valuetext={`${Math.round(hsv.s * 100)}% saturation, ${Math.round(hsv.v * 100)}% brightness`}
        style={{ "--hue-colour": hsvToHex(hue, 1, 1) }}
        onPointerDown={(e) => {
          if (disabled || e.button !== 0) return;
          e.preventDefault();
          e.currentTarget.focus();
          pointer.current = e.pointerId;
          e.currentTarget.setPointerCapture(e.pointerId);
          sample(e);
        }}
        onPointerMove={(e) => {
          if (pointer.current === e.pointerId && !disabled) sample(e);
        }}
        onPointerUp={() => (pointer.current = null)}
        onPointerCancel={() => (pointer.current = null)}
        onLostPointerCapture={() => (pointer.current = null)}
        onKeyDown={(e) => {
          if (
            disabled ||
            ![
              "ArrowLeft",
              "ArrowRight",
              "ArrowUp",
              "ArrowDown",
              "Home",
              "End",
            ].includes(e.key)
          )
            return;
          e.preventDefault();
          const step = e.shiftKey ? 0.1 : 0.01;
          pick(
            e.key === "Home"
              ? 0
              : e.key === "End"
                ? 1
                : Math.min(
                    1,
                    Math.max(
                      0,
                      hsv.s +
                        (e.key === "ArrowRight"
                          ? step
                          : e.key === "ArrowLeft"
                            ? -step
                            : 0),
                    ),
                  ),
            Math.min(
              1,
              Math.max(
                0,
                hsv.v +
                  (e.key === "ArrowUp"
                    ? step
                    : e.key === "ArrowDown"
                      ? -step
                      : 0),
              ),
            ),
          );
        }}
      >
        <i
          style={{
            left: `${hsv.s * 100}%`,
            top: `${(1 - hsv.v) * 100}%`,
            background: value,
          }}
        />
      </div>
      <div className="colour-picker-hue">
        <Slider
          label="Hue"
          ariaLabel={`${ariaLabel} hue`}
          value={hue}
          min={0}
          max={360}
          step={1}
          unit="°"
          disabled={disabled}
          onChange={(h) => {
            setHue(h);
            onChange(hsvToHex(h, hsv.s, hsv.v));
          }}
        />
      </div>
      <div className="colour-picker-hex">
        <i style={{ background: value }} />
        <input
          aria-label={ariaLabel}
          disabled={disabled}
          value={hex.toUpperCase()}
          maxLength={7}
          spellCheck={false}
          onChange={(e) => {
            setHex(e.target.value);
            if (/^#[\da-f]{6}$/i.test(e.target.value))
              onChange(e.target.value.toLowerCase());
          }}
          onBlur={() => {
            if (!/^#[\da-f]{6}$/i.test(hex)) setHex(value);
          }}
        />
        <span>HEX</span>
      </div>
      {!!swatches.length && (
        <div className="colour-picker-swatches">
          {swatches.map((c) => (
            <button
              key={c}
              aria-label={`Paint swatch ${c}`}
              aria-pressed={value.toLowerCase() === c.toLowerCase()}
              disabled={disabled}
              title={c}
              style={{ background: c }}
              onClick={() => onChange(c)}
            />
          ))}
        </div>
      )}
    </section>
  );
}
