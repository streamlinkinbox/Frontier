#!/usr/bin/env python3
"""Generate editable SVG-only variants for the pinned ThorVG subset; never rasterize.
Original artwork is preserved. See EngineContent/Icons/ThorVG/manifest.json for
explicit compatibility compromises. Requires fonttools only when regenerating.
"""
from pathlib import Path
import copy, json, re, xml.etree.ElementTree as E
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
ROOT=Path(__file__).resolve().parents[2]
ICONS=ROOT/'EngineContent/Icons'; OUT=ICONS/'ThorVG'
NS='http://www.w3.org/2000/svg'; E.register_namespace('',NS)
ALLOWED={'svg','title','desc','defs','g','path','rect','circle','ellipse','line','polyline','polygon','linearGradient','radialGradient','stop','clipPath','mask','filter','feGaussianBlur'}
def tag(e):return e.tag.split('}')[-1]
def elem(name,attrs=None):return E.Element('{'+NS+'}'+name,attrs or {})
def fingerprint(data):
    value=14695981039346656037
    for byte in data:value=((value^byte)*1099511628211)&0xffffffffffffffff
    return f'{value:016x}'
FONTS={}
def outline(text):
    weight=text.get('font-weight','400');weight='600' if weight=='bold' else weight
    name='SemiBold' if int(weight)>=600 else 'Medium' if int(weight)>=500 else 'Regular'
    if name not in FONTS:FONTS[name]=TTFont(ROOT/f'EngineContent/FontArchives/Archivo/Archivo-{name}.ttf')
    font=FONTS[name];glyphs=font.getGlyphSet();cmap=font.getBestCmap();scale=float(text.get('font-size','16'))/font['head'].unitsPerEm
    label=''.join(text.itertext());names=[cmap.get(ord(c),'.notdef') for c in label]
    advances=[font['hmtx'][n][0]*scale for n in names];spacing=float(text.get('letter-spacing','0'))
    width=sum(advances)+spacing*max(0,len(names)-1);x=float(text.get('x','0'));y=float(text.get('y','0'))
    if text.get('text-anchor')=='middle':x-=width/2
    elif text.get('text-anchor')=='end':x-=width
    group=elem('g',{k:v for k,v in text.attrib.items() if k not in {'x','y','font-family','font-size','font-weight','letter-spacing','text-anchor'}})
    for name,advance in zip(names,advances):
        pen=SVGPathPen(glyphs);glyphs[name].draw(TransformPen(pen,(scale,0,0,-scale,x,y)))
        if pen.getCommands():group.append(elem('path',{'d':pen.getCommands()}))
        x+=advance+spacing
    return group

def convert(path):
    data=path.read_bytes();root=E.fromstring(data);unsupported=sorted({tag(e) for e in root.iter()}-ALLOWED)
    if not unsupported:return None
    notes=[]
    # SVG2 rgba() colors are not parsed by this pin, including gradient stops.
    for node in root.iter():
        for paint,opacity in [('fill','fill-opacity'),('stroke','stroke-opacity'),('stop-color','stop-opacity'),('flood-color','flood-opacity')]:
            color=node.get(paint,'');match=re.fullmatch(r'rgba\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*([.\d]+)\s*\)',color)
            if match:
                r,g,b,a=match.groups();node.set(paint,f'#{int(r):02x}{int(g):02x}{int(b):02x}')
                node.set(opacity,f'{float(a)*float(node.get(opacity,"1")):.6f}')
                notes.append('SVG2 rgba() colors normalized to RGB + explicit opacity (including gradient stops).')
            elif 'rgba(' in color:raise ValueError('Unreviewed rgba color: '+color)
    defs=next((e for e in root if tag(e)=='defs'),None)
    if defs is None:defs=elem('defs');root.insert(0,defs)
    shadow={};empty=set();patterns={};serial=0
    for parent in list(root.iter()):
        for child in list(parent):
            if tag(child)=='pattern':patterns[child.get('id')]=child;parent.remove(child)
            elif tag(child)=='filter':
                drop=next((e for e in child if tag(e)=='feDropShadow'),None)
                if drop is not None:
                    shadow[child.get('id')]=dict(drop.attrib);parent.remove(child)
                    notes.append('Drop shadow uses cloned vector silhouettes + ThorVG Gaussian blur; filter bounds/compositing may differ.')
                else:
                    removed=[]
                    for effect in list(child):
                        if tag(effect) not in ALLOWED:removed.append(tag(effect));child.remove(effect)
                    if removed:notes.append('Procedural filter primitives omitted in vector variant: '+', '.join(removed))
                    if not list(child):empty.add(child.get('id'));parent.remove(child)
    def reference(s):
        match=re.fullmatch(r'url\(#([^\)]+)\)',s or '');return match.group(1) if match else None
    def visit(node):
        nonlocal serial
        for i,child in enumerate(list(node)):node[i]=visit(child)
        if tag(node)=='mask' and node.get('id')=='smoke-mask':
            # The pinned loader loses gradient paints inside mask definitions.
            # Keep the original fade as fine vector opacity strips (not pixels).
            node[:]=[elem('rect',{'width':'256','height':'166.4','fill':'#ffffff'})]
            for band in range(64):
                node.append(elem('rect',{'y':f'{166.4+band*1.4:.4f}','width':'256','height':'1.4',
                    'fill':'#ffffff','fill-opacity':f'{1-.88*(band+.5)/64:.6f}'}))
            notes.append('Smoke fade mask expanded to 64 vector-opacity strips; avoids pinned ThorVG gradient-in-mask failure.')
        if tag(node)=='text':
            notes.append('Text outlined using bundled OFL Archivo (no runtime/system-font dependency).');node=outline(node)
        pattern=reference(node.get('fill'))
        if pattern in patterns:
            if tag(node)!='rect' or pattern!='slate-fabric--weavePattern':raise ValueError('Unreviewed SVG pattern')
            serial+=1;clip=f'frontier-weave-{serial}';shape=copy.deepcopy(node);shape.set('fill','white');shape.attrib.pop('stroke',None)
            cp=elem('clipPath',{'id':clip});cp.append(shape);defs.append(cp)
            node=elem('g',{'clip-path':f'url(#{clip})'});lines=elem('g',{'transform':'rotate(45)','stroke':'#ffffff','stroke-opacity':'.04','stroke-width':'1','fill':'none'})
            commands=[]
            for n in range(-300,301,6):commands.extend([f'M-300 {n+3}H300',f'M{n+3} -300V300'])
            lines.append(elem('path',{'d':' '.join(commands)}));node.append(lines);notes.append('Weave pattern expanded into clipped vector strokes.')
        key=reference(node.get('filter'))
        if key in empty:node.attrib.pop('filter',None)
        if key in shadow:
            settings=shadow[key];node.attrib.pop('filter',None);transform=node.attrib.pop('transform',None)
            serial+=1;blur=f'frontier-shadow-{serial}';f=elem('filter',{'id':blur,'x':'-100%','y':'-100%','width':'300%','height':'300%'});f.append(elem('feGaussianBlur',{'stdDeviation':settings.get('stdDeviation','0')}));defs.append(f)
            silhouette=copy.deepcopy(node);tinted={}
            def shadow_paint(value):
                original=reference(value)
                gradient=next((e for e in defs if e.get('id')==original and tag(e) in {'linearGradient','radialGradient'}),None) if original else None
                if gradient is None:return settings.get('flood-color','#000000')
                if original not in tinted:
                    clone=copy.deepcopy(gradient);name=f'frontier-shadow-gradient-{serial}-{len(tinted)}';clone.set('id',name)
                    for stop in clone:stop.set('stop-color',settings.get('flood-color','#000000'))
                    defs.append(clone);tinted[original]=name
                return f'url(#{tinted[original]})'

            for e in silhouette.iter():
                e.attrib.pop('id',None)
                for paint in ('fill','stroke'):
                    if paint in e.attrib and e.get(paint)!='none':e.set(paint,shadow_paint(e.get(paint)))
            shade=elem('g',{'transform':f"translate({settings.get('dx','0')} {settings.get('dy','0')})",'opacity':settings.get('flood-opacity','1'),'filter':f'url(#{blur})'});shade.append(silhouette)
            group=elem('g',{'transform':transform} if transform else {});group.extend([shade,node]);node=group
        return node
    root=visit(root)
    unknown={tag(e) for e in root.iter()}-ALLOWED
    if unknown:raise ValueError((path.name,unknown))
    svg=E.tostring(root,encoding='unicode')
    assert '<image' not in svg and 'href=' not in svg
    (OUT/path.name).write_text(f'<!-- frontier-source-fnv1a64:{fingerprint(data)} -->\n'+svg+'\n')
    return {'original_unsupported':unsupported,'changes':sorted(set(notes)),'source_fnv1a64':fingerprint(data)}

def main():
    OUT.mkdir(exist_ok=True);manifest={}
    files=re.findall(r'FRONTIER_ICON\([^,]+,\s*"([^"]+)"', (ROOT/'Engine/DisplayPresentation/IconSymbols.inc').read_text())
    for file in files:
        result=convert(ICONS/file)
        if result:manifest[file]=result
    (OUT/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');print(f'{len(manifest)} SVG-only ThorVG variants; no pixels baked.')
if __name__=='__main__':main()
