#!/usr/bin/env python3
"""Local authoring bridge: native SceneStructure owns entities, never React state.
This is not the Windows/Vulkan game-process transport.
"""
from pathlib import Path
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import argparse, json, math, os, subprocess, threading
ROOT=Path(__file__).resolve().parents[3]

class World:
 def __init__(self,path):
  self.path=path;self.lock=threading.Lock();self.process=None
  self.history=json.loads(path.read_text()) if path.exists() else []
  self.restore()
 def rpc(self,line):
  self.process.stdin.write(line+'\n');self.process.stdin.flush()
  result=json.loads(self.process.stdout.readline())
  if not result.get('ok'):raise ValueError(result.get('error','Native worker refused request'))
  return result
 def restore(self):
  if self.process:
   self.process.terminate();self.process.wait()
  self.process=subprocess.Popen([str(ROOT/'.cache/construct/WorldHost')],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,bufsize=1)
  for command in self.history:self.rpc(command)
 def create(self,data):
  if not isinstance(data,dict):raise ValueError('Expected an object')
  kind=data.get('kind');size=data.get('size',1);position=data.get('position',[0,0,0]);name=data.get('name','')
  if not isinstance(name,str):raise ValueError('Name must be text')
  name=name.strip()
  if type(kind)!=int or not 0<=kind<9:raise ValueError('Unsupported entity type')
  if not isinstance(position,list) or len(position)!=3:raise ValueError('Position requires X, Y and Z')
  for value,limit in [(size,10000),*[(p,100000) for p in position]]:
   if type(value) not in (int,float) or not math.isfinite(value) or abs(value)>limit:raise ValueError('Invalid dimensions')
  if size<.001:raise ValueError('Size must be at least 0.001 m')
  if len(name.encode())>64 or any(ord(c)<32 or ord(c)==127 for c in name):raise ValueError('Name must be at most 64 UTF-8 bytes, without control characters')
  quoted='"'+name.replace('\\','\\\\').replace('"','\\"')+'"'
  command=' '.join(map(str,[kind,*position,size]))+' '+quoted
  result=self.rpc(command)
  pending=self.history+[command];temp=self.path.with_suffix('.tmp')
  try:
   self.path.parent.mkdir(parents=True,exist_ok=True)
   with temp.open('w') as f:json.dump(pending,f);f.flush();os.fsync(f.fileno())
   os.replace(temp,self.path)
  except OSError:
   self.restore();raise
  self.history=pending
  return result

if __name__=='__main__':
 parser=argparse.ArgumentParser();parser.add_argument('--port',type=int,default=5191);parser.add_argument('--scene',type=Path,default=ROOT/'.frontier/construct-world.json');args=parser.parse_args()
 world=World(args.scene)
 class Handler(BaseHTTPRequestHandler):
  def reply(self,code,data):
   body=json.dumps(data).encode();self.send_response(code);self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(body)));self.send_header('Cache-Control','no-store');self.end_headers();self.wfile.write(body)
  def do_GET(self):
   if self.path=='/':return self.reply(200,{'service':'Frontier native scene authoring','endpoint':'/api/construct','editorPort':5173,'renderer':'CPU authoring only; not a Vulkan game process'})
   if self.path!='/api/construct':return self.reply(404,{'error':'Not found'})
   try:
    with world.lock:self.reply(200,world.rpc('list'))
   except (OSError,ValueError):self.reply(503,{'error':'Native authoring worker unavailable; no browser-only fallback'})
  def do_POST(self):
   if self.path!='/api/construct':return self.reply(404,{'error':'Not found'})
   try:
    length=int(self.headers.get('Content-Length','0'))
    if not 0<length<=4096:raise ValueError('Invalid request length')
    data=json.loads(self.rfile.read(length))
    with world.lock:result=world.create(data)
    self.reply(201,result)
   except (ValueError,TypeError,AttributeError) as error:self.reply(400,{'error':str(error)})
   except OSError:self.reply(503,{'error':'Could not persist native scene; creation was rolled back'})
 print(f'Native authoring bridge on {args.port}; durable scene: {args.scene}',flush=True)
 try:ThreadingHTTPServer(('0.0.0.0',args.port),Handler).serve_forever()
 finally:world.process.terminate();world.process.wait()
