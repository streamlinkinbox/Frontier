#!/usr/bin/env python3
"""Native worker persistence/rejection/rollback contract (isolated temporary scene)."""
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest.mock import patch
from Serve import World
with TemporaryDirectory() as directory:
 world=World(Path(directory)/'scene.json')
 try:
  first=world.create({'kind':0,'name':'Repeated','position':[1,2,3],'size':2})
  assert first['entities'][0]['position']==[1,2,3]
  assert first['triangles']==12
  second=world.create({'kind':0,'name':'Repeated'})
  assert second['entities'][1]['name']=='Repeated 2'
  for request in [{'kind':99},{'kind':True},{'kind':1,'size':float('nan')},{'kind':1,'position':[1,2]},{'kind':1,'name':'bad\nname'},{'kind':1,'size':0}]:
   try:world.create(request);raise AssertionError('Invalid request accepted')
   except ValueError:pass
   assert world.rpc('list')==second
  world.restore();assert world.rpc('list')==second,'Restart changed world or identity'
  try:
   with patch('Serve.os.replace',side_effect=OSError('Simulated disk failure')):world.create({'kind':1})
   raise AssertionError('Persistence failure was swallowed')
  except OSError:pass
  assert world.rpc('list')==second,'Native world did not roll back'
  world.restore();assert world.rpc('list')==second,'Durable scene changed on failed write'
  area=world.create({'kind':6});assert area['emitters']==2
  camera=world.create({'kind':7});assert camera['entities'][-1]['camera']
  empty=world.create({'kind':8});assert empty['entities'][-1]['triangles']==0
  print('PASS bridge: native geometry, naming, invalid requests, stable IDs across restart, disk-failure rollback, emission and camera/empty records.')
 finally:
  world.process.terminate();world.process.wait()
