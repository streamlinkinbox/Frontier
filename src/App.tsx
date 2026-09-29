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

export const App: React.FC = () => {
  const initial = useRef(getInitialGraph());
  const [nodes, setNodes] = useState<GraphNodeData[]>(initial.current.nodes);
  const [edges, setEdges] = useState<GraphEdge[]>(initial.current.edges);
  const [selectedNodeId, setSelectedNodeId] = useState<string | null>('node-start');
  const [previewNodeId, setPreviewNodeId] = useState<string | null>(null);

  const [domain, setDomain] = useState<SDFDomainConfig>({
    worldSize: 512,
    maxHeight: 210,
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
  const engineVersion = 8; // Upgrades active graph to dedicated 8-node 3D CSG SDF Monolith pipeline
  useEffect(() => {
    const fresh = getInitialGraph('monument_valley');
    setNodes(fresh.nodes);
    setEdges(fresh.edges);
    setSelectedNodeId('node-monoliths');
    setPreviewNodeId(null);
    setDomain({
      worldSize: 512,
      maxHeight: 210,
      sdfResolution: 256,
      strictLowResBlock: true,
      blockSkirt: true,
    });
  }, [engineVersion]);

  const handleLoadGraphPreset = (preset: GraphPresetId) => {
    const fresh = getInitialGraph(preset);
    setNodes(fresh.nodes);
    setEdges(fresh.edges);
    setSelectedNodeId(preset === 'monument_valley' ? 'node-monoliths' : 'node-multifractal');
    setPreviewNodeId(null);
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
        />
      </div>
    </div>
  );
};
