open /home/user/Frontier/Vehicles/Liger/Liger_Complete.arc
show shading plastic
show cages off
show iso on
view front
view persp
view orbit 48 18
view fit
view dolly 2
render Liger_Complete_01_FrontQuarter --size=1920x1200
view front
view persp
view orbit -48 18
view fit
view dolly 2
render Liger_Complete_02_RearQuarter --size=1920x1200
view front
view ortho
view fit
view dolly 2
render Liger_Complete_03_Side --size=1920x1200
view top
view ortho
view fit
view dolly 2
render Liger_Complete_04_Top --size=1920x1200
view right
view ortho
view fit
view dolly 2
render Liger_Complete_05_Front --size=1920x1200
view front
view ortho
view orbit 48 22
view fit
render sheet 0 --size=1280x800
view front
view ortho
view fit
render sheet 1 --size=1280x800
view right
view ortho
view fit
render sheet 2 --size=1280x800
view top
view ortho
view fit
render sheet 3 --size=1280x800
render sheet finalize Liger_Complete_06_ContactSheet
show iso off
show shading matcap
view front
view persp
view orbit 48 18
view fit
view dolly 2
render Liger_Complete_07_FrontQuarter_Matcap --size=1920x1200
view front
view persp
view orbit -48 18
view fit
view dolly 2
render Liger_Complete_08_RearQuarter_Matcap --size=1920x1200
