int main(){try{
 using namespace Frontier;
 int checks=0;
 auto Check=[&](bool value,const char* text){if(!value)throw std::runtime_error(text);++checks;};
 CloudLayerSettings C;LocalVolumeSettings L,F;FogSettings Fog;WindSettings Wind;Wind.Speed=0;
 VolumetricBudget Budget;Budget.CloudSteps=64;Budget.LocalSteps=64;
 auto Pack=[&](){auto r=PackWeatherConstants(C,L,F,Fog,Wind,Budget,3);for(int i=0;i<19;++i)Weather[i]=vec4(r.Rows[i][0],r.Rows[i][1],r.Rows[i][2],r.Rows[i][3]);Weather[8].y=Weather[8].z=Weather[8].w=.2f;};
 vec3 O(0,0,2),D(0,0,1),White(1);
 Pack();Check(ApplyWeather(O,D,200,White)==White,"disabled weather exact identity");
 C.Enabled=true;C.Base=10;C.Thickness=100;C.Coverage=1;C.Density=3;Pack();
 Check(length(ApplyWeather(O,D,200,White)-White)>.001f,"global cloud changes pixels");
 Check(ApplyWeather(O,D,5,White)==White,"opaque surface before cloud occludes it");
 Check(ApplyWeather(O,vec3(0,1,0),200,White)==White,"parallel ray below cloud is clear");
 // Shader density is the same model, without demanding GPU sin/hash bit identity.
 for(int n=0;n<64;++n){float p[3]={float(n*7),float(n*11),float(n+20)};float cpu=VolumetricMedia::CloudDensity(C,Wind,p,3);Check(std::abs(cpu-WeatherDensity(0,vec3(p[0],p[1],p[2])))<.003f,"global density model matches CPU");}
 for(unsigned type=0;type<6;++type){C.Type=CloudTypeCategory(type);Pack();
   for(int n=0;n<16;++n){float p[3]={float(n*7),float(n*11),float(12+n*6)};Check(std::abs(VolumetricMedia::CloudDensity(C,Wind,p,3)-WeatherDensity(0,vec3(p[0],p[1],p[2])))<.003f,"all six cloud profiles match CPU");}
 }C.Type=CloudTypeCategory::Cumulus;
 C.Enabled=false;L.Enabled=true;L.Centre[0]=L.Centre[1]=0;L.Centre[2]=50;
 L.HalfSize[0]=L.HalfSize[1]=L.HalfSize[2]=40;L.Coverage=1;L.Density=3;Pack();
 Check(length(ApplyWeather(O,D,200,White)-White)>.01f,"local cloud visible");
 Check(ApplyWeather(O,D,5,White)==White,"local cloud depth clipped");
 for(int n=0;n<32;++n){float p[3]={float(n-16),0,float(n+30)};float cpu=VolumetricMedia::LocalDensity(L,Wind,p,3);Check(std::abs(cpu-WeatherDensity(1,vec3(p[0],p[1],p[2])))<.003f,"local density model matches CPU");}
 L.Centre[0]=1000;Pack();Check(ApplyWeather(O,D,200,White)==White,"moving local bounds off ray removes cloud");
 L.Centre[0]=0;F=L;L.Enabled=false;Pack();Check(length(ApplyWeather(O,D,200,White)-White)>.01f,"local fog visible");
 F.Enabled=false;Fog.HeightEnabled=true;Pack();
 Check(length(ApplyWeather(O,vec3(0,1,0),100,White)-White)>.01f,"height fog visible");
 Check(ApplyWeather(O,D,0,White)==White,"zero path is identity");
 Fog.HeightEnabled=false;Fog.AerialEnabled=true;Pack();Check(length(ApplyWeather(O,vec3(0,1,0),10000,White)-White)>.01f,"aerial fog visible");
 // Analytic fog source and extinction match the CPU model with identical coefficients.
 AtmosphereMedium Medium;AtmosphereLight Sun;Sun.Intensity=1;
 SkyRayleigh=vec4(Medium.RayleighScattering[0]*Medium.RayleighStrength,Medium.RayleighScattering[1]*Medium.RayleighStrength,Medium.RayleighScattering[2]*Medium.RayleighStrength,Medium.RayleighScaleHeight);
 SkyMie=vec4(Medium.MieScattering*Medium.MieStrength,Medium.MieScaleHeight,0,0);
 for(int mode=0;mode<3;++mode){Fog.HeightEnabled=mode!=1;Fog.AerialEnabled=mode!=0;Pack();
   float rgb[3]={1,1,1},dir[3]={0,1,0},ambient[3]={.2f,.2f,.2f};
   FogModel::Apply(Fog,Medium,Sun,2,2,300,dir,ambient,rgb);
   auto gpu=ApplyWeather(O,vec3(0,1,0),300,White);
   Check(length(gpu-vec3(rgb[0],rgb[1],rgb[2]))<.00001f,"analytic fog composition matches CPU");
 }
 Fog.HeightEnabled=false;
 Fog.AerialEnabled=false;C.Enabled=true;L.Enabled=true;F.Enabled=true;Pack();
 auto both=ApplyWeather(O,D,200,White);Check(std::isfinite(both.x)&&std::isfinite(both.y)&&std::isfinite(both.z),"overlapping global and local media finite");
 SkySunRadiance=vec4(0);Weather[8].y=Weather[8].z=Weather[8].w=0;
 auto full=ApplyWeather(O,D,200,White);F.Enabled=false;Pack();Weather[8].y=Weather[8].z=Weather[8].w=0;
 auto less=ApplyWeather(O,D,200,White);Check(full.x<=less.x+.00001f,"adding overlapping fog cannot increase transmission");
 // Wind noise is evaluated at sample altitude, not frozen at volume centre.
 Wind.Speed=12;Wind.Shear=.2f;Wind.Veer=3;Pack();
 float p[3]={12,20,50};Check(std::abs(VolumetricMedia::CloudDensity(C,Wind,p,3)-WeatherDensity(0,vec3(12,20,50)))<.003f,"wind-advection density matches CPU");
 std::printf("PASS %d shader-source mirror checks (CPU execution, not Vulkan)\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
