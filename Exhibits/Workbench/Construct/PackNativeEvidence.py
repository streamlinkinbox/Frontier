#!/usr/bin/env python3
"""Verify folder parity; losslessly pack only the native Construct captures."""
from pathlib import Path
import hashlib,json,re,struct,zlib
R=Path(__file__).resolve().parents[3];O=R/'Exhibits/Gallery/NativeConstruct'
folders={}
for kind,source in [('scene','scenes'),('world','models'),('materials','materials')]:
 text=(R/f'Experimental/FrontierEditor/custom-icons/asset-folder-{source}.svg').read_text()
 for pattern in [r'<text\b[^>]*>[\s\S]*?</text>',r'<title\b[^>]*>[\s\S]*?</title>',r'\saria-labelledby="[^"]*"']:text=re.sub(pattern,'',text)
 native=R/f'EngineContent/Icons/folder-{kind}.svg'
 assert native.read_text()==text,kind+' differs from HTML compactFolder'
 folders[kind]=hashlib.sha256(text.encode()).hexdigest()
for image in O.glob('*.png'):
 blob=image.read_bytes();parts=[];i=8
 while i<len(blob):
  n=struct.unpack('>I',blob[i:i+4])[0];parts.append((blob[i+4:i+8],blob[i+8:i+8+n]));i+=n+12
 raw=zlib.decompress(b''.join(d for t,d in parts if t==b'IDAT'));packed=zlib.compress(raw,9);out=bytearray(blob[:8]);done=False
 for t,d in parts:
  if t==b'IDAT':
   if done:continue
   d=packed;done=True
  out+=struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
 assert zlib.decompress(packed)==raw
 image.write_bytes(out)
assets=[*O.glob('*.png'),*list((R/'EngineContent/Icons/Baked').glob('*')),R/'EngineContent/Icons/Manifest.json']
(O/'AssetParity.json').write_text(json.dumps({'htmlFolderParity':folders,'captureEncoding':'lossless IDAT recompression; unchanged native draw-list pixels','sha256':{str(p.relative_to(R)):hashlib.sha256(p.read_bytes()).hexdigest() for p in assets}},indent=2)+'\n')
print('PASS: native category folders exactly match HTML compactFolder; captured pixels unchanged.')
