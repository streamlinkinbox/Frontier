import React from 'react';
import { Outliner } from './ui/Outliner.jsx';
import { Viewport } from './ui/Viewport.jsx';
import { Inspector } from './ui/Inspector.jsx';
import { RoadLibrary } from './ui/RoadLibrary.jsx';

export default function App() {
  return (
    <div className="shell">
      <Outliner />
      <Viewport />
      <Inspector />
      <RoadLibrary />
    </div>
  );
}
