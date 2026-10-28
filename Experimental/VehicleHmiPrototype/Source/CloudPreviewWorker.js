import {CloudDensityImages} from './CloudField.js';
self.onmessage=({data:{Settings,Local,Aspect,Key}})=>{const R=CloudDensityImages(Settings,Local,Aspect);self.postMessage({...R,Key},[R.Top.buffer,R.Side.buffer]);};
