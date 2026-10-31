/*==========================================================================================================================================
                                                            VULKANPREVIEW.JS
==========================================================================================================================================*/
// 🧩 Display actual offscreen Vulkan readbacks inside the earlier browser; no WebGL substitution or editor-integration claim.
'use strict';

class VulkanPreview
{
    static Evidence(Geometry,Mode)
    {
        const Names = Geometry === 0 ? ['CadOrthographic','MatCapBracket','PreviewPbrBracket','MaterialBrushedCopperBracket'] :
            ['PreviewCadCalibration','PreviewMatCapCalibration','PreviewPbrCalibration','MaterialBrushedCopperSphere'];
        return '/renderer/evidence/'+Names[Mode]+'.png';
    }

    constructor(Surface,Geometry,Mode)
    {
        this.Surface = Surface;
        this.Geometry = Geometry;
        this.Mode = Mode;
        this.Revision = 0;
        this.Busy = false;
        this.Pending = false;
        Surface.innerHTML = `<div class="vulkan-surface"><img class="vulkan-capture" alt="Actual Vulkan device readback"><span class="vulkan-status" role="status" aria-live="polite">Saved Vulkan capture · connecting…</span></div>`;
        this.Image = Surface.querySelector('img');
        this.Image.src = VulkanPreview.Evidence(Geometry,Mode);
        this.Status = Surface.querySelector('[role=status]');
    }

    AttachControls(Surface)
    {
        if (this.Form?.isConnected && Surface.contains(this.Form)) return;
        Surface.innerHTML = `<h3>Surface scattering</h3><p>Software Vulkan · offscreen readback. Not the native editor viewport.</p>
            <form class="vulkan-controls">
            <label>Roughness <output>.32</output><input name="Roughness" aria-label="Roughness" type="range" min=".045" max="1" step=".005" value=".32"></label>
            <label>Metallic <output>.8</output><input name="Metallic" aria-label="Metallic" type="range" min="0" max="1" step=".01" value=".8"></label>
            <label>Coating coverage <output>.75</output><input name="Coating" aria-label="Coating coverage" type="range" min="0" max="1" step=".01" value=".75"></label>
            <label>Exposure <output>1</output><input name="Exposure" aria-label="Exposure" type="range" min="0" max="4" step=".05" value="1"></label>
            </form><div class="crossline"></div><div class="label">SUPPORTED SCATTERING</div>
            <ol class="scattering-sequence"><li>Opaque GGX + diffuse stratum</li><li>Dielectric coatings · Fresnel + Beer absorption</li></ol>
            <p>Coatings attenuate evaluated scattering beneath; they do not blend roughness parameters. Bounded preview sky quadrature, not GI or converged HDR lighting.</p>
            <p class="vulkan-statistics" role="status"></p><div class="crossline"></div>
            <p>Same resident bracket geometry as CAD and MatCap. The sphere is analytical normal-calibration geometry, not another imported CAD document.</p>`;
        this.Form = Surface.querySelector('form');
        this.Statistics = Surface.querySelector('.vulkan-statistics');
        this.Form.onsubmit = Event => Event.preventDefault();
        for (const Input of this.Form.querySelectorAll('input'))
        {
            Input.parentElement.hidden = this.Mode === 3 && Input.name !== 'Exposure';
            Input.disabled = this.Mode !== 2 && !(this.Mode === 3 && Input.name === 'Exposure');
            Input.oninput = () => Input.parentElement.querySelector('output').textContent = Number(Input.value).toFixed(2);
            Input.onchange = () => this.Refresh();
        }
        if (this.Mode === 3)
        {
            this.AttachMaterialAuthoring(Surface);
        }
        else this.Refresh();
    }

    async AttachMaterialAuthoring(Surface)
    {
        const Authoring = document.createElement('section');
        Authoring.className = 'material-authoring';
        Authoring.innerHTML = `<h3>Authored material</h3><label>Material <select aria-label="Material specimen"></select></label>
            <p>Layer textures · independent UVs/normals · rotated anisotropic GGX · exact dielectric Fresnel.</p>
            <label>Material TOML<textarea aria-label="Material TOML" spellcheck="false" rows="16"></textarea></label>
            <div class="material-actions"><button type="button" class="primary small" data-material-apply>Apply material</button>
            <button type="button" data-material-import>Import</button><button type="button" data-material-export>Export</button></div>
            <input type="file" accept=".material,.toml" hidden><p class="material-diagnostic" role="alert"></p>
            <p>PNG/JPEG references are relative to the configured ContentDirectory. Unsupported fields/categories produce errors, not silent substitutions.</p>`;
        this.Form.after(Authoring);
        this.Editor = Authoring.querySelector('textarea');
        this.Diagnostic = Authoring.querySelector('[role=alert]');
        this.MaterialSelection = Authoring.querySelector('select');
        Authoring.querySelector('[data-material-apply]').onclick = () => this.Refresh();
        const Picker = Authoring.querySelector('input[type=file]');
        Authoring.querySelector('[data-material-import]').onclick = () => Picker.click();
        Picker.onchange = async () =>
        {
            const File = Picker.files[0];
            if (!File) return;
            if (File.size > 900000) { this.Diagnostic.textContent = 'Material source exceeds the preview limit.'; return; }
            this.Editor.value = await File.text();
            this.Diagnostic.textContent = 'Imported source. Apply to compile; original file remains unchanged.';
            Picker.value = '';
        };
        Authoring.querySelector('[data-material-export]').onclick = () =>
        {
            const Uri = URL.createObjectURL(new Blob([this.Editor.value],{type:'text/plain;charset=utf-8'}));
            const Link = document.createElement('a');
            Link.href = Uri;
            Link.download = 'Surface.material';
            Link.click();
            setTimeout(()=>URL.revokeObjectURL(Uri),1000);
        };
        this.MaterialSelection.onchange = async () =>
        {
            this.Diagnostic.textContent = 'Loading material source…';
            try
            {
                const Response = await fetch('/api/materials/'+encodeURIComponent(this.MaterialSelection.value));
                if (!Response.ok) throw new Error('Material source unavailable');
                this.Editor.value = await Response.text();
                this.Refresh();
            }
            catch (Failure) { this.Diagnostic.textContent = Failure.message; }
        };
        try
        {
            const Response = await fetch('/api/materials');
            if (!Response.ok) throw new Error('Authored material executable is not configured');
            const Names = await Response.json();
            for (const Name of Names)
            {
                const Option = document.createElement('option');
                Option.value = Option.textContent = Name;
                this.MaterialSelection.append(Option);
            }
            if (VulkanPreview.AuthoredText)
            {
                this.Editor.value = VulkanPreview.AuthoredText;
                this.Refresh();
            }
            else this.MaterialSelection.onchange();
        }
        catch (Failure) { this.Diagnostic.textContent = Failure.message; }
    }

    async Refresh()
    {
        this.Pending = true;
        if (this.Busy) return;
        this.Busy = true;
        while (this.Pending && this.Surface.isConnected)
        {
            this.Pending = false;
            const Specification = {Geometry:this.Geometry,Mode:this.Mode};
            for (const Input of this.Form.querySelectorAll('input')) Specification[Input.name] = Number(Input.value);
            if (this.Mode === 3)
            {
                if (!this.Editor?.value) break;
                Specification.Material = this.Editor.value;
            }
            this.Status.textContent = 'Rendering with Vulkan…';
            if (this.Diagnostic) this.Diagnostic.textContent = 'Compiling material…';
            try
            {
                const Response = await fetch('/api/renderer',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(Specification)});
                if (!Response.ok) throw new Error(await Response.text());
                const Capture = await Response.blob();
                if (!this.Surface.isConnected) break;
                const Previous = this.Image.src;
                this.Image.src = URL.createObjectURL(Capture);
                this.LiveCapture = true;
                if (this.Diagnostic) this.Diagnostic.textContent = 'Material compiled and rendered successfully.';
                if (this.Mode === 3) VulkanPreview.AuthoredText = Specification.Material;
                if (Previous.startsWith('blob:')) URL.revokeObjectURL(Previous);
                this.Status.textContent = 'Vulkan · software device · live parameter readback';
                this.Statistics.textContent = `${Response.headers.get('X-Frontier-Pipelines')} shared pipelines · bracket/sphere uploads ${Response.headers.get('X-Frontier-Geometry-Uploads')} · texture uploads ${Response.headers.get('X-Frontier-Texture-Uploads')}`;
            }
            catch (Failure)
            {
                if (this.Diagnostic) this.Diagnostic.textContent = Failure.message;
                this.Status.textContent = (this.LiveCapture ? 'Last successful' : 'Saved')+' Vulkan capture · live readback unavailable';
                this.Statistics.textContent = 'Run ServeRenderer.py with a provisioned Vulkan executable, driver and shaders. No browser renderer has been substituted.';
            }
        }
        this.Busy = false;
    }
}
