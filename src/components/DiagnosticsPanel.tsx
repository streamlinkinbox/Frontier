import {
  useCallback,
  useEffect,
  useRef,
  useState,
  useSyncExternalStore,
} from "react";
import {
  Check,
  Copy,
  Download,
  LoaderCircle,
  Pause,
  Play,
  ScanLine,
} from "lucide-react";
import { Dialog } from "./Guide";
import { diagnostics } from "../diagnostics";
import type { TerrainEngine } from "../engine/TerrainEngine";

export function DiagnosticsPanel({
  engineRef,
  onClose,
}: {
  engineRef: { current: TerrainEngine | null };
  onClose: () => void;
}) {
  const revision = useSyncExternalStore(
    diagnostics.subscribe,
    diagnostics.getRevision,
  );
  const textRef = useRef<HTMLTextAreaElement>(null);
  const [live, setLive] = useState(true);
  const [checking, setChecking] = useState(false);
  const [copied, setCopied] = useState(false);
  const [message, setMessage] = useState("");
  const [report, setReport] = useState("");
  const mounted = useRef(true);
  const collect = useCallback(() => {
    try {
      return diagnostics.report(
        engineRef.current?.getDiagnostics() ?? { engine: "not yet created" },
      );
    } catch (error) {
      return diagnostics.report({ snapshotError: error });
    }
  }, [engineRef]);
  useEffect(() => {
    mounted.current = true;
    setReport(collect());
    return () => {
      mounted.current = false;
    };
  }, [collect]);
  useEffect(() => {
    if (live) setReport(collect());
  }, [revision, live, collect]);
  useEffect(() => {
    if (!live) return;
    const timer = window.setInterval(() => setReport(collect()), 2000);
    return () => window.clearInterval(timer);
  }, [live, collect]);

  const copy = async () => {
    const text = collect();
    setReport(text);
    setLive(false); // Keep this exact report selected if the iframe blocks copy.
    setCopied(false);
    try {
      if (!navigator.clipboard?.writeText)
        throw new Error("Clipboard API unavailable");
      await navigator.clipboard.writeText(text);
      if (!mounted.current) return;
      setCopied(true);
      setMessage("Copied GPU logs. Paste them into the chat.");
    } catch {
      if (!mounted.current) return;
      const area = textRef.current;
      if (area) {
        area.value = text;
        area.focus();
        area.select();
      }
      let success = false;
      try {
        success = document.execCommand("copy");
      } catch {
        /* manual selection remains available */
      }
      setCopied(success);
      setMessage(
        success
          ? "Copied GPU logs. Paste them into the chat."
          : "Clipboard access is blocked in this preview. The report is selected: press Ctrl+C (⌘C on Mac), or Download logs.",
      );
    }
  };
  const download = () => {
    const text = collect();
    const url = URL.createObjectURL(
      new Blob([text], { type: "text/plain;charset=utf-8" }),
    );
    const anchor = document.createElement("a");
    anchor.href = url;
    anchor.download = "frontier-gpu-diagnostics.txt";
    anchor.click();
    window.setTimeout(() => URL.revokeObjectURL(url), 1000);
    setMessage(
      "Diagnostic report downloaded. You can attach the text file in the chat.",
    );
  };
  const check = async () => {
    setChecking(true);
    setCopied(false);
    setLive(true);
    setMessage(
      "Checking the GPU output and SDF data. Rendering pauses briefly; your terrain is not edited.",
    );
    try {
      if (engineRef.current) await engineRef.current.checkViewport();
      else
        diagnostics.log(
          "Check",
          "Engine not yet created; copy the startup logs instead.",
          undefined,
          "warn",
        );
    } catch (error) {
      diagnostics.log("Check", "Diagnostic check failed", error, "error");
    } finally {
      if (mounted.current) {
        setChecking(false);
        // The log subscription refreshes live reports. Do not replace a
        // frozen/selected report if the user copied while a check was pending.
        setMessage(
          "Check finished. Click Copy GPU logs and paste the report into the chat, even if the viewport is still blank.",
        );
      }
    }
  };

  return (
    <Dialog
      title="GPU diagnostics"
      onClose={onClose}
      className="diagnostics-dialog"
    >
      <div className="diagnostics-body">
        <p className="diagnostics-intro">
          Blank viewport? Click <strong>Check viewport</strong>, then{" "}
          <strong>Copy GPU logs</strong>. This records this device’s adapter,
          shader errors, canvas sizing, frame progress and pixel checks.
        </p>
        <div className="diagnostics-actions">
          <button
            className="secondary-button"
            onClick={() => void check()}
            disabled={checking}
          >
            {checking ? (
              <LoaderCircle size={15} className="spin" />
            ) : (
              <ScanLine size={15} />
            )}
            {checking ? "Checking viewport…" : "Check viewport"}
          </button>
          <button className="primary-button" onClick={() => void copy()}>
            {copied ? <Check size={15} /> : <Copy size={15} />}
            {copied ? "Copied GPU logs" : "Copy GPU logs"}
          </button>
          <button className="secondary-button" onClick={download}>
            <Download size={15} /> Download logs
          </button>
        </div>
        <div className="diagnostics-caption">
          <label htmlFor="gpu-diagnostic-report">THIS TAB’S GPU REPORT</label>
          <button onClick={() => setLive(!live)} aria-pressed={live}>
            {live ? <Pause size={12} /> : <Play size={12} />}
            {live ? "Pause live updates" : "Resume live updates"}
          </button>
        </div>
        <textarea
          id="gpu-diagnostic-report"
          aria-label="GPU diagnostic report"
          className="diagnostics-report"
          ref={textRef}
          readOnly
          spellCheck={false}
          wrap="off"
          value={report}
          onPointerDown={() => setLive(false)}
          onKeyDown={(event) => event.stopPropagation()}
        />
        <p className="diagnostics-feedback" role="status">
          {message ||
            "Logs stay in this tab. Nothing is uploaded automatically."}
        </p>
        <p className="diagnostics-note">
          A successful GPU readback or FPS counter does <strong>not</strong>{" "}
          prove the browser displayed the canvas. Tell us whether the main
          viewport is still blank when you paste the logs. No raw terrain,
          cookies or URL query strings are included.
        </p>
      </div>
    </Dialog>
  );
}
