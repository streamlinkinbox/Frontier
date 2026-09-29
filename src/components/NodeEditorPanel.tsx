import React, { useState, useRef } from 'react';
import {
  LayoutGrid,
  SlidersHorizontal,
  Play,
  Settings2,
  Box,
  Sparkles,
  Droplets,
  Wind,
  Mountain,
  Layers,
  Minus,
  Eye,
  Crosshair,
  Lock,
  Unlock,
  Type,
  Search,
  X,
  Plus,
  ChevronDown,
  ChevronRight,
  Check,
  ShieldAlert,
  ShieldCheck,
  AlertTriangle,
} from 'lucide-react';
import {
  GraphNodeData,
  GraphEdge,
  NODE_CATALOG,
  CatalogItem,
  GraphPresetId,
} from '../engine/graphEvaluator';
import { NodeResolutionReport, SDFDomainConfig } from '../engine/resolutionGuard';
import { NodeInspectorPanel } from './NodeInspectorModal';

interface NodeEditorPanelProps {
  nodes: GraphNodeData[];
  edges: GraphEdge[];
  selectedNodeId: string | null;
  previewNodeId: string | null;
  domain: SDFDomainConfig;
  reports: Record<string, NodeResolutionReport>;
  buildProgress: number;
  isBuilding: boolean;
  buildTimeMs: number;
  wireframe: boolean;
  showRiverWater: boolean;
  onSelectNode: (id: string | null) => void;
  onTogglePreviewNode: (id: string) => void;
  onMoveNode: (id: string, x: number, y: number) => void;
  onUpdateNodeMeta: (id: string, patch: Partial<GraphNodeData>) => void;
  onUpdateNodeParams: (id: string, newParams: Record<string, any>) => void;
  onUpdateDomain: (patch: Partial<SDFDomainConfig>) => void;
  onAddNodeFromCatalog: (item: CatalogItem) => void;
  onDeleteNode: (id: string) => void;
  onConnectNodes: (sourceId: string, targetId: string) => void;
  onDisconnectEdge: (edgeId: string) => void;
  onRunBuild: () => void;
  onToggleWireframe: () => void;
  onToggleRiverWater: () => void;
  onLoadGraphPreset?: (preset: GraphPresetId) => void;
}

export const NodeEditorPanel: React.FC<NodeEditorPanelProps> = ({
  nodes,
  edges,
  selectedNodeId,
  previewNodeId,
  domain,
  reports,
  buildProgress,
  isBuilding,
  buildTimeMs,
  wireframe,
  showRiverWater,
  onSelectNode,
  onTogglePreviewNode,
  onMoveNode,
  onUpdateNodeMeta,
  onUpdateNodeParams,
  onUpdateDomain,
  onAddNodeFromCatalog,
  onDeleteNode,
  onConnectNodes,
  onDisconnectEdge,
  onRunBuild,
  onToggleWireframe,
  onToggleRiverWater,
  onLoadGraphPreset,
}) => {
  // Pan & Zoom state for the Node Canvas
  const [pan, setPan] = useState<{ x: number; y: number }>({ x: 0, y: 0 });
  const [zoom, setZoom] = useState<number>(0.88);
  const [isPanning, setIsPanning] = useState(false);
  const panStartRef = useRef<{ x: number; y: number; panX: number; panY: number }>({
    x: 0,
    y: 0,
    panX: 0,
    panY: 0,
  });

  // Node dragging state
  const [draggingNodeId, setDraggingNodeId] = useState<string | null>(null);
  const dragOffsetRef = useRef<{ startMouseX: number; startMouseY: number; nodeX: number; nodeY: number }>({
    startMouseX: 0,
    startMouseY: 0,
    nodeX: 0,
    nodeY: 0,
  });

  // Wire connection dragging state
  const [connectingFromId, setConnectingFromId] = useState<string | null>(null);
  const [mouseCanvasPos, setMouseCanvasPos] = useState<{ x: number; y: number }>({ x: 0, y: 0 });

  // UI Popovers matching Screenshots 3276, 3277, 3278, 3279
  const [showNodeLibrary, setShowNodeLibrary] = useState<boolean>(false);
  const [showInspector, setShowInspector] = useState<boolean>(false);
  const [showViewMenu, setShowViewMenu] = useState<boolean>(false);
  const [canvasControls, setCanvasControls] = useState<boolean>(true);
  const [backgroundMode, setBackgroundMode] = useState<'dotted' | 'blank'>('dotted');

  // Node library search & category accordion state
  const [searchQuery, setSearchQuery] = useState<string>('');
  const [openCategories, setOpenCategories] = useState<Record<string, boolean>>({
    generators: true,
    erosion: true,
    sdf_caves: true,
    texturing: false,
  });

  // Inline title rename state (for 'T' icon in floating node toolbar)
  const [renamingNodeId, setRenamingNodeId] = useState<string | null>(null);

  const canvasRef = useRef<HTMLDivElement | null>(null);

  const screenToCanvas = (clientX: number, clientY: number) => {
    const rect = canvasRef.current?.getBoundingClientRect();
    if (!rect) return { x: 0, y: 0 };
    return {
      x: (clientX - rect.left - pan.x) / zoom,
      y: (clientY - rect.top - pan.y) / zoom,
    };
  };

  const handleCanvasMouseDown = (e: React.MouseEvent<HTMLDivElement>) => {
    if (e.button !== 0 && e.button !== 1) return;
    setShowViewMenu(false);
    setRenamingNodeId(null);
    setIsPanning(true);
    panStartRef.current = {
      x: e.clientX,
      y: e.clientY,
      panX: pan.x,
      panY: pan.y,
    };
  };

  const handleCanvasMouseMove = (e: React.MouseEvent<HTMLDivElement>) => {
    if (connectingFromId) {
      setMouseCanvasPos(screenToCanvas(e.clientX, e.clientY));
    }

    if (draggingNodeId) {
      const dx = (e.clientX - dragOffsetRef.current.startMouseX) / zoom;
      const dy = (e.clientY - dragOffsetRef.current.startMouseY) / zoom;
      onMoveNode(
        draggingNodeId,
        Math.round(dragOffsetRef.current.nodeX + dx),
        Math.round(dragOffsetRef.current.nodeY + dy)
      );
      return;
    }

    if (isPanning) {
      const dx = e.clientX - panStartRef.current.x;
      const dy = e.clientY - panStartRef.current.y;
      setPan({
        x: panStartRef.current.panX + dx,
        y: panStartRef.current.panY + dy,
      });
    }
  };

  const handleCanvasMouseUp = () => {
    setIsPanning(false);
    setDraggingNodeId(null);
    setConnectingFromId(null);
  };

  const handleWheel = (e: React.WheelEvent<HTMLDivElement>) => {
    if (!canvasControls) return;
    e.preventDefault();
    const factor = e.deltaY < 0 ? 1.08 : 0.92;
    const nextZoom = Math.max(0.45, Math.min(1.65, zoom * factor));
    setZoom(nextZoom);
  };

  const centerOnNode = (node: GraphNodeData) => {
    const rect = canvasRef.current?.getBoundingClientRect();
    if (!rect) return;
    setPan({
      x: rect.width * 0.42 - (node.x + 105) * zoom,
      y: rect.height * 0.45 - (node.y + 55) * zoom,
    });
  };

  const renderNodeIcon = (node: GraphNodeData) => {
    if (node.type === 'Start' || node.category === 'sdf_caves') {
      return <Box className="w-4 h-4 text-neutral-300" />;
    }
    if (node.category === 'generators') {
      return <Sparkles className="w-4 h-4 text-amber-400/90" />;
    }
    if (node.type === 'WindErosion') {
      return <Wind className="w-4 h-4 text-sky-300" />;
    }
    if (node.type === 'CliffStrata') {
      return <Mountain className="w-4 h-4 text-orange-300" />;
    }
    if (node.category === 'erosion') {
      return <Droplets className="w-4 h-4 text-cyan-400" />;
    }
    return <Layers className="w-4 h-4 text-emerald-400" />;
  };

  const selectedNode = nodes.find((n) => n.id === selectedNodeId) || null;

  const filteredCatalog = NODE_CATALOG.filter(
    (item) =>
      item.title.toLowerCase().includes(searchQuery.toLowerCase()) ||
      item.subtitle.toLowerCase().includes(searchQuery.toLowerCase())
  );

  // Port coordinate helper for SVG wires
  const getOutPortPos = (node: GraphNodeData) => ({
    x: node.x + 216,
    y: node.y + (node.collapsed ? 34 : 88),
  });

  const getInPortPos = (node: GraphNodeData) => ({
    x: node.x,
    y: node.y + (node.collapsed ? 34 : 88),
  });

  return (
    <div
      ref={canvasRef}
      onMouseDown={handleCanvasMouseDown}
      onMouseMove={handleCanvasMouseMove}
      onMouseUp={handleCanvasMouseUp}
      onWheel={handleWheel}
      className="w-full h-full relative overflow-hidden bg-[#0b0b0b] select-none"
      style={
        backgroundMode === 'dotted'
          ? {
              backgroundImage:
                'radial-gradient(rgba(255, 255, 255, 0.14) 1.25px, transparent 1.25px)',
              backgroundSize: `${24 * zoom}px ${24 * zoom}px`,
              backgroundPosition: `${pan.x}px ${pan.y}px`,
            }
          : undefined
      }
    >
      {/* TOP-CENTER FLOATING PILL BAR (Screenshot 3276, 3277, 3278, 3279) */}
      <div
        onMouseDown={(e) => e.stopPropagation()}
        className="absolute top-6 left-1/2 -translate-x-1/2 z-30 h-[46px] px-2.5 rounded-full bg-[#121212]/95 border border-white/12 flex items-center gap-1.5 shadow-2xl"
      >
        {/* Grid / Node Library Button */}
        <button
          onClick={() => setShowNodeLibrary((v) => !v)}
          title="Toggle Node Library"
          className={`w-8 h-8 rounded-full flex items-center justify-center transition ${
            showNodeLibrary
              ? 'bg-white text-black shadow'
              : 'text-neutral-400 hover:text-white hover:bg-white/10'
          }`}
        >
          <LayoutGrid className="w-4 h-4" />
        </button>

        {/* Parameters / SDF Resolution Inspector Button */}
        <button
          onClick={() => {
            if (!selectedNodeId && nodes.length > 0) {
              onSelectNode(nodes[0].id);
            }
            setShowInspector((v) => !v);
          }}
          title="Toggle Node Parameters & SDF Resolution Guard"
          className={`w-8 h-8 rounded-full flex items-center justify-center transition ${
            showInspector
              ? 'bg-white text-black shadow'
              : 'text-neutral-400 hover:text-white hover:bg-white/10'
          }`}
        >
          <SlidersHorizontal className="w-4 h-4" />
        </button>

        {/* Vertical Divider */}
        <div className="w-[1px] h-5 bg-white/12 mx-1" />

        {/* Play / Evaluate Graph Button */}
        <button
          onClick={onRunBuild}
          title="Evaluate SDF Terrain Graph"
          className="w-9 h-8 rounded-full flex items-center justify-center text-neutral-300 hover:text-white hover:bg-white/10 transition"
        >
          <Play
            className={`w-4 h-4 ${
              isBuilding ? 'text-emerald-400 animate-pulse' : ''
            }`}
          />
        </button>
      </div>

      {/* TOP-RIGHT VIEW SETTINGS BUTTON (Screenshot 3279) */}
      <button
        onMouseDown={(e) => e.stopPropagation()}
        onClick={() => setShowViewMenu((v) => !v)}
        title="View Options"
        className={`absolute top-6 right-6 z-30 w-11 h-11 rounded-full border flex items-center justify-center transition shadow-2xl ${
          showViewMenu
            ? 'bg-white text-black border-white'
            : 'bg-[#121212]/95 text-neutral-300 border-white/12 hover:border-white/25'
        }`}
      >
        <Settings2 className="w-4 h-4" />
      </button>

      {/* TOP-RIGHT VIEW POPOVER MENU (Exact match to Screenshot 3279) */}
      {showViewMenu && (
        <div
          onMouseDown={(e) => e.stopPropagation()}
          className="absolute top-20 right-6 z-30 w-[236px] rounded-[20px] bg-[#111111]/95 backdrop-blur-md border border-white/12 p-4 shadow-2xl"
        >
          <div className="text-[10px] tracking-wider uppercase text-neutral-500 font-semibold mb-3">
            VIEW
          </div>
          <div className="flex items-center justify-between py-1 mb-4">
            <span className="text-xs text-neutral-200 font-medium">
              Canvas Controls
            </span>
            <button
              onClick={() => setCanvasControls((v) => !v)}
              className={`w-9 h-5 rounded-full transition relative ${
                canvasControls ? 'bg-neutral-600' : 'bg-neutral-800'
              }`}
            >
              <span
                className={`block w-3.5 h-3.5 rounded-full bg-white transition transform ${
                  canvasControls ? 'translate-x-4' : 'translate-x-1'
                }`}
              />
            </button>
          </div>

          <div className="text-[10px] tracking-wider uppercase text-neutral-500 font-semibold mb-2">
            BACKGROUND
          </div>
          <div className="space-y-1">
            <button
              onClick={() => setBackgroundMode('dotted')}
              className={`w-full px-3 py-2 rounded-xl text-xs flex items-center justify-between transition ${
                backgroundMode === 'dotted'
                  ? 'bg-[#262626] text-white font-medium'
                  : 'text-neutral-400 hover:text-white hover:bg-white/5'
              }`}
            >
              <span>Dotted Grid</span>
              {backgroundMode === 'dotted' && <Check className="w-3.5 h-3.5" />}
            </button>
            <button
              onClick={() => setBackgroundMode('blank')}
              className={`w-full px-3 py-2 rounded-xl text-xs flex items-center justify-between transition ${
                backgroundMode === 'blank'
                  ? 'bg-[#262626] text-white font-medium'
                  : 'text-neutral-400 hover:text-white hover:bg-white/5'
              }`}
            >
              <span>Blank</span>
              {backgroundMode === 'blank' && <Check className="w-3.5 h-3.5" />}
            </button>
          </div>

          <div className="text-[10px] tracking-wider uppercase text-neutral-500 font-semibold mt-4 mb-2">
            3D SDF DISPLAY
          </div>
          <div className="space-y-1">
            <button
              onClick={onToggleRiverWater}
              className={`w-full px-3 py-2 rounded-xl text-xs flex items-center justify-between transition ${
                showRiverWater
                  ? 'bg-[#262626] text-white font-medium'
                  : 'text-neutral-400 hover:text-white hover:bg-white/5'
              }`}
            >
              <span>River Water Surface</span>
              {showRiverWater && <Check className="w-3.5 h-3.5" />}
            </button>
            <button
              onClick={onToggleWireframe}
              className={`w-full px-3 py-2 rounded-xl text-xs flex items-center justify-between transition ${
                wireframe
                  ? 'bg-[#262626] text-white font-medium'
                  : 'text-neutral-400 hover:text-white hover:bg-white/5'
              }`}
            >
              <span>SDF Mesh Wireframe</span>
              {wireframe && <Check className="w-3.5 h-3.5" />}
            </button>
          </div>

          {onLoadGraphPreset && (
            <>
              <div className="text-[10px] tracking-wider uppercase text-neutral-500 font-semibold mt-4 mb-2">
                GRAPH PIPELINE PRESETS
              </div>
              <div className="space-y-1">
                <button
                  onClick={() => {
                    onLoadGraphPreset('monument_valley');
                    setShowViewMenu(false);
                  }}
                  className="w-full px-3 py-2 rounded-xl text-xs flex items-center justify-between bg-[#222] hover:bg-[#2d2d2d] text-white font-medium transition"
                >
                  <span>Monument Valley Monoliths</span>
                </button>
                <button
                  onClick={() => {
                    onLoadGraphPreset('colorado_rivers');
                    setShowViewMenu(false);
                  }}
                  className="w-full px-3 py-2 rounded-xl text-xs flex items-center justify-between bg-[#1b1b1b] hover:bg-[#262626] text-neutral-300 hover:text-white transition"
                >
                  <span>Colorado Canyon & Rivers</span>
                </button>
              </div>
            </>
          )}
        </div>
      )}

      {/* NODE PARAMETERS & SDF RESOLUTION INSPECTOR PANEL */}
      {showInspector && selectedNode && (
        <NodeInspectorPanel
          node={selectedNode}
          domain={domain}
          report={reports[selectedNode.id]}
          onUpdateParams={onUpdateNodeParams}
          onUpdateDomain={onUpdateDomain}
          onClose={() => setShowInspector(false)}
        />
      )}

      {/* TRANSFORMED CANVAS LAYER FOR EDGES AND NODES */}
      <div
        className="absolute inset-0 pointer-events-none"
        style={{
          transform: `translate(${pan.x}px, ${pan.y}px) scale(${zoom})`,
          transformOrigin: '0 0',
        }}
      >
        {/* SVG Connections Layer */}
        <svg className="overflow-visible w-full h-full absolute inset-0 pointer-events-none">
          {edges.map((edge) => {
            const srcNode = nodes.find((n) => n.id === edge.source);
            const dstNode = nodes.find((n) => n.id === edge.target);
            if (!srcNode || !dstNode) return null;

            const p1 = getOutPortPos(srcNode);
            const p2 = getInPortPos(dstNode);
            const dx = Math.max(60, Math.abs(p2.x - p1.x) * 0.48);
            const path = `M ${p1.x} ${p1.y} C ${p1.x + dx} ${p1.y}, ${p2.x - dx} ${p2.y}, ${p2.x} ${p2.y}`;

            const dstReport = reports[dstNode.id];
            const isBlocked = dstReport?.status === 'blocked_low_res';

            return (
              <g key={edge.id} className="pointer-events-auto cursor-pointer" onDoubleClick={() => onDisconnectEdge(edge.id)}>
                <path
                  d={path}
                  fill="none"
                  stroke="transparent"
                  strokeWidth={14}
                />
                <path
                  d={path}
                  fill="none"
                  stroke={isBlocked ? 'rgba(239, 68, 68, 0.55)' : 'rgba(255, 255, 255, 0.36)'}
                  strokeWidth={2}
                  strokeDasharray={isBlocked ? '5 4' : undefined}
                />
              </g>
            );
          })}

          {/* Active Wire Being Dragged */}
          {connectingFromId && (() => {
            const srcNode = nodes.find((n) => n.id === connectingFromId);
            if (!srcNode) return null;
            const p1 = getOutPortPos(srcNode);
            const p2 = mouseCanvasPos;
            const dx = Math.max(50, Math.abs(p2.x - p1.x) * 0.45);
            const path = `M ${p1.x} ${p1.y} C ${p1.x + dx} ${p1.y}, ${p2.x - dx} ${p2.y}, ${p2.x} ${p2.y}`;
            return (
              <path
                d={path}
                fill="none"
                stroke="rgba(255, 255, 255, 0.75)"
                strokeWidth={2}
                strokeDasharray="4 4"
              />
            );
          })()}
        </svg>

        {/* Nodes Layer */}
        {nodes.map((node) => {
          const isSelected = node.id === selectedNodeId;
          const isPreviewed = node.id === previewNodeId;
          const report = reports[node.id];

          return (
            <div
              key={node.id}
              style={{
                transform: `translate(${node.x}px, ${node.y}px)`,
                width: 216,
              }}
              onMouseDown={(e) => {
                e.stopPropagation();
                onSelectNode(node.id);
                if (!node.locked) {
                  setDraggingNodeId(node.id);
                  dragOffsetRef.current = {
                    startMouseX: e.clientX,
                    startMouseY: e.clientY,
                    nodeX: node.x,
                    nodeY: node.y,
                  };
                }
              }}
              onDoubleClick={(e) => {
                e.stopPropagation();
                onSelectNode(node.id);
                setShowInspector(true);
              }}
              className="absolute top-0 left-0 pointer-events-auto"
            >
              {/* FLOATING 7-ICON ACTION TOOLBAR ABOVE SELECTED NODE (Exact match to Screenshot 3277) */}
              {isSelected && (
                <div
                  onMouseDown={(e) => e.stopPropagation()}
                  className="absolute -top-12 left-1/2 -translate-x-1/2 h-9 px-3 rounded-full bg-[#121212] border border-white/14 flex items-center gap-2.5 shadow-2xl whitespace-nowrap z-20"
                >
                  {/* 1. Minus: Collapse / Minimize node */}
                  <button
                    onClick={() =>
                      onUpdateNodeMeta(node.id, { collapsed: !node.collapsed })
                    }
                    title="Collapse / Expand Node"
                    className="text-neutral-400 hover:text-white transition"
                  >
                    <Minus className="w-3.5 h-3.5" />
                  </button>

                  {/* 2. Eye: Preview / Solo terrain at this node */}
                  <button
                    onClick={() => onTogglePreviewNode(node.id)}
                    title="Solo Preview Terrain at this Node"
                    className={`transition ${
                      isPreviewed
                        ? 'text-emerald-400'
                        : 'text-neutral-400 hover:text-white'
                    }`}
                  >
                    <Eye className="w-3.5 h-3.5" />
                  </button>

                  {/* 3. Crosshair: Center canvas on this node */}
                  <button
                    onClick={() => centerOnNode(node)}
                    title="Center on Node"
                    className="text-neutral-400 hover:text-white transition"
                  >
                    <Crosshair className="w-3.5 h-3.5" />
                  </button>

                  {/* 4. Lock: Lock / Unlock node position */}
                  <button
                    onClick={() =>
                      onUpdateNodeMeta(node.id, { locked: !node.locked })
                    }
                    title={node.locked ? 'Unlock Node' : 'Lock Node'}
                    className={`transition ${
                      node.locked
                        ? 'text-amber-400'
                        : 'text-neutral-400 hover:text-white'
                    }`}
                  >
                    {node.locked ? (
                      <Lock className="w-3.5 h-3.5" />
                    ) : (
                      <Unlock className="w-3.5 h-3.5" />
                    )}
                  </button>

                  {/* 5. T: Rename node title */}
                  <button
                    onClick={() =>
                      setRenamingNodeId(
                        renamingNodeId === node.id ? null : node.id
                      )
                    }
                    title="Rename Node"
                    className="text-neutral-400 hover:text-white transition"
                  >
                    <Type className="w-3.5 h-3.5" />
                  </button>

                  {/* 6. Search / Inspect: Open parameters & SDF resolution guard */}
                  <button
                    onClick={() => setShowInspector((v) => !v)}
                    title="Inspect Node Parameters & SDF Resolution"
                    className={`transition ${
                      showInspector
                        ? 'text-white'
                        : 'text-neutral-400 hover:text-white'
                    }`}
                  >
                    <Search className="w-3.5 h-3.5" />
                  </button>

                  {/* 7. Red X: Delete node */}
                  <button
                    onClick={() => {
                      if (node.type !== 'Start') onDeleteNode(node.id);
                    }}
                    title={
                      node.type === 'Start'
                        ? 'Entry Point cannot be deleted'
                        : 'Delete Node'
                    }
                    className={`transition ${
                      node.type === 'Start'
                        ? 'text-red-500/35 cursor-not-allowed'
                        : 'text-red-500 hover:text-red-400'
                    }`}
                  >
                    <X className="w-3.5 h-3.5" />
                  </button>
                </div>
              )}

              {/* NODE CARD (Matching Screenshot 3276 & 3277) */}
              <div
                className={`rounded-[22px] bg-[#151515] transition-shadow relative ${
                  isSelected
                    ? 'border-[1.5px] border-white/90 shadow-[0_0_28px_rgba(255,255,255,0.07)]'
                    : 'border border-white/14 hover:border-white/30'
                }`}
              >
                {/* Header */}
                <div className="px-4 pt-3.5 pb-3 flex items-center gap-3">
                  <div className="w-9 h-9 rounded-full bg-[#242424] flex items-center justify-center shrink-0">
                    {renderNodeIcon(node)}
                  </div>
                  <div className="min-w-0 flex-1">
                    {renamingNodeId === node.id ? (
                      <input
                        autoFocus
                        value={node.title}
                        onChange={(e) =>
                          onUpdateNodeMeta(node.id, { title: e.target.value })
                        }
                        onBlur={() => setRenamingNodeId(null)}
                        onKeyDown={(e) => {
                          if (e.key === 'Enter') setRenamingNodeId(null);
                        }}
                        className="w-full bg-[#222] text-white text-xs font-semibold px-1.5 py-0.5 rounded outline-none border border-white/30"
                      />
                    ) : (
                      <div className="text-[13px] font-semibold text-white truncate flex items-center gap-1.5">
                        <span>{node.title}</span>
                        {isPreviewed && (
                          <span className="text-[9px] px-1.5 py-0.5 rounded-full bg-emerald-500/20 text-emerald-300 font-medium">
                            VIEW
                          </span>
                        )}
                      </div>
                    )}
                    <div className="text-[11px] text-neutral-500 truncate mt-0.5">
                      {node.subtitle}
                    </div>
                  </div>
                </div>

                {/* Body */}
                {!node.collapsed && (
                  <>
                    <div className="h-[1px] bg-white/10 w-full" />
                    <div className="px-4 py-3 relative">
                      <div className="flex items-center justify-between text-[10px] tracking-wider text-neutral-500 font-medium">
                        <span>IN</span>
                        <div className="text-right leading-tight">
                          <div>OUT</div>
                          <div className="text-[10px] text-neutral-500 font-normal tracking-normal">
                            Flow
                          </div>
                        </div>
                      </div>

                      {/* Compact SDF Resolution Guard & Quick Status */}
                      <div className="mt-2 pt-2 border-t border-white/5 flex items-center justify-between text-[10px]">
                        {node.type === 'Start' ? (
                          <span className="text-neutral-400 font-mono">
                            {domain.worldSize}m · {domain.sdfResolution}×{domain.sdfResolution}×{Math.round(domain.sdfResolution * 0.5)} (Δx=
                            {(domain.worldSize / domain.sdfResolution).toFixed(1)}m)
                          </span>
                        ) : report?.status === 'blocked_low_res' ? (
                          <span className="text-red-400 flex items-center gap-1 font-medium">
                            <ShieldAlert className="w-3 h-3 shrink-0" />
                            <span>Low-Res Guard: Blur Blocked</span>
                          </span>
                        ) : report?.status === 'clamped' ? (
                          <span className="text-amber-300/90 flex items-center gap-1">
                            <AlertTriangle className="w-3 h-3 shrink-0" />
                            <span>Nyquist Clamped (2Δx)</span>
                          </span>
                        ) : (
                          <span className="text-neutral-400 flex items-center gap-1">
                            <ShieldCheck className="w-3 h-3 text-emerald-400/80 shrink-0" />
                            <span>
                              Δx={(domain.worldSize / domain.sdfResolution).toFixed(1)}m · Sharp SDF
                            </span>
                          </span>
                        )}
                      </div>
                    </div>
                  </>
                )}

                {/* INPUT PORT HANDLE (Left border dot) */}
                {node.type !== 'Start' && (
                  <div
                    onMouseUp={(e) => {
                      e.stopPropagation();
                      if (connectingFromId && connectingFromId !== node.id) {
                        onConnectNodes(connectingFromId, node.id);
                        setConnectingFromId(null);
                      }
                    }}
                    title="Input Flow Port"
                    className="w-2.5 h-2.5 rounded-full bg-[#8a8a8e] hover:bg-white hover:scale-125 transition absolute -left-[5px] cursor-crosshair"
                    style={{ top: node.collapsed ? 29 : 83 }}
                  />
                )}

                {/* OUTPUT PORT HANDLE (Right border dot matching Screenshot 3276) */}
                <div
                  onMouseDown={(e) => {
                    e.stopPropagation();
                    setConnectingFromId(node.id);
                    setMouseCanvasPos(screenToCanvas(e.clientX, e.clientY));
                  }}
                  title="Drag to connect Output Flow"
                  className="w-2.5 h-2.5 rounded-full bg-[#8a8a8e] hover:bg-white hover:scale-125 transition absolute -right-[5px] cursor-crosshair"
                  style={{ top: node.collapsed ? 29 : 83 }}
                />
              </div>
            </div>
          );
        })}
      </div>

      {/* FLOATING NODE LIBRARY DRAWER (Exact match to Screenshot 3278) */}
      {showNodeLibrary && (
        <div
          onMouseDown={(e) => e.stopPropagation()}
          onWheel={(e) => e.stopPropagation()}
          className="absolute bottom-6 right-6 z-30 w-[278px] max-h-[470px] rounded-[22px] bg-[#0e0e0e]/95 backdrop-blur-md border border-white/12 p-3.5 flex flex-col shadow-2xl"
        >
          {/* Search Input */}
          <div className="mb-3">
            <input
              type="text"
              value={searchQuery}
              onChange={(e) => setSearchQuery(e.target.value)}
              placeholder="Search nodes..."
              className="w-full bg-[#1a1a1a] border border-white/8 focus:border-white/25 rounded-xl px-3 py-2 text-xs text-neutral-200 placeholder-neutral-500 outline-none transition"
            />
          </div>

          {/* Scrollable Categorized Node List */}
          <div className="overflow-y-auto pr-1 space-y-2 flex-1">
            {(
              [
                {
                  key: 'generators',
                  label: 'Generators',
                  icon: <Sparkles className="w-3.5 h-3.5 text-amber-400" />,
                },
                {
                  key: 'erosion',
                  label: 'Erosion & Cliffs',
                  icon: <Droplets className="w-3.5 h-3.5 text-cyan-400" />,
                },
                {
                  key: 'sdf_caves',
                  label: 'SDF & Caves',
                  icon: <Box className="w-3.5 h-3.5 text-violet-400" />,
                },
                {
                  key: 'texturing',
                  label: 'Texturing',
                  icon: <Layers className="w-3.5 h-3.5 text-teal-400" />,
                },
              ] as const
            ).map((cat) => {
              const items = filteredCatalog.filter(
                (i) => i.category === cat.key
              );
              if (items.length === 0) return null;
              const isOpen = openCategories[cat.key] || searchQuery.length > 0;

              return (
                <div key={cat.key} className="pt-1">
                  <button
                    onClick={() =>
                      setOpenCategories((prev) => ({
                        ...prev,
                        [cat.key]: !prev[cat.key],
                      }))
                    }
                    className="w-full flex items-center justify-between py-1.5 px-1 text-xs font-medium text-neutral-200 hover:text-white transition"
                  >
                    <div className="flex items-center gap-2.5">
                      {cat.icon}
                      <span>{cat.label}</span>
                    </div>
                    {isOpen ? (
                      <ChevronDown className="w-3.5 h-3.5 text-neutral-500" />
                    ) : (
                      <ChevronRight className="w-3.5 h-3.5 text-neutral-500" />
                    )}
                  </button>

                  {isOpen && (
                    <div className="mt-1 space-y-1 pl-1">
                      {items.map((item) => (
                        <button
                          key={item.type}
                          onClick={() => onAddNodeFromCatalog(item)}
                          className="w-full flex items-center justify-between py-2 px-2.5 rounded-xl hover:bg-white/8 transition group text-left"
                        >
                          <div className="min-w-0 pr-2">
                            <div className="text-[12px] font-medium text-neutral-200 group-hover:text-white leading-tight">
                              {item.title}
                            </div>
                            <div className="text-[10px] text-neutral-500 leading-tight mt-0.5 truncate">
                              {item.subtitle}
                            </div>
                          </div>
                          <Plus className="w-3.5 h-3.5 text-neutral-500 group-hover:text-white shrink-0" />
                        </button>
                      ))}
                    </div>
                  )}
                </div>
              );
            })}
          </div>
        </div>
      )}

      {/* BOTTOM-LEFT BUILT 100% STATUS PILL (Exact match to Screenshot 3276, 3277, 3278, 3279) */}
      <div
        onMouseDown={(e) => e.stopPropagation()}
        onClick={() => {
          onSelectNode('node-start');
          setShowInspector(true);
        }}
        title="Click to inspect 3D SDF Resolution & Domain Guard"
        className="absolute bottom-5 left-5 z-30 min-w-[145px] rounded-full bg-[#121212]/95 border border-white/12 px-4 py-2 shadow-2xl cursor-pointer hover:border-white/25 transition"
      >
        <div className="flex items-center justify-between text-[10px] text-neutral-300 font-medium mb-1 gap-4">
          <span>{isBuilding ? 'Building' : 'Built'}</span>
          <span>{Math.round(buildProgress)}%</span>
        </div>
        <div className="w-full h-[3px] bg-white/15 rounded-full overflow-hidden">
          <div
            className="h-full bg-white rounded-full transition-all duration-200"
            style={{ width: `${buildProgress}%` }}
          />
        </div>
      </div>
    </div>
  );
};
