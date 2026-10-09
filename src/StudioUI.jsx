import React from "react";
import { Box, Layers3, X } from "lucide-react";

export const studioWorkspaces = [
  { id: "material", label: "Material" },
  { id: "pattern", label: "Pattern" },
  { id: "texture", label: "Texture" },
  { id: "stamp", label: "Stamp" },
  { id: "baking", label: "Baking" },
];
export function normalizeStudioWorkspace(value) {
  const key = String(value || "")
    .toLowerCase()
    .replace(/[ _-]/g, "");
  if (["pattern", "patterns", "patternstudio"].includes(key)) return "pattern";
  if (["texture", "paint", "texturepaint", "texturestudio"].includes(key))
    return "texture";
  if (["stamp", "stamps", "stampstudio"].includes(key)) return "stamp";
  if (["bake", "baking", "bakestudio", "bakingstudio"].includes(key))
    return "baking";
  return "material";
}
export function StudioHeader({
  workspace,
  name,
  extension,
  onWorkspace,
  onClose,
  closeLabel,
  children,
  legacyNames = false,
}) {
  return (
    <header
      className={`studio-shared-header tp-workspace-header ${workspace === "material" ? "main-header" : ""}`}
    >
      <div className="tp-workspace-brand">
        <Layers3 size={16} />
        <strong>
          alloy<span>.</span>
        </strong>
        <span className="tp-brand-divider" />
        <span className="tp-file-name" title={name}>
          {name} {extension && <small>{extension}</small>}
        </span>
      </div>
      <nav className="tp-workspace-switch" aria-label="Studio workspace">
        {studioWorkspaces.map(({ id, label }) => (
          <button
            key={id}
            aria-current={id === workspace ? "page" : undefined}
            aria-label={
              legacyNames && id !== "material"
                ? `${label === "Baking" ? "Baking" : label} studio`
                : undefined
            }
            onClick={() => onWorkspace(id)}
          >
            {label}
          </button>
        ))}
      </nav>
      <div className="studio-header-actions tp-workspace-file-actions">
        {children}
        {onClose && (
          <button
            aria-label={closeLabel || `Close ${workspace} studio`}
            title="Return to material workspace"
            onClick={onClose}
          >
            <X size={16} />
          </button>
        )}
      </div>
    </header>
  );
}
export function StudioDockTab({ icon: Icon = Box, children, detail }) {
  return (
    <div className="studio-dock-tab tp-tab-strip">
      <div className="tp-document-tab active">
        <Icon size={12} />
        <span>{children}</span>
        {detail && <small>{detail}</small>}
      </div>
    </div>
  );
}
export class StudioErrorBoundary extends React.Component {
  state = { error: null };
  static getDerivedStateFromError(error) {
    return { error };
  }
  render() {
    const runtimeMismatch =
      /invalid hook call|cannot read properties of null.*(?:useState|useRef|useEffect|useMemo)/i.test(
        this.state.error?.message || "",
      );
    if (this.state.error)
      return (
        <section className="studio-workspace-error" role="alert">
          <Layers3 size={30} />
          <h1>{this.props.name} could not open</h1>
          <p>
            {runtimeMismatch
              ? "The browser loaded different versions of the studio runtime. Reload the studio to reconnect the workspace hooks to React."
              : this.state.error.message ||
                "The workspace encountered an unexpected error."}
          </p>
          <div>
            {runtimeMismatch ? (
              <button onClick={() => window.location.reload()}>
                Reload studio
              </button>
            ) : (
              <button
                onClick={() => {
                  this.setState({ error: null });
                  this.props.onRetry?.();
                }}
              >
                Retry workspace
              </button>
            )}
            <button onClick={this.props.onReturn}>Return to Material</button>
          </div>
          <small>
            Your saved material presets and layer projects have not been
            cleared.
          </small>
        </section>
      );
    return this.props.children;
  }
}
