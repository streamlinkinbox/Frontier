/*============================================================================================================================================
                                                    VOLUMECOMPARISON.JS
============================================================================================================================================*/
/* 🧩 Compare native anisotropic cube distances with the browser SDF implementation. */
global.window = global;
require('../../../Experimental/BakingWorkspace/GeometryAndBakers.js');
const fs = require('fs');
const fixture = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
const mesh = { name: 'Cube', positions: fixture.positions, indices: fixture.indices,
  bounds: { min: [-1, -1, -1], max: [1, 1, 1], diag: Math.sqrt(12) } };
FrontierBakingCore.bakeVolumeSdf(mesh, { nx: 24, ny: 18, nz: 20, padding: 0.14, signMode: 'signed' }).then(volume => {
  let largest = 0;
  for (let index = 0; index < fixture.distances.length; index++)
    largest = Math.max(largest, Math.abs(fixture.distances[index] - volume.distances[index]));
  console.log(`${fixture.distances.length} voxels; maximum difference ${largest}`);
  if (largest > 1e-5) process.exitCode = 1;
}).catch(error => { console.error(error); process.exitCode = 1; });
