import { useCallback, useRef, useState } from 'react';
import Viewer, { ViewerApi, ViewOpts } from './components/Viewer';
import Panel from './components/Panel';
import { DEFAULT_PARAMS, PRESETS, REGION_CURVES, Region, RoofParams } from './lib/types';
import { Check, RoofStats } from './lib/buildRoof';

export default function App() {
  const [params, setParams] = useState<RoofParams>(DEFAULT_PARAMS);
  const [view, setView] = useState<ViewOpts>({ wireframe: false, xray: false, autorotate: false });
  const [checks, setChecks] = useState<Check[]>([]);
  const [stats, setStats] = useState<RoofStats | null>(null);
  const viewerApi = useRef<ViewerApi>(null);

  const patch = useCallback((p: Partial<RoofParams>) => setParams((prev) => ({ ...prev, ...p })), []);
  const patchView = useCallback((p: Partial<ViewOpts>) => setView((prev) => ({ ...prev, ...p })), []);
  const onReport = useCallback((c: Check[], s: RoofStats) => { setChecks(c); setStats(s); }, []);

  const applyPreset = useCallback((id: string) => {
    const pr = PRESETS.find((x) => x.id === id);
    if (pr) setParams((prev) => ({ ...prev, ...pr.patch }));
  }, []);

  const applyRegion = useCallback((r: Region) => {
    setParams((prev) => ({ ...prev, region: r, ...REGION_CURVES[r] }));
  }, []);

  const exportJSON = useCallback(() => {
    const blob = new Blob([JSON.stringify(params, null, 2)], { type: 'application/json' });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = `frontier-${params.style}-params.json`;
    a.click();
    URL.revokeObjectURL(a.href);
  }, [params]);

  return (
    <div className="app">
      <header className="topbar">
        <div className="brand">
          <svg viewBox="0 0 32 32" width="26" height="26" aria-hidden>
            <path d="M4 9 Q16 4 28 9 L26 12 Q16 8 6 12 Z" fill="#e05a1f" />
            <rect x="8" y="13" width="3" height="12" fill="#e8e0d0" />
            <rect x="21" y="13" width="3" height="12" fill="#e8e0d0" />
            <rect x="6" y="17" width="20" height="2.4" fill="#e05a1f" />
          </svg>
          <div>
            <h1>FRONTIER <span>Procedural East-Asian Building Generator</span></h1>
          </div>
        </div>
        <div className="phase-badge">Phase 1 · Roofs 屋根</div>
      </header>
      <main className="layout">
        <Viewer
          ref={viewerApi}
          params={params}
          view={view}
          onView={patchView}
          checks={checks}
          stats={stats}
          onReport={onReport}
        />
        <aside className="sidebar">
          <Panel
            params={params}
            onChange={patch}
            onPreset={applyPreset}
            onRegion={applyRegion}
            checks={checks}
            stats={stats}
            onExportGLB={() => viewerApi.current?.exportGLB()}
            onExportJSON={exportJSON}
          />
        </aside>
      </main>
    </div>
  );
}
