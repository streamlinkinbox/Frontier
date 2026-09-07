import { useCallback, useEffect, useRef, useState } from "react";
import {
  Mountain,
  ChevronRight,
  ChevronDown,
  ArrowUpRight,
  Save,
  Download,
  BookOpen,
  Undo2,
  Redo2,
  MousePointer2,
  Plus,
  Minus,
  Waves,
  Layers3,
  Eye,
  EyeOff,
  Sun,
  Wind,
  Droplets,
  Box,
  Grid2X2,
  Maximize,
  Minimize,
  RotateCcw,
  Play,
  Pause,
  SkipForward,
  SlidersHorizontal,
  Shuffle,
  ArrowRight,
  Check,
  LoaderCircle,
  X,
  Keyboard,
  PanelLeft,
  Image,
  FileBox,
  FolderOpen,
  SplitSquareHorizontal,
  MoveHorizontal,
  Info,
  Square,
  CloudSun,
  Cpu,
  Navigation,
  TerminalSquare,
  Zap,
  Scissors,
  Gem,
} from "lucide-react";
import { TerrainEngine } from "./engine/TerrainEngine";
import {
  DEFAULT_SETTINGS,
  PRESETS,
  type Settings,
  type Tool,
  type Preset,
  type EngineStats,
} from "./engine/types";
import {
  download,
  encodeProject,
  decodeProject,
  saveLocal,
  loadLocal,
  type Project,
} from "./engine/project";
import {
  Slider,
  Toggle,
  Menu,
  MenuItem,
  SectionHeading,
} from "./components/Controls";
import { Dialog, Guide } from "./components/Guide";
import { MaterialEditor } from "./components/MaterialEditor";
import { MATERIAL_PRESETS } from "./engine/materials";
import { DiagnosticsPanel } from "./components/DiagnosticsPanel";
import { diagnostics } from "./diagnostics";
import "./styles.css";
import { version } from "../package.json";

declare global {
  interface Window {
    __frontier?: TerrainEngine;
  }
}
const TOOL_LIST: {
  id: Tool;
  label: string;
  icon: typeof Plus;
  key: string;
  description: string;
}[] = [
  {
    id: "add",
    label: "Add",
    icon: Plus,
    key: "B",
    description: "Build up rock in a spherical volume",
  },
  {
    id: "carve",
    label: "Carve",
    icon: Minus,
    key: "X",
    description: "Cut into the volume — caves and overhangs welcome",
  },
  {
    id: "smooth",
    label: "Smooth",
    icon: Waves,
    key: "M",
    description: "Relax the signed-distance field around the brush",
  },
  {
    id: "flatten",
    label: "Flatten",
    icon: MoveHorizontal,
    key: "L",
    description:
      "Flatten to the plane captured at stroke start; no moving-height ridges",
  },
  {
    id: "ridges",
    label: "Ridges",
    icon: Layers3,
    key: "R",
    description:
      "Legacy flatten: a moving horizontal shelf with spherical falloff",
  },
  {
    id: "crack",
    label: "Cracks",
    icon: Zap,
    key: "K",
    description: "Carve a branching, uneven fault into the rock",
  },
  {
    id: "crevice",
    label: "Crevice",
    icon: Scissors,
    key: "U",
    description: "Cut a deep, wide V-shaped fissure",
  },
  {
    id: "boulder",
    label: "Boulder",
    icon: Gem,
    key: "O",
    description:
      "Place irregular, partly embedded rock; drag to space more boulders",
  },
];
const percent = (v: number) => `${Math.round(v * 100)}%`;
export default function App() {
  const [settings, setSettings] = useState<Settings>({ ...DEFAULT_SETTINGS });
  const settingsRef = useRef(settings);
  settingsRef.current = settings;
  const [tool, setTool] = useState<Tool>("orbit");
  const [logsOpen, setLogsOpen] = useState(
    () => new URLSearchParams(location.search).get("diagnostics") === "1",
  );
  const [stats, setStats] = useState<EngineStats>({
    fps: 0,
    steps: 0,
    backend: "Connecting",
    size: { x: 144, y: 72, z: 144 },
    running: false,
  });
  const [ready, setReady] = useState(false),
    [error, setError] = useState(""),
    [busy, setBusy] = useState("");
  const [startupMessage, setStartupMessage] = useState(
    "Initializing the terrain volume and graphics pipeline…",
  );
  const [dirty, setDirty] = useState(false),
    [saved, setSaved] = useState(false);
  const [history, setHistory] = useState([0, 0]);
  const [inspector, setInspector] = useState<
    "erosion" | "environment" | "materials"
  >("erosion");
  const [erosionTab, setErosionTab] = useState<
    "hydraulic" | "thermal" | "wind"
  >("hydraulic");
  const [guide, setGuide] = useState(false),
    [comparing, setComparing] = useState(false),
    [full, setFull] = useState(false),
    [leftOpen, setLeftOpen] = useState(false);
  const [confirm, setConfirm] = useState<{
    title: string;
    description: string;
    action: () => void;
  } | null>(null);
  const [toast, setToast] = useState<{
    message: string;
    error?: boolean;
  } | null>(null);
  const [seedDraft, setSeedDraft] = useState(String(settings.seed));
  const [savedAvailable, setSavedAvailable] = useState(false);
  const viewportRef = useRef<HTMLDivElement>(null),
    stageRef = useRef<HTMLDivElement>(null),
    fileRef = useRef<HTMLInputElement>(null),
    engine = useRef<TerrainEngine | null>(null);
  const toastTimer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const openLogs = () => {
    // Capture the real canvas hit stack BEFORE the modal covers the viewport.
    try {
      diagnostics.log(
        "UI",
        "GPU logs opened (snapshot before dialog)",
        engine.current?.getDiagnostics(),
      );
    } catch (error) {
      diagnostics.log("UI", "Canvas snapshot unavailable", error, "warn");
    }
    setLogsOpen(true);
  };
  const currentMaterial = MATERIAL_PRESETS.find(
    (p) => p.id === settings.material,
  )!;
  const changeMaterial = (patch: Partial<Settings>) => {
    const next = { ...settingsRef.current, ...patch };
    settingsRef.current = next;
    setSettings(next);
    engine.current?.update(next, false);
    setDirty(true);
    setSaved(false);
  };
  const currentPreset = PRESETS.find((p) => p.id === settings.preset)!;
  const notify = useCallback((message: string, isError = false) => {
    setToast({ message, error: isError });
    if (toastTimer.current) clearTimeout(toastTimer.current);
    toastTimer.current = setTimeout(
      () => setToast(null),
      isError ? 8000 : 4200,
    );
  }, []);
  useEffect(() => {
    if (!viewportRef.current) return;
    const initialSettings = { ...DEFAULT_SETTINGS, ...settingsRef.current };
    settingsRef.current = initialSettings;
    setSettings(initialSettings);
    // A remount must not inherit the old engine's ready/FPS indicator while a
    // replacement device is still compiling or validating its first frame.
    setReady(false);
    setError("");
    setStartupMessage("Initializing the terrain volume and graphics pipeline…");
    setStats((s) => ({
      ...s,
      backend: "Connecting",
      fps: 0,
      steps: 0,
      running: false,
    }));
    setHistory([0, 0]);
    setTool("orbit");
    setComparing(false);
    let active = true;
    const e = new TerrainEngine(viewportRef.current, settingsRef.current, {
      stats: (s) => {
        if (active) setStats(s);
      },
      status: (message) => {
        if (active) setStartupMessage(message);
      },
      changed: () => {
        if (active) {
          setDirty(true);
          setSaved(false);
        }
      },
      history: (u, r) => {
        if (active) setHistory([u, r]);
      },
      notice: (m) => {
        if (active) notify(m);
      },
      error: (m) => {
        if (active) {
          setError(m);
          notify(m, true);
        }
      },
      ready: (backend) => {
        if (active) {
          setError("");
          setReady(true);
          if (backend === "WebGL2") {
            const next = { ...settingsRef.current, speed: 1 };
            settingsRef.current = next;
            setSettings(next);
            e.update(next);
          }
        }
      },
    });
    engine.current = e;
    if (import.meta.env.DEV) window.__frontier = e;
    void e.initialize().catch((err) => {
      diagnostics.log(
        "Startup",
        "Renderer initialization rejected",
        err,
        "error",
      );
      if (active) {
        setError(err instanceof Error ? err.message : String(err));
        notify(
          "The renderer could not start. See the viewport for details.",
          true,
        );
      }
    });
    void loadLocal()
      .then((p) => {
        if (active) setSavedAvailable(!!p);
      })
      .catch(() => {});
    return () => {
      active = false;
      e.dispose();
      if (toastTimer.current) clearTimeout(toastTimer.current);
    };
  }, [notify]);
  const update = useCallback(
    <K extends keyof Settings>(key: K, value: Settings[K], preview = false) => {
      const next = { ...settingsRef.current, [key]: value };
      settingsRef.current = next;
      setSettings(next);
      engine.current?.update(next, preview);
      setDirty(true);
      setSaved(false);
    },
    [],
  );
  const chooseTool = useCallback((t: Tool) => {
    setTool(t);
    engine.current?.setTool(t);
  }, []);
  const compare = useCallback((v: boolean) => {
    setComparing(v);
    if (engine.current) engine.current.compare = v;
  }, []);
  const perform = useCallback(
    async (label: string, action: () => Promise<void>) => {
      if (!engine.current || !ready || busy) return;
      engine.current.pause();
      setBusy(label);
      try {
        await action();
      } catch (e) {
        notify(e instanceof Error ? e.message : String(e), true);
      } finally {
        setBusy("");
      }
    },
    [ready, busy, notify],
  );
  const undo = useCallback(
    (redo = false) => {
      if (!ready || busy) return;
      void engine.current?.undo(redo).catch((e) => notify(String(e), true));
    },
    [ready, busy, notify],
  );
  const toggleRun = useCallback(() => {
    if (!ready || busy) return;
    void engine.current
      ?.toggleSimulation()
      .catch((e) => notify(String(e), true));
  }, [ready, busy, notify]);
  useEffect(() => {
    const down = (e: KeyboardEvent) => {
      if (
        e.defaultPrevented ||
        /INPUT|TEXTAREA|SELECT/.test((e.target as HTMLElement).tagName) ||
        guide ||
        logsOpen ||
        confirm ||
        busy ||
        !ready ||
        !!error
      )
        return;
      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "z") {
        e.preventDefault();
        undo(e.shiftKey);
        return;
      }
      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "y") {
        e.preventDefault();
        undo(true);
        return;
      }
      if (e.ctrlKey || e.metaKey || e.altKey) return;
      if (e.key === " ") {
        if ((e.target as HTMLElement).closest("button, a")) return;
        e.preventDefault();
        if (!e.repeat) toggleRun();
      }
      if (e.key.toLowerCase() === "c") {
        e.preventDefault();
        compare(true);
      }
      if (e.key.toLowerCase() === "v" || e.key === "Escape")
        chooseTool("orbit");
      if (e.key.toLowerCase() === "b") chooseTool("add");
      if (e.key.toLowerCase() === "x") chooseTool("carve");
      if (e.key.toLowerCase() === "m") chooseTool("smooth");
      if (e.key.toLowerCase() === "l") chooseTool("flatten");
      if (e.key.toLowerCase() === "r") chooseTool("ridges");
      if (e.key.toLowerCase() === "k") chooseTool("crack");
      if (e.key.toLowerCase() === "u") chooseTool("crevice");
      if (e.key.toLowerCase() === "o") chooseTool("boulder");
      if (e.key.toLowerCase() === "f") engine.current?.resetCamera();
      if (e.key.toLowerCase() === "g")
        update("grid", !settingsRef.current.grid);
      if (e.key === "[" || e.key === "]")
        update(
          "radius",
          Math.max(
            1,
            Math.min(
              12,
              settingsRef.current.radius + (e.key === "]" ? 0.5 : -0.5),
            ),
          ),
        );
    };
    const up = (e: KeyboardEvent) => {
      if (e.key.toLowerCase() === "c") compare(false);
    };
    const blur = () => compare(false);
    window.addEventListener("keydown", down);
    window.addEventListener("keyup", up);
    window.addEventListener("blur", blur);
    return () => {
      window.removeEventListener("keydown", down);
      window.removeEventListener("keyup", up);
      window.removeEventListener("blur", blur);
    };
  }, [
    ready,
    busy,
    error,
    guide,
    logsOpen,
    confirm,
    undo,
    toggleRun,
    compare,
    chooseTool,
    update,
  ]);
  useEffect(() => {
    const f = () => setFull(!!document.fullscreenElement);
    document.addEventListener("fullscreenchange", f);
    return () => document.removeEventListener("fullscreenchange", f);
  }, []);
  const regenerate = (preset: Preset, seed = settingsRef.current.seed) => {
    const execute = () =>
      void perform("Forming your landscape…", async () => {
        const next = {
          ...settingsRef.current,
          preset,
          seed,
          water:
            preset === settingsRef.current.preset
              ? settingsRef.current.water
              : preset === "canyon",
        };
        setSettings(next);
        settingsRef.current = next;
        setSeedDraft(String(seed));
        await engine.current!.regenerate(next);
        chooseTool("orbit");
        setDirty(true);
        notify(`${PRESETS.find((p) => p.id === preset)!.name} · seed ${seed}`);
      });
    if (dirty || stats.steps > 0)
      setConfirm({
        title: "Start a fresh landscape?",
        description:
          "This replaces your current sculpting and erosion. Save or export the current project first if you want to keep it.",
        action: execute,
      });
    else execute();
  };
  const applySeed = () => {
    const value = Number(seedDraft);
    if (
      !seedDraft.trim() ||
      !Number.isInteger(value) ||
      value < 0 ||
      value > 999999
    ) {
      notify("Use a whole-number seed between 0 and 999999.", true);
      return;
    }
    regenerate(settings.preset, value);
  };
  const randomize = () =>
    regenerate(
      settings.preset,
      crypto.getRandomValues(new Uint32Array(1))[0] % 1000000,
    );
  const project = async (): Promise<Project> => ({
    version: 1,
    settings: { ...settingsRef.current },
    size: { ...engine.current!.backend.size },
    steps: engine.current!.steps,
    savedAt: new Date().toISOString(),
    data: await engine.current!.backend.readVolume(),
  });
  const save = () =>
    void perform("Saving to this browser…", async () => {
      await saveLocal(await project());
      setDirty(false);
      setSaved(true);
      setSavedAvailable(true);
      notify("Project saved locally, including all sculpting and erosion.");
    });
  const load = (p: Project) =>
    void perform("Restoring the 3D volume…", async () => {
      await engine.current!.load(p.data, p.size, p.settings, p.steps);
      setSettings(p.settings);
      settingsRef.current = p.settings;
      setSeedDraft(String(p.settings.seed));
      chooseTool("orbit");
      setDirty(false);
      setSaved(true);
      notify("Your terrain is right where you left it.");
    });
  const requestLoad = (p: Project) => {
    if (dirty)
      setConfirm({
        title: "Open this project?",
        description:
          "Opening a project replaces the current volume. Unsaved edits will be lost.",
        action: () => load(p),
      });
    else load(p);
  };
  const openSaved = async () => {
    try {
      const p = await loadLocal();
      if (!p) {
        notify(
          "No saved project yet. Use Save to keep this terrain in your browser.",
        );
        return;
      }
      requestLoad(p);
    } catch (e) {
      notify(String(e), true);
    }
  };
  const importFile = async (file: File) => {
    try {
      if (file.size > 64 * 1024 * 1024)
        throw new Error("Projects must be smaller than 64 MB.");
      requestLoad(decodeProject(await file.arrayBuffer()));
    } catch (e) {
      notify(e instanceof Error ? e.message : String(e), true);
    }
  };
  const exportFile = (kind: "glb" | "project" | "image") =>
    void perform(
      kind === "glb"
        ? "Extracting the terrain surface…"
        : "Preparing your export…",
      async () => {
        const name = `frontier-${settingsRef.current.preset}-${settingsRef.current.seed}`;
        if (kind === "project") {
          download(encodeProject(await project()), `${name}.frontier`);
          notify("Editable volume exported. Reopen it with Import project.");
        }
        if (kind === "image") {
          download(await engine.current!.capture(), `${name}.png`);
          notify("Viewport image exported.");
        }
        if (kind === "glb") {
          const data = await engine.current!.backend.readVolume(),
            size = engine.current!.backend.size;
          const result = await new Promise<{
            buffer: ArrayBuffer;
            triangles: number;
          }>((resolve, reject) => {
            const worker = new Worker(
              new URL("./engine/export.worker.ts", import.meta.url),
              { type: "module" },
            );
            worker.onmessage = (e) => {
              if (e.data.error) {
                worker.terminate();
                reject(new Error(e.data.error));
              } else if (e.data.buffer) {
                worker.terminate();
                resolve(e.data);
              } else
                setBusy(
                  `Extracting the terrain surface… ${Math.round(e.data.progress * 100)}%`,
                );
            };
            worker.onerror = (e) => {
              worker.terminate();
              reject(new Error(e.message));
            };
            worker.postMessage({ data, size, settings: settingsRef.current }, [
              data.buffer,
            ]);
          });
          download(
            new Blob([result.buffer], { type: "model/gltf-binary" }),
            `${name}.glb`,
          );
          notify(
            `Terrain exported · ${result.triangles.toLocaleString()} triangles · vertex-colored GLB`,
          );
        }
      },
    );
  const weatherProfile = (kind: "rain" | "wind" | "talus") => {
    const patch: Partial<Settings> =
      kind === "rain"
        ? {
            rainfall: 0.85,
            erosion: 0.8,
            channeling: 1,
            cohesion: 0.12,
            sediment: 0.85,
            settling: 0.2,
            evaporation: 0.12,
            thermal: 0,
            windErosion: 0,
          }
        : kind === "wind"
          ? {
              rainfall: 0,
              erosion: 0,
              thermal: 0,
              windErosion: 0.9,
              evaporation: 0.95,
              settling: 0.25,
            }
          : { rainfall: 0, erosion: 0, thermal: 0.65, windErosion: 0 };
    engine.current?.pause();
    const next = { ...settingsRef.current, ...patch };
    settingsRef.current = next;
    setSettings(next);
    engine.current?.update(next, false);
    setDirty(true);
    setSaved(false);
    setErosionTab(
      kind === "rain" ? "hydraulic" : kind === "wind" ? "wind" : "thermal",
    );
    notify(
      kind === "rain"
        ? "Rain cuts: talus off. Run erosion to develop channels."
        : kind === "wind"
          ? "Wind streaks: dry exposed faces abrade. Wet areas must dry first."
          : "Talus: gravity-driven material movement, without rain or wind cutting.",
    );
  };
  const reset = () =>
    setConfirm({
      title: "Return to the original terrain?",
      description:
        "This removes all sculpting and erosion, and restores the current procedural seed. You can undo this reset.",
      action: () =>
        void perform("Restoring the original landscape…", async () => {
          await engine.current!.reset();
          notify("Original terrain restored.");
        }),
    });
  const fullscreen = async () => {
    try {
      if (document.fullscreenElement) await document.exitFullscreen();
      else await stageRef.current?.requestFullscreen();
    } catch {
      notify(
        "Fullscreen is not available in this preview. You can still orbit and zoom.",
      );
    }
  };
  const restartRenderer = (
    mode?: "webgpu" | "webgl",
    display?: "native" | "safe",
  ) => {
    const restart = () => {
      const url = new URL(location.href);
      if (mode) url.searchParams.set("renderer", mode);
      if (display) url.searchParams.set("display", display);
      if (url.href === location.href) location.reload();
      else location.assign(url.href);
    };
    if (dirty)
      setConfirm({
        title: "Restart the renderer?",
        description: error
          ? "The graphics device is unavailable. Restarting discards unsaved edits; previously saved or exported projects can be reopened afterward."
          : "This reloads the viewport. Save or export your current project first to keep any unsaved sculpting and erosion.",
        action: restart,
      });
    else restart();
  };
  const disabled = !ready || !!busy || !!error;
  return (
    <div className="app-shell">
      <header className="app-header">
        <a
          className="brand"
          href={import.meta.env.BASE_URL}
          aria-label="Frontier Terrain Lab"
        >
          <svg viewBox="0 0 38 32" aria-hidden="true">
            <path d="M3 26 16 5h8L11 26m11 0 7-12 7 12" />
          </svg>
          <span>FRONTIER</span>
        </a>
        <span className="header-divider" />
        <span className="lab-label">
          TERRAIN LAB <span>01</span>
        </span>
        <div className="header-actions">
          <span className="local-label">
            <span className="status-dot" /> Local workspace
          </span>
          <button className="text-button gpu-logs-button" onClick={openLogs}>
            <TerminalSquare size={15} /> <span>GPU logs</span>
          </button>
          <button
            className="text-button guide-button"
            aria-label="Quick guide"
            onClick={() => setGuide(true)}
          >
            <BookOpen size={15} />
            <span>Quick guide</span>
          </button>
          <span className="header-divider" />
          <button
            className={`text-button save-button ${saved ? "saved" : ""}`}
            aria-label={saved ? "Saved" : "Save"}
            onClick={save}
            disabled={disabled}
          >
            {saved ? <Check size={15} /> : <Save size={15} />}
            <span>{saved ? "Saved" : "Save"}</span>
          </button>
          <div className={disabled ? "disabled-area" : ""}>
            <Menu
              className="export-menu"
              disabled={disabled}
              label="Export"
              icon={<ArrowUpRight size={16} />}
            >
              {(close) => (
                <>
                  <div className="menu-label">TAKE YOUR TERRAIN WITH YOU</div>
                  <MenuItem
                    icon={<FileBox size={17} />}
                    description="Mesh + vertex colors · game-engine ready"
                    onClick={() => {
                      close();
                      exportFile("glb");
                    }}
                  >
                    Terrain mesh <kbd>.glb</kbd>
                  </MenuItem>
                  <MenuItem
                    icon={<Box size={17} />}
                    description="Editable 3D volume + all parameters"
                    onClick={() => {
                      close();
                      exportFile("project");
                    }}
                  >
                    SDF project <kbd>.frontier</kbd>
                  </MenuItem>
                  <MenuItem
                    icon={<Image size={17} />}
                    description="Capture the current viewport"
                    onClick={() => {
                      close();
                      exportFile("image");
                    }}
                  >
                    Viewport image <kbd>.png</kbd>
                  </MenuItem>
                </>
              )}
            </Menu>
          </div>
        </div>
      </header>
      <div className="workspace-bar">
        <div className="project-path">
          <button
            className="icon-button mobile-panel-button"
            aria-label="Toggle sculpting panel"
            onClick={() => setLeftOpen(!leftOpen)}
          >
            <PanelLeft size={17} />
          </button>
          <Mountain size={15} />
          <span>Desert studies</span>
          <ChevronRight size={12} />
          <Menu
            className="project-menu"
            align="left"
            label={
              <>
                <strong>{currentPreset.label}</strong>
                {dirty && <i className="unsaved-dot" title="Unsaved changes" />}
              </>
            }
          >
            {(close) => (
              <>
                <div className="menu-label">YOUR LOCAL PROJECT</div>
                <MenuItem
                  icon={<Save size={15} />}
                  onClick={() => {
                    close();
                    save();
                  }}
                >
                  Save current project
                </MenuItem>
                <MenuItem
                  icon={<FolderOpen size={15} />}
                  description={
                    savedAvailable
                      ? "Restore your last local save"
                      : "No local save yet"
                  }
                  onClick={() => {
                    close();
                    void openSaved();
                  }}
                >
                  Open local save
                </MenuItem>
                <MenuItem
                  icon={<Download size={15} />}
                  onClick={() => {
                    close();
                    fileRef.current?.click();
                  }}
                >
                  Import .frontier project
                </MenuItem>
              </>
            )}
          </Menu>
          <span className="project-type">SDF TERRAIN</span>
        </div>
        <div className="history-actions">
          <button
            className="icon-button"
            title="Undo · Ctrl / ⌘ Z"
            aria-label="Undo"
            disabled={!history[0] || disabled}
            onClick={() => undo()}
          >
            <Undo2 size={16} />
          </button>
          <button
            className="icon-button"
            title="Redo · Ctrl / ⌘ Shift Z"
            aria-label="Redo"
            disabled={!history[1] || disabled}
            onClick={() => undo(true)}
          >
            <Redo2 size={16} />
          </button>
          <span className="small-divider" />
          <button
            className={`compare-button ${comparing ? "active" : ""}`}
            disabled={disabled}
            title="Hold to see the original terrain · C"
            onPointerDown={(e) => {
              e.currentTarget.setPointerCapture(e.pointerId);
              compare(true);
            }}
            onPointerUp={() => compare(false)}
            onLostPointerCapture={() => compare(false)}
            onKeyDown={(e) => {
              if (e.key === "Enter" || e.key === " ") {
                e.preventDefault();
                compare(true);
              }
            }}
            onKeyUp={() => compare(false)}
            onBlur={() => compare(false)}
          >
            <SplitSquareHorizontal size={15} />
            Compare<kbd>C</kbd>
          </button>
        </div>
      </div>
      <main className="workspace">
        <aside
          className={`left-sidebar ${leftOpen ? "mobile-open" : ""}`}
          aria-label="Scene and sculpting tools"
        >
          <div className="sidebar-main">
            <SectionHeading detail={<span className="count">03</span>}>
              Scene
            </SectionHeading>
            <div className="scene-tree">
              <div
                className={`scene-row terrain ${inspector === "erosion" ? "selected" : ""}`}
              >
                <button
                  className="scene-label"
                  onClick={() => setInspector("erosion")}
                >
                  <ChevronDown size={12} />
                  <Mountain size={16} />
                  <span>Terrain volume</span>
                  <i>SDF</i>
                </button>
                <button
                  className="visibility-button"
                  title={
                    settings.terrainVisible ? "Hide terrain" : "Show terrain"
                  }
                  aria-label="Toggle terrain visibility"
                  onClick={() =>
                    update("terrainVisible", !settings.terrainVisible)
                  }
                >
                  {settings.terrainVisible ? (
                    <Eye size={13} />
                  ) : (
                    <EyeOff size={13} />
                  )}
                </button>
              </div>
              <button
                className="material-row material-tree-button"
                onClick={() => setInspector("materials")}
                aria-label="Edit surface material"
              >
                <span className="tree-line" />
                <span className="material-dot" />
                {currentMaterial.name}
              </button>
              <div className="scene-row">
                <button
                  className="scene-label"
                  onClick={() => setInspector("environment")}
                >
                  <span className="tree-indent" />
                  <Waves size={16} />
                  <span>Water surface</span>
                </button>
                <button
                  className="visibility-button"
                  aria-label="Toggle water visibility"
                  title={settings.water ? "Hide water" : "Show water"}
                  onClick={() => update("water", !settings.water)}
                >
                  {settings.water ? <Eye size={13} /> : <EyeOff size={13} />}
                </button>
              </div>
              <div className="scene-row">
                <button
                  className="scene-label"
                  onClick={() => setInspector("environment")}
                >
                  <span className="tree-indent" />
                  <Sun size={16} />
                  <span>Environment</span>
                </button>
                <ChevronRight size={12} />
              </div>
            </div>
            <div className="sidebar-section sculpt-section">
              <SectionHeading
                detail={<span className="small-badge">VOLUMETRIC</span>}
              >
                Sculpt
              </SectionHeading>
              <p className="section-description">
                Start with a shape. Make it your own.
              </p>
              <button
                className={`orbit-tool ${tool === "orbit" ? "active" : ""}`}
                onClick={() => chooseTool("orbit")}
                disabled={disabled}
              >
                <MousePointer2 size={15} />
                <span>Navigate</span>
                <kbd>V</kbd>
              </button>
              <div className="sculpt-tools">
                {TOOL_LIST.map((t) => (
                  <button
                    key={t.id}
                    className={`sculpt-tool ${tool === t.id ? "active" : ""}`}
                    onClick={() => chooseTool(t.id)}
                    disabled={disabled}
                    title={`${t.description}${t.key ? ` · ${t.key}` : ""}`}
                    aria-pressed={tool === t.id}
                  >
                    <t.icon size={18} />
                    <span>{t.label}</span>
                    {t.key && <kbd>{t.key}</kbd>}
                  </button>
                ))}
              </div>
              <div className="active-brush-description">
                {TOOL_LIST.find((t) => t.id === tool)?.description ??
                  "Orbit, pan or fly through the volume."}
              </div>
              <fieldset disabled={disabled}>
                {tool === "flatten" && (
                  <div
                    className="brush-plane-controls"
                    role="group"
                    aria-label="Flatten plane"
                  >
                    <button
                      aria-pressed={settings.flattenPlane === "surface"}
                      onClick={() => update("flattenPlane", "surface")}
                    >
                      Surface plane
                    </button>
                    <button
                      aria-pressed={settings.flattenPlane === "horizontal"}
                      onClick={() => update("flattenPlane", "horizontal")}
                    >
                      Horizontal
                    </button>
                    <small>
                      Plane locks at mouse-down. Release to sample a new plane.
                    </small>
                  </div>
                )}
                <Slider
                  label="Brush radius"
                  value={settings.radius}
                  min={1}
                  max={12}
                  step={0.25}
                  onChange={(v) => update("radius", v)}
                  format={(v) => `${v.toFixed(2).replace(/0$/, "")} m`}
                />
                <Slider
                  label="Strength"
                  value={settings.strength}
                  min={0.05}
                  onChange={(v) => update("strength", v)}
                />
                <Slider
                  label="Falloff"
                  value={settings.falloff}
                  min={0.05}
                  onChange={(v) => update("falloff", v)}
                />
                {["crack", "crevice", "boulder"].includes(tool) && (
                  <Slider
                    label={tool === "boulder" ? "Boulder height" : "Cut depth"}
                    value={settings.brushDepth}
                    min={0.1}
                    max={1.5}
                    step={0.05}
                    format={(v) => `${Math.round(v * 100)}% radius`}
                    onChange={(v) => update("brushDepth", v)}
                  />
                )}
                {["crack", "crevice"].includes(tool) && (
                  <Slider
                    label="Cut width"
                    value={settings.brushWidth}
                    min={0.08}
                    max={0.8}
                    step={0.02}
                    format={(v) =>
                      `${(Math.max((96 / stats.size.x) * 0.75, v * settings.radius) * 2 * (tool === "crevice" ? 2.2 : 1)).toFixed(1)} m`
                    }
                    onChange={(v) => update("brushWidth", v)}
                    help="Minimum width is limited by the volume grid; sub-voxel hairline cracks are not geometry"
                  />
                )}
              </fieldset>
              <div className="brush-hint">
                <Info size={13} />
                <span>
                  {tool === "orbit"
                    ? "Choose a brush to sculpt in 3D."
                    : "Left-drag to sculpt. Right-drag to look. WASD to fly."}
                </span>
              </div>
            </div>
            <div className="sidebar-section seed-section">
              <SectionHeading
                detail={
                  <button
                    className="icon-button"
                    aria-label="Randomize terrain seed"
                    title="Generate a new procedural seed"
                    disabled={disabled}
                    onClick={randomize}
                  >
                    <Shuffle size={14} />
                  </button>
                }
              >
                World seed
              </SectionHeading>
              <div className="seed-field">
                <span>#</span>
                <input
                  aria-label="World seed"
                  type="number"
                  min={0}
                  max={999999}
                  value={seedDraft}
                  onChange={(e) => setSeedDraft(e.target.value)}
                  onKeyDown={(e) => {
                    if (e.key === "Enter") applySeed();
                  }}
                  disabled={disabled}
                />
                <button
                  className="icon-button"
                  aria-label="Apply world seed"
                  title="Generate from this seed"
                  onClick={applySeed}
                  disabled={disabled}
                >
                  <ArrowRight size={15} />
                </button>
              </div>
              <p className="seed-note">Same seed. Same starting landscape.</p>
            </div>
          </div>
          <div className="procedural-note">
            <svg viewBox="0 0 220 80" aria-hidden="true">
              <path d="M-20 70Q20 5 75 53T180 36T250 20M-20 58Q20-7 75 41T180 24T250 8M-20 82Q20 17 75 65T180 48T250 32M-20 94Q20 29 75 77T180 60T250 44M-20 46Q20-19 75 29T180 12T250-4" />
            </svg>
            <Box size={18} />
            <h3>Yours, down to the rock.</h3>
            <p>
              Real geometry. Procedural materials.
              <br />
              Not a single borrowed asset.
            </p>
            <span>
              SIGNED DISTANCE FIELD <ArrowUpRight size={11} />
            </span>
          </div>
        </aside>
        <section className="center-stage" aria-label="Terrain workspace">
          <div
            className={`viewport ${stats.navigationMode === "fly" ? "is-flying" : ""}`}
            ref={stageRef}
          >
            <div className="viewport-canvas" ref={viewportRef} />
            <div className="viewport-toolbar">
              <div className="viewport-view-controls">
                <Menu
                  className="viewport-menu"
                  align="left"
                  icon={<Box size={14} />}
                  label={
                    stats.navigationMode === "fly"
                      ? "Fly camera"
                      : "Perspective"
                  }
                >
                  {(close) => (
                    <>
                      <MenuItem
                        icon={<Box size={14} />}
                        onClick={() => {
                          close();
                          engine.current?.resetCamera();
                        }}
                      >
                        Perspective view
                      </MenuItem>
                      <MenuItem
                        icon={<Navigation size={14} />}
                        active={stats.navigationMode === "fly"}
                        description="RMB look · WASD · Q/E down/up"
                        onClick={() => {
                          close();
                          engine.current?.setNavigation("fly");
                        }}
                      >
                        Fly camera
                      </MenuItem>
                      <MenuItem
                        icon={<Grid2X2 size={14} />}
                        onClick={() => {
                          close();
                          engine.current?.resetCamera(true);
                        }}
                      >
                        Top-down view
                      </MenuItem>
                      <div className="camera-settings">
                        <Slider
                          label="Fly speed"
                          value={stats.cameraSpeed ?? 10}
                          min={0.25}
                          max={80}
                          step={0.25}
                          format={(v) => `${v.toFixed(v < 1 ? 2 : 1)} m/s`}
                          onChange={(v) => engine.current?.setCameraSpeed(v)}
                        />
                        <p>
                          Hold Shift for 3× speed. RMB + wheel adjusts speed.
                        </p>
                      </div>
                    </>
                  )}
                </Menu>
                <span className="viewport-divider" />
                <Menu
                  className="viewport-menu shading-menu"
                  align="left"
                  label={
                    settings.view === "lit"
                      ? "Lit"
                      : settings.view === "clay"
                        ? "Clay"
                        : "Flow"
                  }
                  icon={
                    settings.view === "flow" ? (
                      <Droplets size={13} />
                    ) : (
                      <Sun size={13} />
                    )
                  }
                >
                  {(close) => (
                    <>
                      {(["lit", "clay", "flow"] as const).map((v) => (
                        <MenuItem
                          key={v}
                          active={settings.view === v}
                          onClick={() => {
                            close();
                            update("view", v);
                          }}
                        >
                          {v === "lit"
                            ? "Lit · sandstone"
                            : v === "clay"
                              ? "Clay · inspect geometry"
                              : "Flow · erosion & sediment"}
                        </MenuItem>
                      ))}
                    </>
                  )}
                </Menu>
              </div>
              <div className="viewport-actions">
                <Menu
                  className="viewport-menu renderer-menu"
                  ariaLabel="Renderer options"
                  label={
                    <span className="renderer-mode-label">
                      {ready ? stats.backend : "Renderer"}
                    </span>
                  }
                  icon={<Cpu size={14} />}
                >
                  {(close) => (
                    <>
                      <div className="menu-label">DISPLAY & RECOVERY</div>
                      <MenuItem
                        icon={<RotateCcw size={15} />}
                        description="Rebuild a blank or disconnected viewport"
                        onClick={() => {
                          close();
                          restartRenderer();
                        }}
                      >
                        Restart renderer
                      </MenuItem>
                      <MenuItem
                        icon={<Cpu size={15} />}
                        active={
                          ready &&
                          stats.backend === "WebGPU" &&
                          stats.displayMode !== "safe"
                        }
                        description="Fast native canvas · GPU terrain and erosion"
                        onClick={() => {
                          close();
                          restartRenderer("webgpu", "native");
                        }}
                      >
                        Use WebGPU (native display)
                      </MenuItem>
                      <MenuItem
                        icon={<Image size={15} />}
                        active={
                          ready &&
                          stats.backend === "WebGPU" &&
                          stats.displayMode === "safe"
                        }
                        description="GPU terrain + erosion · CPU bitmap display, with extra transfer cost"
                        onClick={() => {
                          close();
                          restartRenderer("webgpu", "safe");
                        }}
                      >
                        Use WebGPU safe display
                      </MenuItem>
                      <MenuItem
                        icon={<Box size={15} />}
                        active={ready && stats.backend === "WebGL2"}
                        description="WebGL2 + CPU erosion · still a 3D volume"
                        onClick={() => {
                          close();
                          restartRenderer("webgl");
                        }}
                      >
                        Use compatibility renderer
                      </MenuItem>
                      <div className="menu-label">RENDER RESOLUTION</div>
                      <MenuItem
                        active={stats.quality !== "native"}
                        description="Stable scaling with GPU backpressure"
                        onClick={() => {
                          close();
                          engine.current?.setQuality("adaptive");
                        }}
                      >
                        Adaptive resolution
                      </MenuItem>
                      <MenuItem
                        active={stats.quality === "native"}
                        description="Full viewport resolution for close-up detail"
                        onClick={() => {
                          close();
                          engine.current?.setQuality("native");
                        }}
                      >
                        Native resolution
                      </MenuItem>
                      <a
                        role="menuitem"
                        className="menu-item"
                        href={location.href}
                        target="_blank"
                        rel="noopener noreferrer"
                        onClick={close}
                      >
                        <ArrowUpRight size={15} />
                        <span>
                          Open preview in a new tab
                          <small>Useful if embedded graphics are blocked</small>
                        </span>
                      </a>
                    </>
                  )}
                </Menu>
                <span className="viewport-divider" />
                <button
                  className={`viewport-icon ${settings.grid ? "active" : ""}`}
                  aria-label="Toggle ground grid"
                  title="Ground grid · G"
                  onClick={() => update("grid", !settings.grid)}
                >
                  <Grid2X2 size={15} />
                </button>
                <button
                  className="viewport-icon"
                  title="Frame terrain · F"
                  aria-label="Frame terrain"
                  onClick={() => engine.current?.resetCamera()}
                >
                  <Maximize size={15} />
                </button>
                <span className="viewport-divider" />
                <button
                  className="viewport-icon"
                  aria-label="Toggle fullscreen"
                  title="Fullscreen viewport"
                  onClick={() => void fullscreen()}
                >
                  {full ? <Minimize size={15} /> : <Square size={14} />}
                </button>
              </div>
            </div>
            <div className="scene-caption">
              <div>
                <span className="caption-line" /> PROCEDURAL LANDSCAPE
              </div>
              <h1>{currentPreset.name}</h1>
              <p>
                {currentMaterial.name} surface <span>·</span> 96 × 96 m
              </p>
            </div>
            {comparing && (
              <div className="compare-banner">
                <SplitSquareHorizontal size={14} />
                Original terrain<span>Release to return</span>
              </div>
            )}
            {settings.view === "flow" && (
              <div className="flow-legend">
                <strong>VOLUME DIAGNOSTICS</strong>
                <span>
                  <i className="legend-flow" />
                  Runoff
                </span>
                <span>
                  <i className="legend-eroded" />
                  Erosion
                </span>
                <span>
                  <i className="legend-deposit" />
                  Deposition
                </span>
              </div>
            )}
            <div className="viewport-bottom">
              <div className="viewport-status">
                <span
                  className={`status-dot ${stats.running ? "running" : ""}`}
                />
                <span>
                  {comparing
                    ? "Original volume"
                    : stats.running
                      ? "Erosion in progress"
                      : stats.navigationMode === "fly"
                        ? "Fly camera"
                        : "Live viewport"}
                </span>
                <i />
                {stats.steps.toLocaleString()}
                <span className="iteration-label">iterations</span>
              </div>
              <div
                className="scale-indicator"
                title="10 meters at the camera focus plane"
              >
                <span>{stats.scaleMeters ?? 10} m</span>
                <i style={{ width: stats.scalePixels || 48 }} />
              </div>
              <div className="axis-gizmo" aria-hidden="true">
                <span className="axis-y">Y</span>
                <span className="axis-z">Z</span>
                <span className="axis-x">X</span>
                <svg viewBox="0 0 60 60">
                  <path d="M30 32V8" stroke="#abc299" />
                  <path d="m30 32 23 11" stroke="#d69783" />
                  <path d="m30 32-23 11" stroke="#91aeba" />
                  <circle cx="30" cy="32" r="3" fill="#c8cdc6" />
                </svg>
              </div>
            </div>
            {(!ready || busy || error) && (
              <div className={`viewport-loading ${error ? "has-error" : ""}`}>
                {error ? (
                  <>
                    <Info size={27} />
                    <h3>The terrain renderer could not display a frame.</h3>
                    <p>{error}</p>
                    <div className="renderer-recovery-actions">
                      <button className="secondary-button" onClick={openLogs}>
                        <TerminalSquare size={15} /> Show GPU logs
                      </button>
                      <button
                        className="primary-button"
                        onClick={() => restartRenderer("webgl")}
                      >
                        Try compatibility renderer
                      </button>
                      <button
                        className="secondary-button"
                        onClick={() => restartRenderer("webgpu", "safe")}
                      >
                        Try safe WebGPU display
                      </button>
                      <button
                        className="secondary-button"
                        onClick={() => restartRenderer()}
                      >
                        Restart renderer
                      </button>
                    </div>
                  </>
                ) : (
                  <>
                    <div className="loading-logo">
                      <Mountain size={29} />
                      <span />
                    </div>
                    <strong>{busy || "A landscape is taking shape."}</strong>
                    <p>
                      {busy
                        ? "Working with your three-dimensional volume"
                        : startupMessage}
                    </p>
                    <LoaderCircle className="spin" size={17} />
                    {!ready && !busy && (
                      <button
                        className="startup-recovery"
                        onClick={() => restartRenderer("webgl")}
                      >
                        Taking too long? Try compatibility rendering{" "}
                        <ArrowUpRight size={12} />
                      </button>
                    )}
                  </>
                )}
              </div>
            )}
          </div>
          <div className="viewport-help">
            <span title="Left-drag or Alt-drag orbits. Right-drag looks around from your position. Click the viewport to focus keyboard input.">
              <Navigation size={12} />
              <b>RMB + WASD</b> fly<span className="dot-separator">·</span>
              <b>Q / E</b> down / up<span className="dot-separator">·</span>
              <b>Shift</b> boost
            </span>
            <button onClick={() => setGuide(true)} title="Keyboard shortcuts">
              <Keyboard size={14} />
              <span>Shortcuts</span>
            </button>
          </div>
          <div className="presets-section">
            <div className="presets-heading">
              <h2>STARTING POINTS</h2>
              <span>A seed. Endless possibilities.</span>
            </div>
            <div className="preset-list">
              {PRESETS.map((p, i) => (
                <button
                  key={p.id}
                  className={`preset-card ${settings.preset === p.id ? "active" : ""}`}
                  onClick={() => {
                    if (p.id !== settings.preset) regenerate(p.id);
                  }}
                  disabled={disabled}
                  aria-pressed={settings.preset === p.id}
                >
                  <div className={`preset-image preset-${p.id}`}>
                    <img
                      src={`${import.meta.env.BASE_URL}presets/${p.id}.webp`}
                      alt=""
                      onError={(e) => {
                        e.currentTarget.style.display = "none";
                      }}
                    />
                    <svg viewBox="0 0 140 85" aria-hidden="true">
                      <path
                        d={
                          i === 1
                            ? "M0 76 14 35 26 25 38 37 43 13 79 11 101 42 116 32 140 67V85H0ZM58 60 81 63 75 31 63 31Z"
                            : i === 2
                              ? "M0 67 22 32 36 57 50 18 70 42 84 25 115 55 130 35 140 69V85H0Z"
                              : "M0 26 19 21 37 29 52 52 75 73 95 62 103 29 125 23 140 35V85H0Z"
                        }
                      />
                      <path d="M0 68q30-10 52 3t43-2 45 6v10H0Z" />
                    </svg>
                    {settings.preset === p.id && (
                      <span className="preset-check">
                        <Check size={10} />
                      </span>
                    )}
                  </div>
                  <div className="preset-details">
                    <h3>{p.name}</h3>
                    <p>
                      {i === 0
                        ? "River-cut · layered rock"
                        : i === 1
                          ? "Wind-worn · natural bridges"
                          : "Caprock · fractured fins"}
                    </p>
                    <span>
                      {i === 0 ? "01" : i === 1 ? "02" : "03"}
                      <ArrowUpRight size={12} />
                    </span>
                  </div>
                </button>
              ))}
            </div>
          </div>
        </section>
        <aside
          className={`inspector ${inspector === "materials" ? "materials-panel" : ""}`}
          aria-label="Terrain simulation inspector"
        >
          <div
            className="inspector-tabs"
            role="tablist"
            aria-label="Inspector section"
          >
            <button
              role="tab"
              aria-selected={inspector === "erosion"}
              className={inspector === "erosion" ? "active" : ""}
              onClick={() => setInspector("erosion")}
            >
              <SlidersHorizontal size={14} />
              Erosion
            </button>
            <button
              role="tab"
              aria-selected={inspector === "environment"}
              className={inspector === "environment" ? "active" : ""}
              onClick={() => setInspector("environment")}
            >
              <CloudSun size={15} />
              Environment
            </button>
            <button
              role="tab"
              aria-selected={inspector === "materials"}
              className={inspector === "materials" ? "active" : ""}
              onClick={() => setInspector("materials")}
            >
              <Gem size={14} />
              Materials
            </button>
          </div>
          <div className="inspector-scroll">
            <fieldset disabled={disabled}>
              {inspector === "erosion" ? (
                <>
                  <div className="inspector-intro">
                    <span className="eyebrow">LET NATURE TAKE OVER</span>
                    <h2>Time, water & stone.</h2>
                    <p>Wear away the ordinary.</p>
                  </div>
                  <div
                    className="weather-profiles"
                    aria-label="Weathering parameter presets"
                  >
                    <button onClick={() => weatherProfile("rain")}>
                      <Droplets size={12} />
                      Rain cuts
                    </button>
                    <button onClick={() => weatherProfile("wind")}>
                      <Wind size={12} />
                      Wind streaks
                    </button>
                    <button onClick={() => weatherProfile("talus")}>
                      <Mountain size={12} />
                      Talus
                    </button>
                  </div>
                  <div
                    className="method-tabs"
                    role="tablist"
                    aria-label="Erosion parameters"
                  >
                    <button
                      role="tab"
                      aria-selected={erosionTab === "hydraulic"}
                      className={erosionTab === "hydraulic" ? "active" : ""}
                      onClick={() => setErosionTab("hydraulic")}
                    >
                      <Droplets size={14} />
                      Hydraulic
                    </button>
                    <button
                      role="tab"
                      aria-selected={erosionTab === "thermal"}
                      className={erosionTab === "thermal" ? "active" : ""}
                      onClick={() => setErosionTab("thermal")}
                    >
                      <Mountain size={14} />
                      Thermal
                    </button>
                    <button
                      role="tab"
                      aria-selected={erosionTab === "wind"}
                      className={erosionTab === "wind" ? "active" : ""}
                      onClick={() => setErosionTab("wind")}
                    >
                      <Wind size={13} />
                      Wind
                    </button>
                  </div>
                  <div className="erosion-parameters">
                    {erosionTab === "hydraulic" ? (
                      <>
                        <div className="parameter-caption">
                          <Droplets size={12} />
                          <span>Water cuts. Sediment settles.</span>
                        </div>
                        <Slider
                          label="Rainfall"
                          value={settings.rainfall}
                          onChange={(v) => update("rainfall", v, true)}
                          help="Rain enters exposed surfaces; every voxel above is checked for cave roofs"
                        />
                        <Slider
                          label="Erosion strength"
                          value={settings.erosion}
                          onChange={(v) => update("erosion", v, true)}
                          help="Rate of detachment above the local shear/cohesion threshold"
                        />
                        <Slider
                          label="Channel focus"
                          value={settings.channeling}
                          onChange={(v) => update("channeling", v, true)}
                          help="Focus runoff into connected low-potential paths; cuts actual channels rather than uniformly shrinking rock"
                        />
                        <Slider
                          label="Sediment capacity"
                          value={settings.sediment}
                          onChange={(v) => update("sediment", v, true)}
                          help="How much eroded material the runoff can carry"
                        />
                        <Slider
                          label="Evaporation"
                          value={settings.evaporation}
                          onChange={(v) => update("evaporation", v, true)}
                          help="Water removed from the volume each iteration"
                        />
                        <details className="process-details">
                          <summary>Scour & sediment controls</summary>
                          <Slider
                            label="Rock cohesion"
                            value={settings.cohesion}
                            onChange={(v) => update("cohesion", v, true)}
                            help="Flow must overcome this critical shear threshold before detaching rock"
                          />
                          <Slider
                            label="Settling rate"
                            value={settings.settling}
                            onChange={(v) => update("settling", v, true)}
                            help="Particles settle down through water and deposit when carrying capacity falls"
                          />
                          <p>
                            Moving runoff cuts; overloaded water deposits. The
                            local rock/sediment exchange is budgeted.
                          </p>
                        </details>
                      </>
                    ) : erosionTab === "thermal" ? (
                      <>
                        <div className="parameter-caption">
                          <Mountain size={12} />
                          <span>Loose material moves downhill.</span>
                        </div>
                        <Slider
                          label="Talus weathering"
                          value={settings.thermal}
                          onChange={(v) => update("thermal", v, true)}
                          help="Gravity-driven material transfer on slopes above the repose angle; not global smoothing"
                        />
                        <Slider
                          label="Layer resistance"
                          value={settings.resistance}
                          onChange={(v) => update("resistance", v, true)}
                          help="Contrasting hardness of sedimentary strata"
                        />
                        <Slider
                          label="Talus angle"
                          value={settings.talusAngle}
                          min={20}
                          max={55}
                          step={1}
                          format={(v) => `${v}°`}
                          onChange={(v) => update("talusAngle", v, true)}
                          help="Slopes below this angle do not shed talus; cohesion protects intact rock"
                        />
                        <div className="parameter-note">
                          <Layers3 size={17} />
                          <p>
                            <strong>Rock remembers its layers.</strong>Harder
                            strata resist scouring. The same bands control the
                            visible sandstone and its mechanical resistance.
                          </p>
                        </div>
                        <p className="thermal-note">
                          Talus is separate from incision. Turn it down for
                          sharp rain cuts or wind flutes.
                        </p>
                      </>
                    ) : (
                      <>
                        <div className="parameter-caption">
                          <Wind size={13} />
                          <span>Abrasive grains cut exposed, dry faces.</span>
                        </div>
                        <Slider
                          label="Wind abrasion"
                          value={settings.windErosion}
                          onChange={(v) => update("windErosion", v, true)}
                          help="Actual removal of SDF rock, not water animation or a texture"
                        />
                        <Slider
                          label="Wind direction"
                          value={settings.windDirection}
                          min={0}
                          max={360}
                          step={5}
                          format={(v) => `${v}°`}
                          onChange={(v) => update("windDirection", v, true)}
                        />
                        <p className="thermal-note">
                          Wind-facing surfaces are checked for shelter. Wet and
                          underwater rock resists abrasion; eroded dust is
                          transported and can settle.
                        </p>
                      </>
                    )}
                  </div>
                  <div className="live-preview-row">
                    <div>
                      <span>
                        <span className="status-dot" />
                        Live preview
                      </span>
                      <p>See changes as you adjust.</p>
                    </div>
                    <Toggle
                      label="Live erosion preview"
                      checked={settings.autoPreview}
                      onChange={(v) => update("autoPreview", v)}
                    />
                  </div>
                  <div className="water-section">
                    <SectionHeading
                      detail={
                        <Toggle
                          label="Water surface"
                          checked={settings.water}
                          onChange={(v) => update("water", v)}
                          small
                        />
                      }
                    >
                      <Waves size={14} />
                      Water & wind
                    </SectionHeading>
                    <div className={!settings.water ? "muted-controls" : ""}>
                      <Slider
                        label="Water level"
                        value={settings.waterLevel}
                        min={-1}
                        max={8}
                        step={0.05}
                        format={(v) => `${v.toFixed(2)} m`}
                        onChange={(v) => update("waterLevel", v)}
                        disabled={!settings.water}
                      />
                      <Slider
                        label="Wind strength"
                        value={settings.wind}
                        format={percent}
                        onChange={(v) => update("wind", v)}
                        disabled={!settings.water}
                      />
                      <Slider
                        label="Water clarity"
                        value={settings.waterClarity}
                        onChange={(v) => update("waterClarity", v)}
                        disabled={!settings.water}
                        help="Optical clarity; suspended simulation sediment also tints the water"
                      />
                      <Slider
                        label="Absorption distance"
                        value={settings.waterAbsorption}
                        min={0.5}
                        max={30}
                        step={0.5}
                        format={(v) => `${v.toFixed(1)} m`}
                        onChange={(v) => update("waterAbsorption", v)}
                        disabled={!settings.water}
                        help="Longer distances transmit more light. Thickness is traced, not inferred from a shallow/deep tint."
                      />
                      <Slider
                        label="Shore foam"
                        value={settings.waterFoam}
                        onChange={(v) => update("waterFoam", v)}
                        disabled={!settings.water}
                      />
                    </div>
                    <p className="water-note">
                      <Wind size={12} />
                      Wind waves · shoreline lapping · sediment-tinted depth.
                    </p>
                  </div>
                </>
              ) : inspector === "materials" ? (
                <MaterialEditor settings={settings} onChange={changeMaterial} />
              ) : (
                <>
                  <div className="inspector-intro">
                    <span className="eyebrow">THE FINISHING TOUCHES</span>
                    <h2>Find your light.</h2>
                    <p>A little atmosphere goes a long way.</p>
                  </div>
                  <div className="environment-section">
                    <SectionHeading>
                      <Sun size={14} />
                      Sun & atmosphere
                    </SectionHeading>
                    <Slider
                      label="Sun elevation"
                      value={settings.sunAngle}
                      min={10}
                      max={85}
                      step={1}
                      format={(v) => `${v}°`}
                      onChange={(v) => update("sunAngle", v)}
                    />
                    <Slider
                      label="Exposure"
                      value={settings.exposure}
                      min={0.5}
                      max={1.7}
                      step={0.01}
                      format={(v) => `${v.toFixed(2)}×`}
                      onChange={(v) => update("exposure", v)}
                    />
                    <div className="lighting-presets">
                      <button
                        onClick={() => {
                          update("sunAngle", 22);
                          update("exposure", 1.12);
                        }}
                      >
                        <Sun size={14} />
                        Golden hour
                      </button>
                      <button
                        onClick={() => {
                          update("sunAngle", 66);
                          update("exposure", 0.96);
                        }}
                      >
                        <Sun size={14} />
                        High noon
                      </button>
                    </div>
                  </div>
                  <div className="water-section">
                    <SectionHeading
                      detail={
                        <Toggle
                          label="Water surface"
                          checked={settings.water}
                          onChange={(v) => update("water", v)}
                          small
                        />
                      }
                    >
                      <Waves size={14} />
                      Water & wind
                    </SectionHeading>
                    <Slider
                      label="Water level"
                      value={settings.waterLevel}
                      min={-1}
                      max={8}
                      step={0.05}
                      format={(v) => `${v.toFixed(2)} m`}
                      onChange={(v) => update("waterLevel", v)}
                      disabled={!settings.water}
                    />
                    <Slider
                      label="Wind strength"
                      value={settings.wind}
                      onChange={(v) => update("wind", v)}
                      disabled={!settings.water}
                    />
                    <Slider
                      label="Water clarity"
                      value={settings.waterClarity}
                      onChange={(v) => update("waterClarity", v)}
                      disabled={!settings.water}
                    />
                    <Slider
                      label="Absorption distance"
                      value={settings.waterAbsorption}
                      min={0.5}
                      max={30}
                      step={0.5}
                      format={(v) => `${v.toFixed(1)} m`}
                      onChange={(v) => update("waterAbsorption", v)}
                      disabled={!settings.water}
                    />
                    <Slider
                      label="Shore foam"
                      value={settings.waterFoam}
                      onChange={(v) => update("waterFoam", v)}
                      disabled={!settings.water}
                    />

                    <div className="parameter-note">
                      <Waves size={17} />
                      <p>
                        <strong>Water meets stone.</strong>Displaced wind waves,
                        entry-tested refraction, thickness-dependent absorption
                        and shadowed foam. Water below the surface is handled as
                        a medium, not a colored edge strip.
                      </p>
                    </div>
                  </div>
                  <div className="environment-section">
                    <SectionHeading>
                      <Grid2X2 size={14} />
                      Workspace
                    </SectionHeading>
                    <div className="simple-toggle-row">
                      <span>Ground grid</span>
                      <Toggle
                        label="Ground grid"
                        checked={settings.grid}
                        onChange={(v) => update("grid", v)}
                      />
                    </div>
                    <button
                      className="material-edit-link"
                      onClick={() => setInspector("materials")}
                    >
                      <span
                        className={`material-chip chip-${settings.material}`}
                      />
                      <span>
                        <strong>{currentMaterial.name}</strong>
                        <small>Edit surface material</small>
                      </span>
                      <ArrowRight size={15} />
                    </button>
                  </div>
                </>
              )}
            </fieldset>
          </div>
          <div className="simulation-footer">
            <div className="simulation-topline">
              <span>
                <span
                  className={`status-dot ${stats.running ? "running" : ""}`}
                />
                {stats.running ? "SIMULATING" : "SIMULATION READY"}
              </span>
              <div
                className="simulation-speed"
                aria-label="Iterations per frame"
              >
                {[1, 2, 4].map((s) => (
                  <button
                    key={s}
                    className={settings.speed === s ? "active" : ""}
                    title={`${s} iterations per simulation tick${stats.backend === "WebGL2" ? " (CPU fallback limited to 1)" : ""}`}
                    onClick={() => update("speed", s)}
                    disabled={
                      disabled || (stats.backend === "WebGL2" && s !== 1)
                    }
                    aria-pressed={settings.speed === s}
                  >
                    {s}×
                  </button>
                ))}
              </div>
            </div>
            <button
              className="weather-batch"
              disabled={disabled || stats.running}
              onClick={() => void engine.current?.weatherBatch(80)}
            >
              Weather 80 iterations <ArrowRight size={12} />
            </button>
            <div className="simulation-buttons">
              <button
                className={`run-button ${stats.running ? "is-running" : ""}`}
                onClick={toggleRun}
                disabled={disabled}
              >
                {stats.running ? (
                  <Pause size={15} fill="currentColor" />
                ) : (
                  <Play size={15} fill="currentColor" />
                )}
                {stats.running ? "Pause erosion" : "Run erosion"}
                <kbd>␣</kbd>
              </button>
              <button
                className="step-button"
                aria-label="Simulate one iteration"
                title="Advance one iteration"
                disabled={disabled}
                onClick={() =>
                  void engine.current
                    ?.singleStep()
                    .catch((e) => notify(String(e), true))
                }
              >
                <SkipForward size={15} />
              </button>
              <button
                className="step-button"
                aria-label="Reset terrain"
                title="Restore the original procedural terrain"
                disabled={disabled}
                onClick={reset}
              >
                <RotateCcw size={15} />
              </button>
            </div>
            <p>
              <Box size={12} />
              Changes the geometry. Not just the surface.
            </p>
          </div>
        </aside>
      </main>
      <footer className="status-bar">
        <div>
          <span
            className={`gpu-status ${stats.backend === "WebGL2" ? "fallback" : ""}`}
          >
            <span className="status-dot" />
            {error
              ? "Graphics interrupted"
              : ready
                ? stats.backend
                : "Connecting GPU"}
            {ready && !error && (
              <span>
                {stats.backend === "WebGPU"
                  ? stats.displayMode === "safe"
                    ? "COMPUTE · SAFE DISPLAY"
                    : "COMPUTE"
                  : "CPU FALLBACK"}
              </span>
            )}
          </span>
          <span className="footer-divider" />
          <span className="volume-status">
            <Box size={12} />
            {stats.size.x} × {stats.size.y} × {stats.size.z}
            <span>voxels</span>
          </span>
          <span className="footer-divider" />
          <span className="fps">
            <i />
            {Math.round(stats.fps)} <span>FPS</span>
          </span>
        </div>
        <div>
          <span className="bounded-label">
            <span className="status-dot" />
            Bounded world
          </span>
          <span className="footer-divider" />
          <span className="footer-version">
            FRONTIER LAB <span>v{version}</span>
          </span>
          <button
            aria-label="About this simulation"
            title="Guide and simulation limitations"
            onClick={() => setGuide(true)}
          >
            <Info size={13} />
          </button>
        </div>
      </footer>
      {toast && (
        <div
          className={`toast ${toast.error ? "error" : ""}`}
          role={toast.error ? "alert" : "status"}
        >
          {toast.error ? <Info size={17} /> : <Check size={17} />}
          <span>{toast.message}</span>
          <button
            aria-label="Dismiss notification"
            onClick={() => setToast(null)}
          >
            <X size={14} />
          </button>
        </div>
      )}
      {logsOpen && (
        <DiagnosticsPanel
          engineRef={engine}
          onClose={() => setLogsOpen(false)}
        />
      )}
      {guide && <Guide onClose={() => setGuide(false)} />}
      {confirm && (
        <Dialog
          title={confirm.title}
          onClose={() => setConfirm(null)}
          className="confirm-dialog"
        >
          <div className="confirm-body">
            <p>{confirm.description}</p>
          </div>
          <div className="dialog-footer">
            <button
              className="secondary-button"
              onClick={() => setConfirm(null)}
            >
              Keep working
            </button>
            <button
              className="primary-button"
              onClick={() => {
                const action = confirm.action;
                setConfirm(null);
                action();
              }}
            >
              Continue
              <ArrowRight size={14} />
            </button>
          </div>
        </Dialog>
      )}
      <input
        type="file"
        ref={fileRef}
        accept=".frontier"
        hidden
        aria-label="Import Frontier project"
        onChange={(e) => {
          const f = e.target.files?.[0];
          if (f) void importFile(f);
          e.target.value = "";
        }}
      />
    </div>
  );
}
