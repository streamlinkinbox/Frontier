import React from 'react';
import {
  SlidersHorizontal,
  ShieldCheck,
  ShieldAlert,
  AlertTriangle,
  X,
  RotateCcw,
} from 'lucide-react';
import { GraphNodeData } from '../engine/graphEvaluator';
import { NodeResolutionReport, SDFDomainConfig } from '../engine/resolutionGuard';
import { SATMAP_PRESETS, getSatMapCssGradient } from '../engine/satmaps';

interface NodeInspectorProps {
  node: GraphNodeData | null;
  domain: SDFDomainConfig;
  report?: NodeResolutionReport;
  onUpdateParams: (nodeId: string, newParams: Record<string, any>) => void;
  onUpdateDomain: (newDomain: Partial<SDFDomainConfig>) => void;
  onClose: () => void;
}

interface SliderControlProps {
  label: string;
  value: number;
  min: number;
  max: number;
  step: number;
  unit?: string;
  onChange: (val: number) => void;
}

const SliderControl: React.FC<SliderControlProps> = ({
  label,
  value,
  min,
  max,
  step,
  unit = '',
  onChange,
}) => {
  const pct = Math.max(0, Math.min(100, ((value - min) / Math.max(1e-6, max - min)) * 100));
  return (
    <div className="mb-3">
      <div className="flex items-center justify-between text-[11px] mb-1">
        <span className="text-neutral-400 font-medium">{label}</span>
        <span className="text-neutral-200 font-mono">
          {step >= 1 ? Math.round(value) : value.toFixed(2)}
          {unit}
        </span>
      </div>
      <div className="relative h-1.5 bg-[#222] rounded-full overflow-hidden">
        <div
          className="absolute top-0 left-0 bottom-0 bg-white/85 rounded-full"
          style={{ width: `${pct}%` }}
        />
        <input
          type="range"
          min={min}
          max={max}
          step={step}
          value={value}
          onChange={(e) => onChange(parseFloat(e.target.value))}
          className="absolute inset-0 w-full h-full opacity-0 cursor-pointer"
        />
      </div>
    </div>
  );
};

export const NodeInspectorPanel: React.FC<NodeInspectorProps> = ({
  node,
  domain,
  report,
  onUpdateParams,
  onUpdateDomain,
  onClose,
}) => {
  if (!node) return null;

  const p = node.params;
  const setParam = (key: string, val: any) => {
    onUpdateParams(node.id, { ...p, [key]: val });
  };

  const voxelSize = domain.worldSize / Math.max(8, domain.sdfResolution);
  const nyquist = voxelSize * 2.0;

  return (
    <div
      className="absolute top-20 left-5 z-30 w-[305px] max-h-[calc(100%-140px)] rounded-[22px] bg-[#111111]/95 backdrop-blur-md border border-white/12 shadow-2xl flex flex-col overflow-hidden"
      onMouseDown={(e) => e.stopPropagation()}
      onWheel={(e) => e.stopPropagation()}
    >
      {/* Header */}
      <div className="px-4 py-3 border-b border-white/10 flex items-center justify-between">
        <div className="flex items-center gap-2">
          <SlidersHorizontal className="w-3.5 h-3.5 text-neutral-400" />
          <div>
            <div className="text-xs font-semibold text-white leading-tight">
              {node.title}
            </div>
            <div className="text-[10px] text-neutral-500">{node.subtitle}</div>
          </div>
        </div>
        <button
          onClick={onClose}
          className="w-6 h-6 rounded-full bg-white/5 hover:bg-white/15 flex items-center justify-center text-neutral-400 hover:text-white transition"
        >
          <X className="w-3.5 h-3.5" />
        </button>
      </div>

      {/* Scrollable Body */}
      <div className="p-4 overflow-y-auto space-y-4 custom-scrollbar">
        {/* Resolution Guard Status Banner */}
        <div
          className={`rounded-xl p-2.5 border text-[11px] ${
            report?.status === 'blocked_low_res'
              ? 'bg-red-950/50 border-red-500/40 text-red-200'
              : report?.status === 'clamped'
              ? 'bg-amber-950/40 border-amber-500/35 text-amber-200'
              : 'bg-emerald-950/30 border-emerald-500/25 text-emerald-200'
          }`}
        >
          <div className="flex items-center gap-1.5 font-semibold mb-0.5">
            {report?.status === 'blocked_low_res' ? (
              <ShieldAlert className="w-3.5 h-3.5 text-red-400 shrink-0" />
            ) : report?.status === 'clamped' ? (
              <AlertTriangle className="w-3.5 h-3.5 text-amber-400 shrink-0" />
            ) : (
              <ShieldCheck className="w-3.5 h-3.5 text-emerald-400 shrink-0" />
            )}
            <span>
              SDF Voxel Δx = {voxelSize.toFixed(1)}m (Nyquist {nyquist.toFixed(1)}m)
            </span>
          </div>
          <div className="text-[10px] opacity-85 leading-relaxed">
            {report
              ? report.message
              : `Domain ${domain.worldSize}m @ ${domain.sdfResolution}³ SDF grid.`}
          </div>
        </div>

        {/* START NODE: SDF Domain & Resolution Guard Controls */}
        {node.type === 'Start' && (
          <div>
            <div className="text-[10px] uppercase tracking-wider text-neutral-500 font-semibold mb-2.5">
              3D SDF Volume ({domain.sdfResolution}×{domain.sdfResolution}×{Math.round(domain.sdfResolution * 0.5)})
            </div>
            <SliderControl
              label="SDF Resolution (N × N × N/2)"
              value={domain.sdfResolution}
              min={32}
              max={256}
              step={8}
              onChange={(v) => {
                setParam('sdfResolution', v);
                onUpdateDomain({ sdfResolution: v });
              }}
            />
            <SliderControl
              label="Terrain World Size"
              value={domain.worldSize}
              min={256}
              max={1200}
              step={32}
              unit="m"
              onChange={(v) => {
                setParam('worldSize', v);
                onUpdateDomain({ worldSize: v });
              }}
            />
            <SliderControl
              label="Max Vertical Height"
              value={domain.maxHeight}
              min={100}
              max={360}
              step={10}
              unit="m"
              onChange={(v) => {
                setParam('maxHeight', v);
                onUpdateDomain({ maxHeight: v });
              }}
            />
            <div className="flex items-center justify-between pt-2 border-t border-white/10">
              <div>
                <div className="text-[11px] text-neutral-200 font-medium">
                  Strict Low-Res Erosion Guard
                </div>
                <div className="text-[10px] text-neutral-500">
                  Block erosion if SDF resolution is too coarse (prevents blurring)
                </div>
              </div>
              <button
                onClick={() => {
                  const next = !domain.strictLowResBlock;
                  setParam('strictLowResBlock', next);
                  onUpdateDomain({ strictLowResBlock: next });
                }}
                className={`w-9 h-5 rounded-full transition relative shrink-0 ${
                  domain.strictLowResBlock ? 'bg-white' : 'bg-neutral-700'
                }`}
              >
                <span
                  className={`block w-3.5 h-3.5 rounded-full transition transform ${
                    domain.strictLowResBlock
                      ? 'translate-x-4 bg-black'
                      : 'translate-x-1 bg-neutral-300'
                  }`}
                />
              </button>
            </div>

            <div className="flex items-center justify-between pt-2.5 mt-2.5 border-t border-white/10">
              <div>
                <div className="text-[11px] text-neutral-200 font-medium">
                  3D Geological Block Cutaway Skirt
                </div>
                <div className="text-[10px] text-neutral-500">
                  Close outer 4 walls with 3D underground strata cross-section
                </div>
              </div>
              <button
                onClick={() => {
                  const next = domain.blockSkirt === false;
                  setParam('blockSkirt', next);
                  onUpdateDomain({ blockSkirt: next });
                }}
                className={`w-9 h-5 rounded-full transition relative shrink-0 ${
                  domain.blockSkirt !== false ? 'bg-white' : 'bg-neutral-700'
                }`}
              >
                <span
                  className={`block w-3.5 h-3.5 rounded-full transition transform ${
                    domain.blockSkirt !== false
                      ? 'translate-x-4 bg-black'
                      : 'translate-x-1 bg-neutral-300'
                  }`}
                />
              </button>
            </div>

            <div className="mt-3 pt-2.5 border-t border-white/10 flex gap-1.5">
              <button
                onClick={() => {
                  setParam('sdfResolution', 32);
                  onUpdateDomain({ sdfResolution: 32 });
                }}
                className="flex-1 py-1.5 px-2 rounded-lg bg-white/5 hover:bg-white/10 text-[10px] text-neutral-300 border border-white/10"
              >
                Test Low Res (32)
              </button>
              <button
                onClick={() => {
                  setParam('sdfResolution', 256);
                  onUpdateDomain({ sdfResolution: 256 });
                }}
                className="flex-1 py-1.5 px-2 rounded-lg bg-white/10 hover:bg-white/15 text-[10px] text-white border border-white/20"
              >
                Default 256×256×128
              </button>
            </div>
          </div>
        )}

        {/* DEDICATED 3D SDF MONOLITH PIPELINE NODES */}
        {node.type === 'DesertPediment' && (
          <div>
            <SliderControl
              label="Feature Distance (Scale)"
              value={p.scale ?? 18}
              min={1}
              max={100}
              step={1}
              unit="km"
              onChange={(v) => setParam('scale', v)}
            />
            <SliderControl
              label="Pediment Base Elevation"
              value={p.baseElevation ?? 16}
              min={8}
              max={36}
              step={1}
              unit="m"
              onChange={(v) => setParam('baseElevation', v)}
            />
            <SliderControl
              label="Basin Swell Amplitude"
              value={p.undulationHeight ?? 9}
              min={2}
              max={24}
              step={1}
              unit="m"
              onChange={(v) => setParam('undulationHeight', v)}
            />
            <SliderControl
              label="Aeolian Sand Sheet Ripples"
              value={p.duneRippleStrength ?? 0.55}
              min={0}
              max={1.2}
              step={0.05}
              onChange={(v) => setParam('duneRippleStrength', v)}
            />
            <SliderControl
              label="Dry Arroyo Wash Depth"
              value={p.arroyoWashDepth ?? 3.5}
              min={0}
              max={10}
              step={0.5}
              unit="m"
              onChange={(v) => setParam('arroyoWashDepth', v)}
            />
            <SliderControl
              label="Seed"
              value={p.seed ?? 4217}
              min={1}
              max={9999}
              step={1}
              onChange={(v) => setParam('seed', v)}
            />
          </div>
        )}

        {node.type === 'SDFMonolithTowers' && (
          <div>
            <div className="mb-3">
              <div className="text-[11px] text-neutral-400 font-medium mb-1.5">
                3D Monolith Cluster Stamp
              </div>
              <div className="grid grid-cols-3 gap-1.5">
                {[
                  { id: 'monument_buttes', label: 'Monument Buttes' },
                  { id: 'mesa_plateau', label: 'Table Mesas' },
                  { id: 'needle_spires', label: 'Spire Needles' },
                ].map((opt) => (
                  <button
                    key={opt.id}
                    onClick={() => setParam('monolithPreset', opt.id)}
                    className={`py-1.5 px-2 rounded-lg text-[10px] font-medium border transition ${
                      (p.monolithPreset ?? 'monument_buttes') === opt.id
                        ? 'bg-white text-black border-white'
                        : 'bg-[#1a1a1a] text-neutral-300 border-white/10 hover:border-white/25'
                    }`}
                  >
                    {opt.label}
                  </button>
                ))}
              </div>
            </div>
            <SliderControl
              label="Monolith Summit Height"
              value={p.towerHeight ?? 174}
              min={80}
              max={220}
              step={2}
              unit="m"
              onChange={(v) => setParam('towerHeight', v)}
            />
            <SliderControl
              label="Monolith Footprint Scale"
              value={p.footprintScale ?? 1.0}
              min={0.6}
              max={1.45}
              step={0.05}
              unit="×"
              onChange={(v) => setParam('footprintScale', v)}
            />
            <SliderControl
              label="Pancake Caprock Crown"
              value={p.caprockCrown ?? 0.85}
              min={0}
              max={1.2}
              step={0.05}
              onChange={(v) => setParam('caprockCrown', v)}
            />
            <SliderControl
              label="Stepped Structural Benches"
              value={p.steppedBenchRatio ?? 0.72}
              min={0}
              max={1.2}
              step={0.05}
              onChange={(v) => setParam('steppedBenchRatio', v)}
            />
            <SliderControl
              label="Seed"
              value={p.seed ?? 4217}
              min={1}
              max={9999}
              step={1}
              onChange={(v) => setParam('seed', v)}
            />
          </div>
        )}

        {node.type === 'VerticalJointFissures' && (
          <div>
            <SliderControl
              label="Vertical Joint Fissure Intensity"
              value={p.fissureIntensity ?? 0.86}
              min={0}
              max={1.3}
              step={0.05}
              onChange={(v) => setParam('fissureIntensity', v)}
            />
            <SliderControl
              label="3D Chimney Recess Depth"
              value={p.chimneyDepth ?? 11.5}
              min={2}
              max={20}
              step={0.5}
              unit="m"
              onChange={(v) => setParam('chimneyDepth', v)}
            />
            <SliderControl
              label="Joint Spacing Wavelength"
              value={p.fissureSpacing ?? 22}
              min={12}
              max={42}
              step={1}
              unit="m"
              onChange={(v) => setParam('fissureSpacing', v)}
            />
            <SliderControl
              label="Horizontal Master Bedding Notches"
              value={p.beddingNotchStrength ?? 0.82}
              min={0}
              max={1.3}
              step={0.05}
              onChange={(v) => setParam('beddingNotchStrength', v)}
            />
            <SliderControl
              label="Vertical Columnar Fluting"
              value={p.columnFluting ?? 0.78}
              min={0}
              max={1.2}
              step={0.05}
              onChange={(v) => setParam('columnFluting', v)}
            />
            <SliderControl
              label="Seed"
              value={p.seed ?? 4217}
              min={1}
              max={9999}
              step={1}
              onChange={(v) => setParam('seed', v)}
            />
          </div>
        )}

        {node.type === 'BasalWindOverhangs' && (
          <div>
            <SliderControl
              label="3D Basal Undercut Depth"
              value={p.undercutDepth ?? 14.5}
              min={0}
              max={24}
              step={0.5}
              unit="m"
              onChange={(v) => setParam('undercutDepth', v)}
            />
            <SliderControl
              label="Protruding Cliff Brow Overhang"
              value={p.browOverhang ?? 8.5}
              min={0}
              max={16}
              step={0.5}
              unit="m"
              onChange={(v) => setParam('browOverhang', v)}
            />
            <SliderControl
              label="Alcove Roof Height"
              value={p.alcoveHeight ?? 38}
              min={18}
              max={68}
              step={2}
              unit="m"
              onChange={(v) => setParam('alcoveHeight', v)}
            />
            <SliderControl
              label="Windward Aspect Bias"
              value={p.directionalBias ?? 0.68}
              min={0}
              max={1.0}
              step={0.05}
              onChange={(v) => setParam('directionalBias', v)}
            />
            <SliderControl
              label="Prevailing Sand-Blast Azimuth"
              value={p.windAngleDeg ?? 35}
              min={0}
              max={360}
              step={5}
              unit="°"
              onChange={(v) => setParam('windAngleDeg', v)}
            />
          </div>
        )}

        {node.type === 'ConicalTalusSkirt' && (
          <div>
            <SliderControl
              label="Talus Apron Max Height"
              value={p.skirtHeight ?? 54}
              min={10}
              max={90}
              step={2}
              unit="m"
              onChange={(v) => setParam('skirtHeight', v)}
            />
            <SliderControl
              label="Scree Angle of Repose"
              value={p.reposeAngleDeg ?? 34}
              min={24}
              max={44}
              step={1}
              unit="°"
              onChange={(v) => setParam('reposeAngleDeg', v)}
            />
            <SliderControl
              label="Radial Gully Chute Carving"
              value={p.gullyChuteStrength ?? 0.78}
              min={0}
              max={1.2}
              step={0.05}
              onChange={(v) => setParam('gullyChuteStrength', v)}
            />
            <SliderControl
              label="Fallen Caprock Boulder Debris"
              value={p.boulderRoughness ?? 0.65}
              min={0}
              max={1.2}
              step={0.05}
              onChange={(v) => setParam('boulderRoughness', v)}
            />
          </div>
        )}

        {/* 3D MONOLITH & BUTTE STAMP GENERATORS */}
        {['MonumentButtes', 'MesaPlateauStamp', 'NeedleSpiresStamp'].includes(
          node.type
        ) && (
          <div>
            <SliderControl
              label="Feature Distance (Scale)"
              value={
                p.scale > 100
                  ? Math.min(100, Math.round(p.scale / 10))
                  : (p.scale ?? 16)
              }
              min={1}
              max={100}
              step={1}
              unit="km"
              onChange={(v) => setParam('scale', v)}
            />
            <SliderControl
              label="Monolith Tower Height"
              value={p.amplitude ?? 188}
              min={60}
              max={260}
              step={2}
              unit="m"
              onChange={(v) => setParam('amplitude', v)}
            />
            <SliderControl
              label="Monolith Cluster Density"
              value={p.butteDensity ?? 0.75}
              min={0.1}
              max={1.2}
              step={0.05}
              onChange={(v) => setParam('butteDensity', v)}
            />
            <SliderControl
              label="Vertical Joint Fissures & Chimneys"
              value={p.verticalFissures ?? 0.85}
              min={0}
              max={1.2}
              step={0.05}
              onChange={(v) => setParam('verticalFissures', v)}
            />
            <SliderControl
              label="3D Basal Wind-Sapped Overhang"
              value={p.basalOverhang3D ?? 0.88}
              min={0}
              max={1.3}
              step={0.05}
              onChange={(v) => setParam('basalOverhang3D', v)}
            />
            <SliderControl
              label="Pancake Caprock Crown"
              value={p.caprockCrown ?? 0.85}
              min={0}
              max={1.2}
              step={0.05}
              onChange={(v) => setParam('caprockCrown', v)}
            />
            <SliderControl
              label="Seed"
              value={p.seed ?? 4217}
              min={1}
              max={9999}
              step={1}
              onChange={(v) => setParam('seed', v)}
            />
          </div>
        )}

        {/* GENERATOR NODES */}
        {[
          'SimplexNoise',
          'PerlinNoise',
          'ValueNoise',
          'MultiFractal',
          'CellularVoronoi',
          'WhiteNoise',
        ].includes(node.type) && (
          <div>
            <SliderControl
              label="Feature Distance (Scale)"
              value={p.scale > 100 ? Math.min(100, Math.round(p.scale / 10)) : (p.scale ?? 16)}
              min={1}
              max={100}
              step={1}
              unit="km"
              onChange={(v) => setParam('scale', v)}
            />
            <SliderControl
              label="Elevation Amplitude"
              value={p.amplitude ?? 170}
              min={20}
              max={280}
              step={2}
              unit="m"
              onChange={(v) => setParam('amplitude', v)}
            />
            <SliderControl
              label="Fractal Octaves"
              value={p.octaves ?? 6}
              min={1}
              max={8}
              step={1}
              onChange={(v) => setParam('octaves', v)}
            />
            <SliderControl
              label="Ridge Sharpness"
              value={p.ridgeSharpness ?? 1.65}
              min={0.8}
              max={3.0}
              step={0.05}
              onChange={(v) => setParam('ridgeSharpness', v)}
            />
            <SliderControl
              label="Domain Warp"
              value={p.warpStrength ?? 24}
              min={0}
              max={80}
              step={2}
              unit="m"
              onChange={(v) => setParam('warpStrength', v)}
            />
            <SliderControl
              label="Basin Valley Relief"
              value={p.valleyFloor ?? 0.35}
              min={0}
              max={0.85}
              step={0.05}
              onChange={(v) => setParam('valleyFloor', v)}
            />
            <SliderControl
              label="Seed"
              value={p.seed ?? 4217}
              min={1}
              max={9999}
              step={1}
              onChange={(v) => setParam('seed', v)}
            />
          </div>
        )}

        {/* CLIFF & STRATA NODE */}
        {node.type === 'CliffStrata' && (
          <div>
            <SliderControl
              label="Cliff Escarpment Steepness"
              value={p.cliffSteepness ?? 0.8}
              min={0.1}
              max={1.0}
              step={0.02}
              onChange={(v) => setParam('cliffSteepness', v)}
            />
            <SliderControl
              label="Sedimentary Rock Beds"
              value={p.strataFrequency ?? 9}
              min={3}
              max={18}
              step={1}
              onChange={(v) => setParam('strataFrequency', v)}
            />
            <SliderControl
              label="Differential Bed Hardness"
              value={p.strataStrength ?? 0.65}
              min={0}
              max={1.0}
              step={0.02}
              onChange={(v) => setParam('strataStrength', v)}
            />
            <SliderControl
              label="Talus Repose Angle"
              value={p.talusReposeAngle ?? 34}
              min={24}
              max={45}
              step={1}
              unit="°"
              onChange={(v) => setParam('talusReposeAngle', v)}
            />
            <SliderControl
              label="Talus Scree Accumulation"
              value={p.talusRate ?? 0.75}
              min={0}
              max={1.5}
              step={0.05}
              onChange={(v) => setParam('talusRate', v)}
            />
            <SliderControl
              label="3D SDF Shale Overhang Undercut"
              value={p.undercut3D ?? 0.65}
              min={0}
              max={1.2}
              step={0.05}
              onChange={(v) => setParam('undercut3D', v)}
            />
            <div className="pt-2 mt-2 border-t border-white/10">
              <div className="text-[10px] uppercase tracking-wider text-neutral-500 font-semibold mb-2">
                3D Tectonic Fault Plane
              </div>
              <SliderControl
                label="Fault Vertical Offset"
                value={p.faultOffset ?? 14}
                min={-32}
                max={32}
                step={1}
                unit="m"
                onChange={(v) => setParam('faultOffset', v)}
              />
              <SliderControl
                label="Fault Strike Azimuth"
                value={p.faultAngle ?? 32}
                min={0}
                max={180}
                step={2}
                unit="°"
                onChange={(v) => setParam('faultAngle', v)}
              />
              <SliderControl
                label="Fault Plane 3D Dip"
                value={p.faultDip ?? 0.28}
                min={-0.75}
                max={0.75}
                step={0.04}
                onChange={(v) => setParam('faultDip', v)}
              />
            </div>
          </div>
        )}

        {/* HYDRAULIC & RIVERS NODE */}
        {node.type === 'HydraulicRivers' && (
          <div>
            <SliderControl
              label="River Gorge Incision Depth"
              value={p.riverCarveDepth ?? 26}
              min={4}
              max={55}
              step={1}
              unit="m"
              onChange={(v) => setParam('riverCarveDepth', v)}
            />
            <SliderControl
              label="River Channel Width"
              value={p.channelWidth ?? 16}
              min={6}
              max={36}
              step={1}
              unit="m"
              onChange={(v) => setParam('channelWidth', v)}
            />
            <SliderControl
              label="Catchment River Threshold"
              value={p.channelThreshold ?? 0.055}
              min={0.02}
              max={0.18}
              step={0.005}
              onChange={(v) => setParam('channelThreshold', v)}
            />
            <SliderControl
              label="Basin Rainfall Runoff"
              value={p.rainIntensity ?? 1.2}
              min={0.3}
              max={2.5}
              step={0.05}
              onChange={(v) => setParam('rainIntensity', v)}
            />
            <SliderControl
              label="3D SDF River Cutbank Overhang"
              value={p.riverBankUndercut3D ?? 0.7}
              min={0}
              max={1.2}
              step={0.05}
              onChange={(v) => setParam('riverBankUndercut3D', v)}
            />
          </div>
        )}

        {/* ALLUVIAL SEDIMENT NODE */}
        {node.type === 'AlluvialSediment' && (
          <div>
            <SliderControl
              label="Alluvial Fan Deposition"
              value={p.depositionStrength ?? 1.25}
              min={0.1}
              max={2.5}
              step={0.05}
              onChange={(v) => setParam('depositionStrength', v)}
            />
            <SliderControl
              label="Critical Deposition Slope"
              value={p.criticalSlope ?? 0.26}
              min={0.08}
              max={0.5}
              step={0.01}
              onChange={(v) => setParam('criticalSlope', v)}
            />
            <SliderControl
              label="Radial Fan Spread"
              value={p.fanSpread ?? 0.78}
              min={0.1}
              max={1.0}
              step={0.02}
              onChange={(v) => setParam('fanSpread', v)}
            />
          </div>
        )}

        {/* WIND (AEOLIAN) NODE */}
        {node.type === 'WindErosion' && (
          <div>
            <SliderControl
              label="Prevailing Wind Azimuth"
              value={p.windAngleDeg ?? 38}
              min={0}
              max={360}
              step={5}
              unit="°"
              onChange={(v) => setParam('windAngleDeg', v)}
            />
            <SliderControl
              label="Windward Yardang Abrasion"
              value={p.abrasionStrength ?? 0.55}
              min={0}
              max={1.2}
              step={0.05}
              onChange={(v) => setParam('abrasionStrength', v)}
            />
            <SliderControl
              label="Leeward Dune Deposition"
              value={p.duneStrength ?? 0.6}
              min={0}
              max={1.2}
              step={0.05}
              onChange={(v) => setParam('duneStrength', v)}
            />
            <SliderControl
              label="Dune Ripple Wavelength"
              value={p.duneRippleScale ?? 24}
              min={12}
              max={64}
              step={2}
              unit="m"
              onChange={(v) => setParam('duneRippleScale', v)}
            />
          </div>
        )}

        {/* 3D SDF KARST CAVES NODE */}
        {node.type === 'SDFKarstCaves' && (
          <div>
            <SliderControl
              label="3D Cave Tunnel Radius"
              value={p.tunnelRadius ?? 14}
              min={6}
              max={26}
              step={0.5}
              unit="m"
              onChange={(v) => setParam('tunnelRadius', v)}
            />
            <SliderControl
              label="Karst Network Scale"
              value={p.caveScale ?? 105}
              min={55}
              max={220}
              step={5}
              unit="m"
              onChange={(v) => setParam('caveScale', v)}
            />
            <SliderControl
              label="Cavern Chamber Expansion"
              value={p.cavernStrength ?? 0.65}
              min={0}
              max={1.2}
              step={0.05}
              onChange={(v) => setParam('cavernStrength', v)}
            />
            <SliderControl
              label="Cliff Arch & Overhang Carve"
              value={p.overhangUndercut ?? 0.72}
              min={0}
              max={1.2}
              step={0.05}
              onChange={(v) => setParam('overhangUndercut', v)}
            />
            <SliderControl
              label="Max Cave Elevation Band"
              value={p.elevationMax ?? 0.72}
              min={0.25}
              max={0.9}
              step={0.02}
              onChange={(v) => setParam('elevationMax', v)}
            />
            <SliderControl
              label="Cave Seed"
              value={p.seed ?? 6073}
              min={1}
              max={9999}
              step={1}
              onChange={(v) => setParam('seed', v)}
            />
          </div>
        )}

        {/* GAEA SATMAP TEXTURING NODE */}
        {node.type === 'SatMapTexture' && (
          <div className="space-y-3">
            <div className="text-[10px] uppercase tracking-wider text-neutral-500 font-semibold">
              Gaea Satellite Gradient Presets ({SATMAP_PRESETS.length})
            </div>
            <div className="space-y-1.5 max-h-56 overflow-y-auto pr-1">
              {SATMAP_PRESETS.map((preset) => {
                const active = p.presetId === preset.id;
                return (
                  <button
                    key={preset.id}
                    onClick={() => setParam('presetId', preset.id)}
                    className={`w-full text-left p-2 rounded-xl border transition ${
                      active
                        ? 'bg-white/10 border-white/60'
                        : 'bg-[#181818] border-white/5 hover:border-white/20'
                    }`}
                  >
                    <div className="flex items-center justify-between text-[11px] text-white font-medium">
                      <span className="truncate">{preset.name}</span>
                    </div>
                    <div className="text-[9px] text-neutral-400 mb-1 truncate">
                      {preset.region}
                    </div>
                    <div
                      className="w-full h-2.5 rounded-full border border-black/40"
                      style={{ background: getSatMapCssGradient(preset) }}
                    />
                  </button>
                );
              })}
            </div>

            <SliderControl
              label="Cliff Strata Band Contrast"
              value={p.strataContrast ?? 0.8}
              min={0}
              max={1.2}
              step={0.05}
              onChange={(v) => setParam('strataContrast', v)}
            />
            <SliderControl
              label="River Channel Tint Strength"
              value={p.riverHighlight ?? 1.0}
              min={0}
              max={1.5}
              step={0.05}
              onChange={(v) => setParam('riverHighlight', v)}
            />
            <SliderControl
              label="Alluvial & Talus Mask Tint"
              value={p.alluvialTintStrength ?? 0.88}
              min={0}
              max={1.2}
              step={0.05}
              onChange={(v) => setParam('alluvialTintStrength', v)}
            />
            <SliderControl
              label="Satellite Grain Jitter"
              value={p.jitter ?? 0.35}
              min={0}
              max={1.0}
              step={0.05}
              onChange={(v) => setParam('jitter', v)}
            />
          </div>
        )}
      </div>
    </div>
  );
};
