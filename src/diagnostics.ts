import { version } from "../package.json";
import { hasVisibleFrame } from "./engine/startup";

type Level = "info" | "warn" | "error";
interface Entry {
  at: string;
  level: Level;
  scope: string;
  message: string;
  detail: string;
  repeats: number;
}

// Reports are explicitly copied/downloaded by the user. No telemetry request,
// local storage, raw volume, cookies, URL credentials, query strings or hashes.
export function redactText(text: string): string {
  return text
    .replace(/https?:\/\/[^\s<>"']+/g, (address) => {
      try {
        const url = new URL(address);
        return `${url.origin}${url.pathname}`;
      } catch {
        return "[URL omitted]";
      }
    })
    .replace(/Bearer\s+\S+/gi, "Bearer [redacted]")
    .slice(0, 4096);
}
export function diagnosticJSON(value: unknown): string {
  const seen = new WeakSet<object>();
  function safe(v: unknown, depth = 0): unknown {
    if (typeof v === "string") return redactText(v);
    if (typeof v === "number") return Number.isFinite(v) ? v : String(v);
    if (typeof v === "bigint") return String(v);
    if (v == null || typeof v === "boolean") return v;
    if (typeof v !== "object") return String(v);
    if (depth > 7) return "[depth limit]";
    if (seen.has(v)) return "[circular]";
    seen.add(v);
    if (v instanceof Error)
      return safe(
        { name: v.name, message: v.message, stack: v.stack },
        depth + 1,
      );
    if (ArrayBuffer.isView(v) || v instanceof ArrayBuffer)
      return `[${v.constructor.name}: ${v.byteLength} bytes omitted]`;
    if (Array.isArray(v))
      return v.slice(0, 64).map((item) => safe(item, depth + 1));
    return Object.fromEntries(
      Object.entries(v)
        .slice(0, 80)
        .map(([key, item]) => [
          key,
          /password|token|secret|authorization|cookie/i.test(key)
            ? "[redacted]"
            : safe(item, depth + 1),
        ]),
    );
  }
  try {
    return JSON.stringify(safe(value), null, 2) ?? "null";
  } catch {
    return "[unserializable diagnostic]";
  }
}

export class DiagnosticLog {
  private entries: Entry[] = [];
  private revision = 0;
  private listeners = new Set<() => void>();
  constructor(private limit = 160) {}
  subscribe = (listener: () => void) => {
    this.listeners.add(listener);
    return () => {
      this.listeners.delete(listener);
    };
  };
  getRevision = () => this.revision;
  log(scope: string, message: string, data?: unknown, level: Level = "info") {
    const detail =
      data === undefined ? "" : diagnosticJSON(data).slice(0, 10000);
    const cleanMessage = redactText(message);
    const previous = this.entries.at(-1);
    const at = new Date().toISOString();
    if (
      previous?.scope === scope &&
      previous.message === cleanMessage &&
      previous.detail === detail &&
      previous.level === level
    ) {
      previous.repeats++;
      previous.at = at;
    } else {
      this.entries.push({
        at,
        scope,
        message: cleanMessage,
        detail,
        level,
        repeats: 1,
      });
      if (this.entries.length > this.limit) this.entries.shift();
    }
    this.revision++;
    this.listeners.forEach((listener) => listener());
  }
  report(snapshot: unknown = null): string {
    const events = this.entries
      .map(
        (e) =>
          `${e.at} [${e.level.toUpperCase()}] ${e.scope}: ${e.message}${e.repeats > 1 ? ` (repeated ${e.repeats} times)` : ""}${e.detail ? `\n${e.detail}` : ""}`,
      )
      .join("\n\n");
    return [
      `FRONTIER GPU DIAGNOSTICS — v${version}`,
      `Collected: ${new Date().toISOString()}`,
      "Local report; nothing is uploaded automatically. Raw terrain data is omitted.",
      "GPU readback / FPS is NOT proof that the browser compositor displayed the canvas.",
      "\n[BROWSER]",
      diagnosticJSON(browserSnapshot()),
      "\n[CURRENT RENDERER AND CANVAS]",
      diagnosticJSON(snapshot),
      "\n[EVENT LOG — latest entries, oldest first]",
      events.slice(-100_000),
      "\nEND FRONTIER GPU DIAGNOSTICS",
    ].join("\n");
  }
}
export const diagnostics = new DiagnosticLog();

export function browserSnapshot() {
  if (typeof window === "undefined") return { browser: "not available" };
  const policy =
    (
      document as Document & {
        permissionsPolicy?: {
          features?: () => string[];
          allowsFeature: (name: string) => boolean;
        };
        featurePolicy?: {
          features?: () => string[];
          allowsFeature: (name: string) => boolean;
        };
      }
    ).permissionsPolicy ?? (document as any).featurePolicy;
  const allowed = (name: string) => {
    try {
      return policy?.features?.().includes(name)
        ? policy.allowsFeature(name)
        : "not exposed";
    } catch {
      return "not exposed";
    }
  };
  let referrerOrigin = "none";
  try {
    if (document.referrer) referrerOrigin = new URL(document.referrer).origin;
  } catch {
    /* optional */
  }
  const requested = new URLSearchParams(location.search).get("renderer");
  const display = new URLSearchParams(location.search).get("display");
  return {
    appVersion: version,
    displayRequested:
      display === "safe" || display === "native" ? display : "auto",
    page: location.origin + location.pathname,
    rendererRequested:
      requested === "webgpu" || requested === "webgl" ? requested : "auto",
    userAgent: navigator.userAgent,
    platform: navigator.platform,
    language: navigator.language,
    hardwareConcurrency: navigator.hardwareConcurrency,
    deviceMemoryGiB:
      (navigator as Navigator & { deviceMemory?: number }).deviceMemory ??
      "not exposed",
    secureContext: window.isSecureContext,
    webgpuExposed: !!navigator.gpu,
    embedded: window.self !== window.top,
    referrerOrigin,
    visibility: document.visibilityState,
    focused: document.hasFocus(),
    devicePixelRatio: window.devicePixelRatio,
    windowSize: [window.innerWidth, window.innerHeight],
    visualViewport: window.visualViewport && {
      width: window.visualViewport.width,
      height: window.visualViewport.height,
      scale: window.visualViewport.scale,
    },
    permissionsPolicy: {
      webgpu: allowed("webgpu"),
      clipboardWrite: allowed("clipboard-write"),
    },
  };
}

let browserDiagnosticsInstalled = false;
export function installBrowserDiagnostics() {
  if (browserDiagnosticsInstalled) return;
  browserDiagnosticsInstalled = true;
  diagnostics.log("App", "Diagnostic recording started", browserSnapshot());
  window.addEventListener(
    "error",
    (event: Event) => {
      if (event instanceof ErrorEvent) {
        diagnostics.log(
          "Browser",
          event.message || "JavaScript error",
          {
            error: event.error,
            file: event.filename,
            line: event.lineno,
            column: event.colno,
          },
          "error",
        );
      } else if (event.target instanceof Element) {
        diagnostics.log(
          "Asset",
          "Resource failed to load",
          {
            tag: event.target.tagName,
            resource:
              event.target.getAttribute("src") ||
              event.target.getAttribute("href"),
          },
          "error",
        );
      }
    },
    true,
  );
  window.addEventListener("unhandledrejection", (event) => {
    diagnostics.log(
      "Browser",
      "Unhandled promise rejection",
      event.reason,
      "error",
    );
  });
  document.addEventListener("visibilitychange", () => {
    diagnostics.log("Page", "Visibility changed", {
      visibility: document.visibilityState,
    });
  });
}

const rect = (el: Element) => {
  const r = el.getBoundingClientRect();
  return {
    x: Math.round(r.x),
    y: Math.round(r.y),
    width: Math.round(r.width),
    height: Math.round(r.height),
  };
};
const describeElement = (el: Element) => {
  const css = getComputedStyle(el);
  return {
    element: `${el.tagName.toLowerCase()}${el.id ? `#${el.id}` : ""}${[...el.classList].map((c) => `.${c}`).join("")}`,
    rect: rect(el),
    display: css.display,
    visibility: css.visibility,
    opacity: css.opacity,
    position: css.position,
    zIndex: css.zIndex,
    transform: css.transform,
    filter: css.filter,
    isolation: css.isolation,
    overflow: css.overflow,
    pointerEvents: css.pointerEvents,
    background: css.backgroundColor,
  };
};
export function canvasSnapshot(
  canvas: HTMLCanvasElement,
  container: HTMLElement,
) {
  const ancestors: ReturnType<typeof describeElement>[] = [];
  let parent: Element | null = canvas;
  while (parent && ancestors.length < 7) {
    ancestors.push(describeElement(parent));
    parent = parent.parentElement;
  }
  const r = canvas.getBoundingClientRect();
  return {
    connected: canvas.isConnected,
    ownsViewport: canvas.parentElement === container,
    canvasCountInViewport: container.querySelectorAll("canvas").length,
    bitmapSize: [canvas.width, canvas.height],
    clientSize: [canvas.clientWidth, canvas.clientHeight],
    parentSize: [container.clientWidth, container.clientHeight],
    ancestors,
    centerHitStack: document
      .elementsFromPoint(r.x + r.width / 2, r.y + r.height / 2)
      .slice(0, 5)
      .map(describeElement),
    openDialogs: [...document.querySelectorAll("dialog[open]")].map((d) =>
      d.getAttribute("aria-label"),
    ),
  };
}

/** Small statistical summaries only; no scene image or volume is retained. */
export function summarizePixels(pixels: ArrayLike<number>, bgra = false) {
  const count = Math.floor(pixels.length / 4);
  const min = [255, 255, 255, 255],
    max = [0, 0, 0, 0],
    sum = [0, 0, 0, 0];
  let opaque = 0,
    black = 0,
    sandstone = 0;
  for (let i = 0; i < count; i++) {
    const at = i * 4;
    const color = [
      pixels[at + (bgra ? 2 : 0)],
      pixels[at + 1],
      pixels[at + (bgra ? 0 : 2)],
      pixels[at + 3],
    ];
    for (let c = 0; c < 4; c++) {
      min[c] = Math.min(min[c], color[c]);
      max[c] = Math.max(max[c], color[c]);
      sum[c] += color[c];
    }
    if (color[3] >= 128) opaque++;
    if (Math.max(...color.slice(0, 3)) < 5) black++;
    if (
      color[3] >= 128 &&
      color[0] > 55 &&
      color[0] > color[1] * 1.18 &&
      color[1] > color[2] * 1.1
    )
      sandstone++;
  }
  return {
    samples: count,
    nonUniformOpaque: hasVisibleFrame(pixels),
    minRGBA: min,
    maxRGBA: max,
    meanRGBA: sum.map((s) => Math.round(s / Math.max(1, count))),
    opaquePercent: +((opaque * 100) / Math.max(1, count)).toFixed(1),
    blackPercent: +((black * 100) / Math.max(1, count)).toFixed(1),
    sandstoneColorPercent: +((sandstone * 100) / Math.max(1, count)).toFixed(1),
    note: "Sandstone color is a heuristic, not proof of geometry or browser presentation.",
  };
}
export function summarizeVolume(data: Float32Array) {
  let min = Infinity,
    max = -Infinity,
    solid = 0,
    nonFinite = 0;
  for (let i = 0; i < data.length; i += 4) {
    const d = data[i];
    if (!Number.isFinite(d)) {
      nonFinite++;
      continue;
    }
    min = Math.min(min, d);
    max = Math.max(max, d);
    if (d < 0) solid++;
  }
  return {
    voxels: data.length / 4,
    minSDF: min,
    maxSDF: max,
    solidVoxels: solid,
    nonFiniteSDF: nonFinite,
  };
}
