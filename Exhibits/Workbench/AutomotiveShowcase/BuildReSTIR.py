#!/usr/bin/env python3
"""Build the real C++ ReSTIR viewport host, not the standalone swatch renderer."""
from pathlib import Path
import subprocess,os,json
from PrepareTarget import ROOT,STAGE,prepare
SOURCES=['Projects/Project-Zero/Host/MaterialLevelViewport.cpp','Exhibits/Workbench/Materials/AtrousDenoiseMirror.cpp','Engine/DisplayPresentation/ShadingTableCodec.cpp','Engine/ContentInterchange/MaterialIndex.cpp','Engine/ContentInterchange/MaterialSwatchStructure.cpp','Engine/ContentInterchange/ShowcaseStructure.cpp','Engine/GeometricRaster/CameraProjection.cpp','Engine/DeviceExchange/OrientationClassifier.cpp','Engine/GeometricRaster/SceneStructure.cpp','Engine/GeometricRaster/GeometryStructure.cpp','Engine/SpatialInterface/InterfaceStructure.cpp','Engine/SpatialInterface/InterfaceSequence.cpp','Engine/SpatialInterface/InterfaceLayoutCodec.cpp','Engine/SpatialInterface/InterfaceLightProjection.cpp','Engine/SpatialInterface/InterfacePointerProjection.cpp','Engine/SpatialInterface/PaletteConfiguration.cpp','Engine/DisplayPresentation/MotionIntegrator.cpp','Projects/Project-Zero/Source/InterfaceTrialSequence.cpp','Projects/Project-Zero/Source/SkyFogIntegrator.cpp']
if __name__=='__main__':
 deps=prepare(SOURCES+['Exhibits/Workbench/Materials/StageAtrousDenoise.py','Engine/Shaders/AtrousDenoise.slang'])
 generated=STAGE/'denoise';generated.mkdir(exist_ok=True)
 subprocess.run(['python3',str(STAGE/'Exhibits/Workbench/Materials/StageAtrousDenoise.py')],cwd=STAGE,env=dict(os.environ,DO_STAGE=str(generated)),check=True)
 dirs=['stub','Projects/Project-Zero/Host','Projects/Project-Zero/Shaders','Projects/Project-Zero/Source','Engine/Shaders','Engine/DisplayPresentation','Engine/ContentInterchange','Engine/DeviceExchange','Engine/GeometricRaster','Exhibits/Workbench/Materials','Exhibits/Workbench/Editor','denoise']
 command=['g++','-O2','-std=c++20','-DFRONTIER_CPU_PORT','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-pthread',*['-I'+str(STAGE/d) for d in dirs],*[str(STAGE/s) for s in SOURCES],'-o',str(STAGE/'MaterialLevelViewport')]
 subprocess.run(command,cwd=STAGE,check=True)
 (STAGE/'restir-build.json').write_text(json.dumps({'command':command,'dependencies':deps},indent=2))
 print('Built',STAGE/'MaterialLevelViewport')
