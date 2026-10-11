#pragma once
#include "GeometricRaster/CameraProjection.h"
#include "Editor/EditorInstance.h"
#include "DisplayPresentation/CameraOptics.h"
#include <cstdio>
#include <cstring>
namespace Frontier::ProjectZero {
inline void BuildCameraInspectorSheet(const CameraProjection& Camera,const CameraOpticsSettings& Saved,EditorSheet& Sheet,bool Live) noexcept {
 Sheet.Appearance=EditorSheetAppearance::Camera;Sheet.CameraLive=Live;Sheet.CameraAspect=Live?Camera.QueryAspectRatio():1.5f;
 Sheet.SkyImage={};Sheet.StarPreview={};Sheet.FogPreview={};Sheet.WeatherPreview={};
 Sheet.GroupCount=1;for(auto& G:Sheet.Groups){G.PropertyCount=0;G.Title[0]=0;for(auto& P:G.Properties)P=EditorProperty{};}
 auto& G=Sheet.Groups[0];std::snprintf(G.Title,sizeof(G.Title),"Camera optics");
 auto Add=[&](const char* Name,float V,float Lo,float Hi,const char* Unit){auto& P=G.Properties[G.PropertyCount++];std::snprintf(P.Label,sizeof(P.Label),"%s",Name);P.Category=EditorPropertyCategory::Slider;P.Figure=V;P.Minimum=Lo;P.Maximum=Hi;P.Decimals=2;std::snprintf(P.Unit,sizeof(P.Unit),"%s",Unit);};
 float Focal=Live?(Saved.SensorWidth/Sheet.CameraAspect)/(2*std::tan(Camera.QueryFieldOfViewRadians()*.5f)):Saved.Focal;
 Add("Focal Length",Focal,1,500,"mm");Add("Sensor Width",Saved.SensorWidth,16,70,"mm");Add("Aperture",Saved.Aperture,1.4f,22,"f/");Add("Subject Distance",Saved.Subject,1,100,"m");
}
inline void ApplyCameraInspectorSheet(CameraProjection& Camera,CameraOpticsSettings& Saved,const EditorSheet& Sheet,bool Live) noexcept {
 auto Read=[&](const char* N,float Old,float Lo,float Hi){for(const auto& G:Sheet.Groups)for(unsigned I=0;I<G.PropertyCount;++I){const auto& P=G.Properties[I];if(!std::strcmp(P.Label,N))return std::isfinite(P.Figure)?std::clamp(P.Figure,Lo,Hi):Old;}return Old;};
 // Preserve external camera navigation/projection updates when the lens inputs are unchanged.
 const float OldFocal=Live?(Saved.SensorWidth/Camera.QueryAspectRatio())/(2*std::tan(Camera.QueryFieldOfViewRadians()*.5f)):Saved.Focal;
 float NewFocal=Read("Focal Length",OldFocal,1,500),NewSensor=Read("Sensor Width",Saved.SensorWidth,16,70);
 const bool Changed=std::abs(NewFocal-OldFocal)>1e-5f||NewSensor!=Saved.SensorWidth;
 Saved.Focal=NewFocal;Saved.SensorWidth=NewSensor;Saved.Aperture=Read("Aperture",Saved.Aperture,1.4f,22);Saved.Subject=Read("Subject Distance",Saved.Subject,1,100);
 if(Live&&Changed){auto O=EvaluateCameraOptics(Saved,Camera.QueryAspectRatio());Camera.AssignFieldOfView(std::clamp(O.Vertical,1.f,175.f));}
}
}
