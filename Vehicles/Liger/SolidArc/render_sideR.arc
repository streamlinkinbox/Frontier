open /home/user/Frontier/Vehicles/Liger/Liger_Body_SideR.arc
show shading plastic
show cages off
show iso on
view back
view persp
view orbit 45 20
view fit
view dolly 2
render Liger_SideR_01_RearQuarter --size=1920x1200
view back
view ortho
view fit
view dolly 2
render Liger_SideR_02_Side --size=1920x1200
view back
view persp
view orbit -45 20
view fit
view dolly 2
render Liger_SideR_03_FrontQuarter --size=1920x1200
view top
view ortho
view fit
view dolly 2
render Liger_SideR_04_Top --size=1920x1200
