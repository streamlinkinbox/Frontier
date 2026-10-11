#pragma once
#include <algorithm>
#include <cmath>
#include <limits>
namespace Frontier {
struct CameraOpticsSettings {float Focal=35,SensorWidth=36,Aperture=2.8f,Subject=4;};
struct CameraOpticsResult {float Horizontal,Vertical,Pupil,Near,Far,Hyperfocal,FrameWidth,FrameHeight,SensorHeight;};
inline CameraOpticsResult EvaluateCameraOptics(const CameraOpticsSettings& S,float Aspect) noexcept {
 const float A=std::max(.01f,Aspect),F=std::max(1.f,S.Focal),N=std::max(.1f,S.Aperture),D=std::max(F+1,S.Subject*1000),H=F*F/(N*.03f)+F,Height=S.SensorWidth/A;
 return {2*std::atan(S.SensorWidth/(2*F))*57.2957795f,2*std::atan(Height/(2*F))*57.2957795f,F/N,H*D/(H+D-F)/1000,H>D-F?H*D/(H-D+F)/1000:std::numeric_limits<float>::infinity(),H/1000,S.Subject*S.SensorWidth/F,S.Subject*Height/F,Height};
}
}
