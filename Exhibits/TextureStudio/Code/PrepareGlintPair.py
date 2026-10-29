#============================================================================================================================================
#                                                          PREPAREGLINTPAIR.PY
#============================================================================================================================================
# 🧩 Prepare an isolated author-demo-derived GLSL mirror versus the existing cellular material source.

from pathlib import Path
import base64
import hashlib
import json
import re
import subprocess

Root = Path(__file__).resolve().parents[3]
Destination = Root / '_ClaudeScratch/build/GlintPair'
Destination.mkdir(parents=True, exist_ok=True)
Repository = 'd-k-ivanov/d-k-ivanov.github.io'
Revision = '96384ebf29c7471f3cda15ec2d53ad3d94ec0fb7'
Pathname = 'shaders/assets/shaders/sampling/glinty_ndf.fragment.glsl'
Reply = json.loads(subprocess.check_output(['gh', 'api', f'repos/{Repository}/contents/{Pathname}?ref={Revision}']))
Raw = base64.b64decode(Reply['content'])
assert hashlib.sha1(b'blob ' + str(len(Raw)).encode() + b'\0' + Raw).hexdigest() == 'c161b097d785346c773ebcd0045003a8896f431e'
(Destination / 'GlintyMirror.glsl').write_bytes(Raw)
Reference = Raw.decode()
Reference = Reference[Reference.index('const float pi ='):Reference.index('// torus intersection')]
Reference = re.sub(r'(\d+(?:\.\d*)?)f\b', r'\1', Reference)
# Reuse the established translation of the unchanged production cellular source, not a new cellular model.
Preparation = (Root / 'Exhibits/TextureStudio/Code/PrepareGlintComparison.py').read_text()
Translation = Preparation[Preparation.index("PaintFile = Root /"):Preparation.index("Prefix = '#version")]
Scope = {'Root': Root, 're': re}
exec(Translation, Scope)
Paint = Scope['Paint']
Prefix = '''#version 300 es
precision highp float;
precision highp int;
uniform float Phase;
uniform int LightCount;
uniform float DensityScale;
in vec2 Uv;
out vec4 OutputColour;
'''
Fixture = (Root / 'Exhibits/TextureStudio/Code/GlintPairScene.glsl').read_text()
Sources = {}
for Name, Method in [('Smooth', 0), ('Cellular', 1), ('Glinty', 2), ('Coverage', 0)]:
    Source = Prefix + f'#define METHOD {Method}\n' + ('#define COVERAGE_ONLY 1\n' if Name == 'Coverage' else '') + Paint + '\n' + Reference + '\n' + Fixture
    (Destination / f'{Name}.frag').write_text(Source)
    Sources[Name] = hashlib.sha256(Source.encode()).hexdigest()
Report = {
    'glinty_repository': Repository, 'glinty_revision': Revision, 'glinty_path': Pathname,
    'glinty_mirror_git_blob': Reply['sha'], 'glinty_mirror_sha256': hashlib.sha256(Raw).hexdigest(),
    'author_link': 'https://thonatt.github.io/ -> https://www.shadertoy.com/view/tcdGDl',
    'authenticity': 'author-demo-derived GLSL mirror; public author-demo prefix matches, full-file identity not established',
    'licence': 'mirror has no repository licence endpoint; author-demo commercial reuse permission not established; scratch evaluation only',
    'adaptation': 'NDF helpers extracted, GLSL float f suffix removed; shared scene replaces demo geometry and lighting',
    'cellular_source': 'Engine/Render/Shader/AutomotivePaint.slang',
    'cellular_source_sha256': hashlib.sha256((Root / 'Engine/Render/Shader/AutomotivePaint.slang').read_bytes()).hexdigest(),
    'cellular_translation': 'existing PrepareGlintComparison.py translation, unchanged cellular algorithm',
    'generated_shader_sha256': Sources,
    'geometry': 'three analytic material-sample spheres: sand, metallic blue paint, snow; not complete vehicle/snow/grain simulation',
    'common': 'same geometry, camera, light positions/intensities, exposure, GGX/Smith/Schlick wrapper and paint clearcoat',
    'not_equal_appearance': 'cellular alters albedo/roughness/normal once per pixel; Glinty evaluates a stochastic NDF per light; no equal-quality ranking',
    'settings': {'sand': {'alpha': .32, 'metalness': 0, 'cell_layers': 1, 'cell_particle_size': 1200, 'glinty_N': 200000},
                 'paint': {'alpha': .20, 'metalness': .9, 'cell_layers': 3, 'cell_particle_size': 450, 'glinty_N': 800000},
                 'snow': {'alpha': .24, 'metalness': 0, 'cell_layers': 1, 'cell_particle_size': 900, 'glinty_N': 300000},
                 'microfacet_alpha': .012, 'filter_size': .7, 'glinty_ndf_mix': .7},
}
(Destination / 'Inputs.json').write_text(json.dumps(Report, indent=2) + '\n')
print('Prepared isolated Glinty mirror / unchanged cellular source / smooth baseline shaders')
