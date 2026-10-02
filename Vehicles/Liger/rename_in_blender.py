import bpy
M = {
 "Body.001": "Body_Main_Shell",
 "Body.004": "Body_Front_Cowl",
 "ExteriorAssembly.001": "Body_Roof_Glass_Frame",
 "Plane.030": "Interior_Seats",
 "Plane.004": "Interior_Tub",
 "Final Body Guide.001": "Interior_Console",
 "Final Body Guide.003": "Interior_Dash_Top",
 "Final Body Guide.005": "Interior_Dash",
 "Guide.004": "Interior_Mirror",
 "Cylinder.003": "Wheel_Rims",
 "StandardTyre.001": "Wheel_Front",
 "StandardTyre.002": "Wheel_Rear",
 "RearFenders": "Aero_Rear_Fender_Flare",
 "Sideskirts": "Aero_Side_Skirt",
 "Spoilers": "Aero_Rear_Wing",
 "Circle.001": "Aero_Roof_Fin",
 "Circle.017": "Aero_Front_Splitter",
 "Circle.018": "Aero_Front_Lip"
}
for o, n in M.items():
    if o in bpy.data.objects: bpy.data.objects[o].name = n
