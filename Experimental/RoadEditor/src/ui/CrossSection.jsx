import React from 'react';
import { LANE_TYPES } from '../road/profiles.js';

// Schematic cross-section of a profile. compact = tile art (no labels).
export function CrossSection({ lanes, compact = false, highlight = -1, onLane }) {
  const total = lanes.reduce((a, l) => a + l.width, 0) || 1;
  const W = 400, pad = compact ? 8 : 10, k = (W - pad * 2) / total;
  const H = compact ? 70 : 128, base = compact ? 50 : 84;
  const lvl = t => (LANE_TYPES[t].level === 'raised' ? 6 : LANE_TYPES[t].level === 'ground' ? -2 : 0);
  let x = pad;
  const parts = [];
  lanes.forEach((l, i) => {
    const t = LANE_TYPES[l.type], w = l.width * k, top = base - lvl(l.type) * (compact ? 1 : 1.4);
    const fill = l.type === 'median' && l.mat === 'concrete' ? '#8b877f' : t.color;
    parts.push(
      <g key={i} onClick={onLane ? () => onLane(i) : undefined} style={{ cursor: onLane ? 'pointer' : 'default' }}>
        <rect x={x} y={top} width={Math.max(w - 0.6, 0.5)} height={H - top - (compact ? 6 : 26)} fill={fill} opacity={highlight === -1 || highlight === i ? 1 : 0.45} rx={1} />
        {highlight === i && <rect x={x} y={top} width={w - 0.6} height={H - top - 26} fill="none" stroke="#4fd8e0" strokeWidth="1.5" rx={1} />}
        {t.traffic && l.dir !== undefined && w > 10 && (
          <path d={l.dir > 0 ? `M${x + w / 2} ${top + 10} l5 7 h-3 v8 h-4 v-8 h-3z` : `M${x + w / 2} ${top + 25} l5 -7 h-3 v-8 h-4 v8 h-3z`} fill="#e8e8e2" opacity=".85" transform={compact ? `translate(0 -4)` : undefined} />
        )}
        {(l.type === 'planter' || (l.type === 'median' && l.width > 2.5 && !l.mat)) && w > 8 && (
          <g><rect x={x + w / 2 - 1} y={top - (compact ? 14 : 22)} width="2" height={compact ? 14 : 22} fill="#6b5240" /><circle cx={x + w / 2} cy={top - (compact ? 18 : 28)} r={compact ? 7 : 10} fill="#4f7a38" /></g>
        )}
        {l.type === 'barrier' && <path d={`M${x} ${top} L${x + w * 0.3} ${top - (compact ? 9 : 14)} L${x + w * 0.7} ${top - (compact ? 9 : 14)} L${x + w} ${top}z`} fill="#c8c3ba" />}
        {l.type === 'tram' && <g fill="#9ea3a8"><rect x={x + w * 0.3} y={top - 1} width="2" height="2" /><rect x={x + w * 0.65} y={top - 1} width="2" height="2" /></g>}
        {l.type === 'bike' || l.type === 'cycletrack' ? w > 10 && <circle cx={x + w / 2} cy={top + 14} r="3" fill="none" stroke="#e8e8e2" strokeWidth="1" opacity=".8" /> : null}
        {l.type === 'parking' && w > 10 && <rect x={x + w * 0.2} y={top + 8} width={w * 0.6} height={compact ? 8 : 12} rx="3" fill="#6c7280" />}
        {!compact && w > 15 && <text x={x + w / 2} y={H - 9} textAnchor="middle" fontSize="8.5" fill="#7a7a7a">{l.width.toFixed(1)}</text>}
      </g>,
    );
    // lane separators
    if (i < lanes.length - 1) {
      const n = lanes[i + 1];
      if (LANE_TYPES[l.type].level === 'road' && LANE_TYPES[n.type].level === 'road' && l.type !== 'barrier' && n.type !== 'barrier') {
        const yellow = l.dir !== undefined && n.dir !== undefined && l.dir !== n.dir;
        parts.push(<line key={'s' + i} x1={x + w} x2={x + w} y1={top} y2={top + 3} stroke={yellow ? '#e0ad1c' : '#efeee8'} strokeWidth="1.6" />);
      }
      if (LANE_TYPES[l.type].level !== LANE_TYPES[n.type].level) parts.push(<rect key={'c' + i} x={x + w - 1} y={Math.min(top, base - lvl(n.type) * 1.4)} width="2" height={Math.abs(lvl(l.type) - lvl(n.type)) * (compact ? 1 : 1.4) + 1} fill="#d6d2ca" />);
    }
    x += w;
  });
  return (
    <svg viewBox={`0 0 ${W} ${H}`} preserveAspectRatio={compact ? 'xMidYMid meet' : 'xMidYMid meet'}>
      {!compact && <rect x="0" y={H - 24} width={W} height="1" fill="#2e2e2e" />}
      {parts}
      {!compact && <text x={W / 2} y={14} textAnchor="middle" fontSize="9" fill="#6a6a6a" letterSpacing="1.5">{total.toFixed(1)} M</text>}
    </svg>
  );
}
