import {RenderFlareImage} from './NativeLensFlareKernel.js';
onmessage=E=>{const {Settings,Key}=E.data;const Pixels=RenderFlareImage(Settings);postMessage({Pixels,Key},[Pixels.buffer]);};
