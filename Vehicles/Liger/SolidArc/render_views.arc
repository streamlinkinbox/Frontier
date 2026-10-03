open /home/user/Frontier/Vehicles/Liger/Liger_Body_Surface.arc
show shading plastic
show cages off
show iso on
view front
view persp
view orbit 48 20
view fit
view dolly 2
render Liger_SA_01_Iso_Curves --size=1920x1200
show iso off
render Liger_SA_02_Iso_Surface --size=1920x1200
show iso on
view front
view persp
view orbit -48 20
view fit
view dolly 2
render Liger_SA_03_RearQuarter --size=1920x1200
view front
view ortho
view fit
view dolly 2
render Liger_SA_04_Side --size=1920x1200
view top
view ortho
view fit
view dolly 2
render Liger_SA_05_Top --size=1920x1200
view right
view ortho
view fit
view dolly 2
render Liger_SA_06_Front --size=1920x1200
