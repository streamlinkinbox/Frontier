import {ConstructMaterialSpecification} from '../../../Experimental/SlateReference/A/Exhibits/VehicleProvingGrounds/Source/MaterialSpecification.js';
import {mkdirSync,writeFileSync} from 'node:fs';
mkdirSync('_ClaudeScratch/vehicle-reference',{recursive:true});
const Materials=ConstructMaterialSpecification();
writeFileSync('_ClaudeScratch/vehicle-reference/Materials.txt',Materials.map(Material=>[...Material.Albedo,Material.Roughness,Material.Metalness,...Material.Emission].join(' ')).join('\n')+'\n');
console.log(`Exported ${Materials.length} original material records`);
