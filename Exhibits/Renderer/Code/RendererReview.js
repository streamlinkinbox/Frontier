/*==========================================================================================================================================
                                                            RENDERERREVIEW.JS
==========================================================================================================================================*/
// 🧩 Isolate live native-renderer evidence from the application Project Browser reference.
'use strict';

let Geometry = 0;
let Preview;
function ConstructRendererReview()
{
    const Mode = 3-Number(document.querySelector('#RevisionSelect').value);
    document.querySelector('#SpecimenTitle').textContent = Geometry === 0 ? 'Bracket' : 'Calibration sphere';
    document.querySelector('#Inspector').replaceChildren();
    Preview = new VulkanPreview(document.querySelector('#Capture'),Geometry,Mode);
    Preview.AttachControls(document.querySelector('#Inspector'));
}
document.querySelector('#RevisionSelect').onchange = ConstructRendererReview;
for (const Button of document.querySelectorAll('[data-preview]'))
    Button.onclick = () => { Geometry = Number(Button.dataset.preview); ConstructRendererReview(); };
document.querySelector('[data-collapse]').onclick = () => document.body.classList.toggle('left-collapsed');
ConstructRendererReview();
