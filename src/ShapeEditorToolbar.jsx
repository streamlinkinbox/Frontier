import React from "react";
import {
  MousePointer2,
  MousePointer,
  Square,
  Circle,
  Triangle,
  Diamond,
  Pentagon,
  Star,
  PenTool,
  Spline,
  Pencil,
  Minus,
  Hand,
  Grid3X3,
  Magnet,
  Group,
  Ungroup,
  Copy,
  Trash2,
  AlignHorizontalJustifyStart,
  AlignHorizontalJustifyCenter,
  AlignHorizontalJustifyEnd,
  AlignVerticalJustifyStart,
  AlignVerticalJustifyCenter,
  AlignVerticalJustifyEnd,
  BetweenHorizontalStart,
  BetweenVerticalStart,
  Type,
  Pipette,
  CircleDashed,
  Combine,
  Code2,
} from "lucide-react";

export const shapeTools = [
  ["move", "Select tool", "V", MousePointer2, "Move shapes"],
  ["node", "Edit points", "A", MousePointer],
  ["rect", "Rectangle tool", "R", Square],
  ["ellipse", "Ellipse tool", "O", Circle],
  ["line", "Line tool", "L", Minus],
  ["triangle", "Triangle tool", "T", Triangle],
  ["diamond", "Diamond tool", "D", Diamond],
  ["polygon", "Polygon tool", "G", Pentagon],
  ["star", "Star tool", "S", Star],
  ["pen", "Pen tool", "P", PenTool],
  ["curve", "Curve tool", "U", Spline],
  ["pencil", "Freehand tool", "B", Pencil, "Draw path"],
  ["arc", "Arc tool", "C", CircleDashed],
  ["text", "Text tool", "X", Type],
  ["eyedropper", "Eyedropper tool", "I", Pipette],
  ["hand", "Hand tool", "H", Hand],
];
const alignTools = [
  ["left", "Align left", AlignHorizontalJustifyStart],
  ["center-x", "Align horizontal centers", AlignHorizontalJustifyCenter],
  ["right", "Align right", AlignHorizontalJustifyEnd],
  ["top", "Align top", AlignVerticalJustifyStart],
  ["center-y", "Align vertical centers", AlignVerticalJustifyCenter],
  ["bottom", "Align bottom", AlignVerticalJustifyEnd],
];
export function ShapeSelectionActions({
  count,
  hasGroup,
  onAction,
  canBoolean,
}) {
  return (
    <div
      className="se-arrange-tools"
      role="group"
      aria-label="Selection actions"
    >
      <div className="se-align-tools">
        {alignTools.map(([key, label, Icon]) => (
          <button
            key={key}
            title={label}
            aria-label={label}
            disabled={!count}
            onClick={() => onAction(`align-${key}`)}
          >
            <Icon size={15} />
          </button>
        ))}
        <span className="se-tool-divider" />
        <button
          title="Distribute horizontally"
          aria-label="Distribute horizontally"
          disabled={count < 3}
          onClick={() => onAction("distribute-x")}
        >
          <BetweenHorizontalStart size={15} />
        </button>
        <button
          title="Distribute vertically"
          aria-label="Distribute vertically"
          disabled={count < 3}
          onClick={() => onAction("distribute-y")}
        >
          <BetweenVerticalStart size={15} />
        </button>
      </div>
      <details className="se-path-operations">
        <summary>
          <Combine size={14} />
          Path operations
        </summary>
        <div>
          {[
            ["union", "Union"],
            ["difference", "Difference"],
            ["intersection", "Intersection"],
            ["exclude", "Exclude"],
          ].map(([key, label]) => (
            <button
              key={key}
              disabled={!canBoolean}
              onClick={() => onAction(`boolean-${key}`)}
            >
              {label}
            </button>
          ))}
          <small>
            Filled vectors only. Difference subtracts upper layers from the
            bottom shape. Stroke widths are not expanded.
          </small>
        </div>
      </details>
      <div className="se-object-tools">
        <button
          title="Group selection (Ctrl/⌘ G)"
          aria-label="Group selection"
          disabled={count < 2}
          onClick={() => onAction("group")}
        >
          <Group size={15} />
        </button>
        <button
          title="Ungroup selection (Ctrl/⌘ Shift G)"
          aria-label="Ungroup selection"
          disabled={!hasGroup}
          onClick={() => onAction("ungroup")}
        >
          <Ungroup size={15} />
        </button>
        <button
          title="Duplicate motif"
          aria-label="Duplicate selection"
          disabled={!count}
          onClick={() => onAction("duplicate")}
        >
          <Copy size={15} />
        </button>
        <button
          title="Delete motif"
          aria-label="Delete selection"
          disabled={!count}
          onClick={() => onAction("delete")}
        >
          <Trash2 size={15} />
        </button>
      </div>
    </div>
  );
}
export default function ShapeEditorToolbar({
  tool,
  onToolChange,
  grid,
  onGridChange,
  showGrid,
  onGridToggle,
  smartGuides,
  onGuidesToggle,
  aspect,
  onAspectChange,
  snapping,
  onSnappingChange,
  onSVGSource,
}) {
  return (
    <div className="se-toolbar-wrap">
      <div
        className="se-drawing-tools"
        role="toolbar"
        aria-label="Drawing tools"
      >
        {shapeTools.map(([key, label, shortcut, Icon, title]) => (
          <button
            key={key}
            className={tool === key ? "selected" : ""}
            title={title || `${label} (${shortcut})`}
            aria-label={`${label} (${shortcut})`}
            aria-pressed={tool === key}
            onClick={() => onToolChange(key)}
          >
            <Icon size={17} />
            <kbd>{shortcut}</kbd>
          </button>
        ))}
        <button
          title="Paste SVG source"
          aria-label="Paste SVG source"
          onClick={onSVGSource}
        >
          <Code2 size={17} />
        </button>
      </div>
      <div className="se-options-tools">
        <button
          title="Show grid"
          aria-label="Show grid"
          aria-pressed={showGrid}
          onClick={onGridToggle}
        >
          <Grid3X3 size={14} />
        </button>
        <label>
          Snap{" "}
          <select
            aria-label="Grid snapping"
            value={grid}
            onChange={(e) => onGridChange(Number(e.target.value))}
          >
            <option value="0">Off</option>
            <option value="8">8 units</option>
            <option value="16">16 units</option>
            <option value="32">32 units</option>
            <option value="64">64 units</option>
          </select>
        </label>
        <button
          title="Point, curve and smart alignment snapping"
          aria-label="Smart alignment guides"
          aria-pressed={smartGuides}
          onClick={onGuidesToggle}
        >
          <Magnet size={14} />
        </button>
        <details className="se-snap-details">
          <summary>Snap targets</summary>
          <div className="se-snap-menu">
            {[
              ["points", "Anchors & centers"],
              ["curves", "Curves & edges"],
              ["guides", "Object alignment"],
            ].map(([key, label]) => (
              <label key={key}>
                <input
                  aria-label={`Snap to ${label.toLowerCase()}`}
                  type="checkbox"
                  checked={snapping[key]}
                  onChange={(e) =>
                    onSnappingChange({ ...snapping, [key]: e.target.checked })
                  }
                />
                {label}
              </label>
            ))}
            <label>
              Angle
              <select
                aria-label="Angle snapping"
                value={snapping.angle}
                onChange={(e) =>
                  onSnappingChange({
                    ...snapping,
                    angle: Number(e.target.value),
                  })
                }
              >
                {[0, 15, 30, 45, 90].map((v) => (
                  <option key={v} value={v}>
                    {v ? `${v}°` : "Off"}
                  </option>
                ))}
              </select>
            </label>
            <small>
              The magnet enables target snapping; the grid and angle settings
              are independent.
            </small>
          </div>
        </details>
        <label className="pe-check">
          <input
            type="checkbox"
            aria-label="Keep aspect ratio"
            checked={aspect}
            onChange={(e) => onAspectChange(e.target.checked)}
          />{" "}
          Constrain proportions
        </label>
        <span>Shift to constrain · Space to pan</span>
      </div>
    </div>
  );
}
