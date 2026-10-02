open ../../../Vehicles/Liger/Liger_Body_Surface.arc
show shading plastic
show cages off
show iso off
view iso
view fit
view dolly 3
render Liger_SA_01_Iso_Curves --size=1920x1200
isolate Shell_Tail Shell_RearArch Shell_Cabin Shell_FrontArch Shell_Nose
render Liger_SA_02_Iso_Surface --size=1920x1200
view orbit 150 20
view fit
view dolly 3
render Liger_SA_03_RearQuarter --size=1920x1200
view right
view fit
view dolly 3
render Liger_SA_04_Side --size=1920x1200
view top
view fit
view dolly 3
render Liger_SA_05_Top --size=1920x1200
view front
view fit
view dolly 3
render Liger_SA_06_Front --size=1920x1200
