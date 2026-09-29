import React, { useState, useEffect, useCallback, useRef } from 'react';
import {
  GraphNodeData,
  GraphEdge,
  CatalogItem,
  GraphPresetId,
  getInitialGraph,
  evaluateTerrainGraph,
} from './engine/graphEvaluator';
import { SDFDomainConfig, NodeResolutionReport } from './engine/resolutionGuard';
import { ExtractedSDFMesh } from './engine/marchingCubes';
import { SDFTerrainVolume } from './engine/terrainState';
import { TerrainViewport3D } from './components/TerrainViewport3D';
import { NodeEditorPanel } from './components/NodeEditorPanel';

export interface SavedCustomGraph {
  id: string;
  name: string;
  savedAt: string;
  nodes: GraphNodeData[];
  edges: GraphEdge[];
}

const SAVED_GRAPHS_STORAGE_KEY = 'frontier_sdf_saved_quickstarts_v1';

export const App: React.FC = () => {
  const initial = useRef(getInitialGraph('double_arch_entrada'));
  const [nodes, setNodes] = useState<GraphNodeData[]>(initial.current.nodes);
  const [edges, setEdges] = useState<GraphEdge[]>(initial.current.edges);
  const [selectedNodeId, setSelectedNodeId] = useState<string | null>('node-start');
  const [previewNodeId, setPreviewNodeId] = useState<string | null>(null);
  const [activePresetId, setActivePresetId] = useState<string>('double_arch_entrada');

  const [savedGraphs, setSavedGraphs] = useState<SavedCustomGraph[]>(() => {
    try {
      const raw = localStorage.getItem(SAVED_GRAPHS_STORAGE_KEY);
      return raw ? JSON.parse(raw) : [];
    } catch {
      return [];
    }
  });

  const [domain, setDomain] = useState<SDFDomainConfig>({
    worldSize: 512,
    maxHeight: 245,
    sdfResolution: 256,
    strictLowResBlock: true,
    blockSkirt: true,
  });

  const [meshData, setMeshData] = useState<ExtractedSDFMesh | null>(null);
  const [volume, setVolume] = useState<SDFTerrainVolume | null>(null);
  const [reports, setReports] = useState<Record<string, NodeResolutionReport>>({});
  const [buildProgress, setBuildProgress] = useState<number>(100);
  const [isBuilding, setIsBuilding] = useState<boolean>(false);
  const [buildTimeMs, setBuildTimeMs] = useState<number>(0);

  const [wireframe, setWireframe] = useState<boolean>(false);
  const [showRiverWater, setShowRiverWater] = useState<boolean>(true);

  const runGraphEvaluation = useCallback(
    (
      currNodes: GraphNodeData[],
      currEdges: GraphEdge[],
      currPreviewId: string | null
    ) => {
      setIsBuilding(true);
      setBuildProgress(35);

      requestAnimationFrame(() => {
        const res = evaluateTerrainGraph(currNodes, currEdges, currPreviewId);
        setMeshData(res.mesh);
        setVolume(res.volume);
        setReports(res.volume.reports);
        setBuildTimeMs(res.buildTimeMs);
        setBuildProgress(100);
        setIsBuilding(false);
      });
    },
    []
  );

  // Initial evaluation on mount & whenever graph structure or parameters change
  const engineVersion = 13; // Adds 3D SDF Natural Arches & Interactive Bezier Arch Splines (Double Arch preset)
  useEffect(() => {
    const fresh = getInitialGraph('double_arch_entrada');
    setNodes(fresh.nodes);
    setEdges(fresh.edges);
    setSelectedNodeId('node-start');
    setPreviewNodeId(null);
    setActivePresetId('double_arch_entrada');
    setDomain({
      worldSize: 512,
      maxHeight: 245,
      sdfResolution: 256,
      strictLowResBlock: true,
      blockSkirt: true,
    });
  }, [engineVersion]);

  const handleLoadGraphPreset = (preset: GraphPresetId) => {
    const fresh = getInitialGraph(preset);
    setNodes(fresh.nodes);
    setEdges(fresh.edges);
    setSelectedNodeId('node-start');
    setPreviewNodeId(null);
    setActivePresetId(preset);
    const startParams = fresh.nodes.find((n) => n.type === 'Start')?.params;
    if (startParams) {
      setDomain({
        worldSize: Number(startParams.worldSize ?? 512),
        maxHeight: Number(startParams.maxHeight ?? 210),
        sdfResolution: Number(startParams.sdfResolution ?? 256),
        strictLowResBlock: Boolean(startParams.strictLowResBlock ?? true),
        blockSkirt: Boolean(startParams.blockSkirt ?? true),
      });
    }
  };

  const handleSaveCurrentGraph = (customName?: string) => {
    const label =
      customName?.trim() ||
      `Custom Graph #${savedGraphs.length + 1} (${nodes.length} nodes)`;
    const entry: SavedCustomGraph = {
      id: `saved-${Date.now()}`,
      name: label,
      savedAt: new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' }),
      nodes: JSON.parse(JSON.stringify(nodes)),
      edges: JSON.parse(JSON.stringify(edges)),
    };
    const next = [entry, ...savedGraphs];
    setSavedGraphs(next);
    setActivePresetId(entry.id);
    try {
      localStorage.setItem(SAVED_GRAPHS_STORAGE_KEY, JSON.stringify(next));
    } catch {
      // ignore storage quota errors
    }
  };

  const handleLoadSavedGraph = (id: string) => {
    const found = savedGraphs.find((g) => g.id === id);
    if (!found) return;
    const clonedNodes: GraphNodeData[] = JSON.parse(JSON.stringify(found.nodes));
    const clonedEdges: GraphEdge[] = JSON.parse(JSON.stringify(found.edges));
    setNodes(clonedNodes);
    setEdges(clonedEdges);
    setSelectedNodeId(clonedNodes[1]?.id || clonedNodes[0]?.id || null);
    setPreviewNodeId(null);
    setActivePresetId(found.id);
  };

  const handleDeleteSavedGraph = (id: string) => {
    const next = savedGraphs.filter((g) => g.id !== id);
    setSavedGraphs(next);
    try {
      localStorage.setItem(SAVED_GRAPHS_STORAGE_KEY, JSON.stringify(next));
    } catch {
      // ignore
    }
  };

  useEffect(() => {
    const timer = setTimeout(() => {
      runGraphEvaluation(nodes, edges, previewNodeId);
    }, 20);
    return () => clearTimeout(timer);
  }, [nodes, edges, previewNodeId, runGraphEvaluation, engineVersion]);

  const handleMoveNode = (id: string, x: number, y: number) => {
    setNodes((prev) =>
      prev.map((n) => (n.id === id ? { ...n, x, y } : n))
    );
  };

  const handleUpdateNodeMeta = (id: string, patch: Partial<GraphNodeData>) => {
    setNodes((prev) =>
      prev.map((n) => (n.id === id ? { ...n, ...patch } : n))
    );
  };

  const handleUpdateNodeParams = (id: string, newParams: Record<string, any>) => {
    setNodes((prev) =>
      prev.map((n) => (n.id === id ? { ...n, params: newParams } : n))
    );
  };

  const handleUpdateDomain = (patch: Partial<SDFDomainConfig>) => {
    setDomain((prev) => {
      const next = { ...prev, ...patch };
      setNodes((nds) =>
        nds.map((n) =>
          n.type === 'Start' ? { ...n, params: { ...n.params, ...next } } : n
        )
      );
      return next;
    });
  };

  const handleAddNodeFromCatalog = (item: CatalogItem) => {
    const newId = `node-${Date.now()}`;
    // Find current tail node or selected node to attach after
    let anchorNode = nodes.find((n) => n.id === selectedNodeId);
    if (!anchorNode) {
      anchorNode = nodes[nodes.length - 1] || nodes[0];
    }

    const newNode: GraphNodeData = {
      id: newId,
      type: item.type,
      title: item.title,
      subtitle: item.subtitle,
      category: item.category,
      x: (anchorNode?.x ?? 120) + 265,
      y: anchorNode?.y ?? 230,
      params: { ...item.defaultParams },
    };

    setNodes((prev) => [...prev, newNode]);

    if (anchorNode) {
      // Splice into chain after anchorNode
      const existingOutEdge = edges.find((e) => e.source === anchorNode!.id);
      if (existingOutEdge) {
        setEdges((prev) => [
          ...prev.filter((e) => e.id !== existingOutEdge.id),
          { id: `e-${Date.now()}-1`, source: anchorNode!.id, target: newId },
          {
            id: `e-${Date.now()}-2`,
            source: newId,
            target: existingOutEdge.target,
          },
        ]);
      } else {
        setEdges((prev) => [
          ...prev,
          { id: `e-${Date.now()}`, source: anchorNode!.id, target: newId },
        ]);
      }
    }

    setSelectedNodeId(newId);
  };

  const handleDeleteNode = (id: string) => {
    const target = nodes.find((n) => n.id === id);
    if (!target || target.type === 'Start') return;

    const inEdge = edges.find((e) => e.target === id);
    const outEdge = edges.find((e) => e.source === id);

    setNodes((prev) => prev.filter((n) => n.id !== id));
    setEdges((prev) => {
      const remaining = prev.filter((e) => e.source !== id && e.target !== id);
      if (inEdge && outEdge) {
        remaining.push({
          id: `e-${Date.now()}`,
          source: inEdge.source,
          target: outEdge.target,
        });
      }
      return remaining;
    });

    if (selectedNodeId === id) setSelectedNodeId('node-start');
    if (previewNodeId === id) setPreviewNodeId(null);
  };

  const handleConnectNodes = (sourceId: string, targetId: string) => {
    if (sourceId === targetId) return;
    setEdges((prev) => {
      // Single-chain flow: replace existing outgoing edge from sourceId or incoming to targetId
      const filtered = prev.filter(
        (e) => e.source !== sourceId && e.target !== targetId
      );
      return [
        ...filtered,
        { id: `e-${Date.now()}`, source: sourceId, target: targetId },
      ];
    });
  };

  const handleDisconnectEdge = (edgeId: string) => {
    setEdges((prev) => prev.filter((e) => e.id !== edgeId));
  };

  const handleTogglePreviewNode = (id: string) => {
    setPreviewNodeId((prev) => (prev === id ? null : id));
  };

  return (
    <div className="w-screen h-screen flex overflow-hidden bg-[#0b0b0b]">
      {/* LEFT SIDE: CLEAN 3D SDF VIEWPORT (No toolbars inside viewport as requested) */}
      <div className="w-1/2 h-full relative border-r border-black">
        <TerrainViewport3D
          meshData={meshData}
          volume={volume}
          wireframe={wireframe}
          showRiverWater={showRiverWater}
          selectedNode={nodes.find((n) => n.id === selectedNodeId) || null}
          onUpdateArchSplines={(nodeId, nextSplines) => {
            const target = nodes.find((n) => n.id === nodeId);
            if (target) {
              handleUpdateNodeParams(nodeId, {
                ...target.params,
                archSplines: nextSplines,
              });
            }
          }}
        />
      </div>

      {/* RIGHT SIDE: NODE EDITOR (Matching reference screenshots) */}
      <div className="w-1/2 h-full relative">
        <NodeEditorPanel
          nodes={nodes}
          edges={edges}
          selectedNodeId={selectedNodeId}
          previewNodeId={previewNodeId}
          domain={domain}
          reports={reports}
          buildProgress={buildProgress}
          isBuilding={isBuilding}
          buildTimeMs={buildTimeMs}
          wireframe={wireframe}
          showRiverWater={showRiverWater}
          onSelectNode={setSelectedNodeId}
          onTogglePreviewNode={handleTogglePreviewNode}
          onMoveNode={handleMoveNode}
          onUpdateNodeMeta={handleUpdateNodeMeta}
          onUpdateNodeParams={handleUpdateNodeParams}
          onUpdateDomain={handleUpdateDomain}
          onAddNodeFromCatalog={handleAddNodeFromCatalog}
          onDeleteNode={handleDeleteNode}
          onConnectNodes={handleConnectNodes}
          onDisconnectEdge={handleDisconnectEdge}
          onRunBuild={() => runGraphEvaluation(nodes, edges, previewNodeId)}
          onToggleWireframe={() => setWireframe((v) => !v)}
          onToggleRiverWater={() => setShowRiverWater((v) => !v)}
          onLoadGraphPreset={handleLoadGraphPreset}
          activePresetId={activePresetId}
          savedGraphs={savedGraphs}
          onSaveCurrentGraph={handleSaveCurrentGraph}
          onLoadSavedGraph={handleLoadSavedGraph}
          onDeleteSavedGraph={handleDeleteSavedGraph}
        />
      </div>
    </div>
  );
};
