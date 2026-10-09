import React from "react";
import {
  SlidersHorizontal,
  RotateCcw,
  Circle,
  Star,
  Layers3,
  ArrowRight,
  Plus,
} from "lucide-react";
import { StudioDockTab } from "./StudioUI.jsx";
import MaterialControls from "./RecipeInspector.jsx";
export default function MaterialInspectorPanel({
  params,
  selected,
  setParams,
  notify,
  thumbs = {},
  favorites = [],
  toggleFavorite = () => {},
  tab,
  setTab,
  layerCount,
  update,
  setCustomName,
  setModal,
  openWorkspace,
}) {
  return (
    <aside className="inspector-panel">
      <StudioDockTab icon={SlidersHorizontal}>Inspector</StudioDockTab>
      <div className="panel-title">
        <h2>Material inspector</h2>
        <button
          className="icon-button"
          title="Reset material to preset"
          onClick={() => {
            setParams({ ...selected });
            notify("Material properties reset.");
          }}
        >
          <RotateCcw size={15} />
        </button>
      </div>
      {params.pattern && (
        <div className="pattern-applied">
          <span>Pattern: {params.pattern.name}</span>
          <button onClick={() => openWorkspace("pattern")}>Edit</button>
          <button
            onClick={() =>
              setParams((p) => ({
                ...p,
                pattern: null,
                name: p.patternBaseName || p.name,
                patternBaseName: undefined,
              }))
            }
          >
            Remove pattern
          </button>
        </div>
      )}
      <div className="inspector-material">
        <div className="inspector-thumbnail">
          {thumbs[selected.id] ? (
            <img src={thumbs[selected.id]} alt="" />
          ) : (
            <Circle size={24} style={{ color: params.color }} />
          )}
        </div>
        <div>
          <h3>{params.name}</h3>
          <p>
            <span className="status-dot" />
            {params.label}
          </p>
        </div>
        <button
          className="icon-button"
          title="Save current material"
          onClick={() => toggleFavorite(selected.id)}
        >
          <Star
            size={15}
            fill={favorites.includes(selected.id) ? "currentColor" : "none"}
          />
        </button>
      </div>
      <div className="inspector-tabs">
        {["Properties", "Layers"].map((t) => (
          <button
            key={t}
            className={tab === t ? "active" : ""}
            onClick={() => setTab(t)}
          >
            {t === "Properties" ? (
              <SlidersHorizontal size={13} />
            ) : (
              <Layers3 size={14} />
            )}{" "}
            {t}
            {t === "Layers" && <span>{layerCount}</span>}
          </button>
        ))}
      </div>
      <div className="inspector-scroll">
        {tab === "Properties" ? (
          <MaterialControls
            key={selected.id}
            params={params}
            update={update}
            setParams={setParams}
          />
        ) : (
          <div className="layers-content">
            <p className="layer-intro">
              A physically based finish, built from the surface up.
            </p>
            {[
              ...(params.coat > 0
                ? [
                    {
                      name: "Clearcoat",
                      detail: "Dielectric protective finish",
                      value: params.coat,
                      color: "#c2debb",
                    },
                  ]
                : []),
              ...(params.type === 0 && params.flakes > 0
                ? [
                    {
                      name: "Metallic flakes",
                      detail: "Multicolor reflective particles",
                      value: params.flakes,
                      color: "#bca4d2",
                    },
                  ]
                : []),
              {
                name: "Base substrate",
                detail: params.category + " · " + params.color.toUpperCase(),
                value: 1,
                color: params.color,
              },
            ].map((l, i) => (
              <div className="layer-card" key={l.name}>
                <div
                  className="layer-visual"
                  style={{ "--layer-color": l.color }}
                >
                  <i />
                  <i />
                  <i />
                </div>
                <div>
                  <span className="layer-num">0{i + 1}</span>
                  <h4>{l.name}</h4>
                  <p>{l.detail}</p>
                  <span className="layer-value">
                    {Math.round(l.value * 100)}% strength
                  </span>
                </div>
              </div>
            ))}
            <div className="layer-note">
              <Layers3 size={17} />
              <p>
                Layers are evaluated together in a single energy-conserving PBR
                shader.
              </p>
            </div>
            <button
              className="secondary-button"
              onClick={() => setTab("Properties")}
            >
              Edit layer properties <ArrowRight size={14} />
            </button>
          </div>
        )}
      </div>
      <div className="inspector-footer">
        <button
          className="save-preset-button"
          onClick={() => {
            setCustomName(params.name + " Custom");
            setModal("save");
          }}
        >
          <Plus size={14} /> Save as preset <span>⌘ S</span>
        </button>
        <span>Made to be made your own.</span>
      </div>
    </aside>
  );
}
