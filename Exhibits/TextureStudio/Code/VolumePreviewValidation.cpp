/*============================================================================================================================================
                                                   VOLUMEPREVIEWVALIDATION.CPP
============================================================================================================================================*/
// 🧪 Validate slice modes, palettes, axis/camera controls and unsigned normalization with an anisotropic synthetic field.
#include "Engine/ContentInterchange/VolumePreview.h"
#include <cstdlib>
#define CHECK(Expression) do { if (!(Expression)) { std::cerr << "Failed: " << #Expression << " at line " << __LINE__ << "\n"; return EXIT_FAILURE; } } while (false)
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace Frontier;
int main()
{
    VolumeDistanceField V;
    V.Dimensions={24,20,16}; V.Minimum={-1.f,-.75f,-.5f}; V.Maximum={1.f,.75f,.5f};
    V.Step={2.f/24,.075f,1.f/16}; V.MinimumDistance=-.45f; V.MaximumDistance=1.f;
    for(uint32_t Z=0;Z<V.Dimensions[2];++Z) for(uint32_t Y=0;Y<V.Dimensions[1];++Y) for(uint32_t X=0;X<V.Dimensions[0];++X)
    {
        const float PX=V.Minimum[0]+(X+.5f)*V.Step[0], PY=V.Minimum[1]+(Y+.5f)*V.Step[1], PZ=V.Minimum[2]+(Z+.5f)*V.Step[2];
        V.Distances.push_back(std::sqrt(PX*PX+PY*PY+PZ*PZ)-.45f);
        V.Confidence.push_back(float(X)/(V.Dimensions[0]-1));
    }
    VolumePreviewOptions O; O.Yaw=.65f; O.Pitch=.3f; O.Resolution=64;
    const auto Iso=RenderVolumePreview(V,O);
    CHECK(Iso.size()==64*64*4);
    O.Mode=VolumePreviewMode::Triplane; const auto Tri=RenderVolumePreview(V,O); CHECK(Tri!=Iso);
    O.Mode=VolumePreviewMode::Stack; const auto Stack=RenderVolumePreview(V,O); CHECK(Stack!=Tri && Stack!=Iso);
    O.Axis=0; const auto Axis=RenderVolumePreview(V,O); CHECK(Axis!=Stack);
    O.Slice=.2f; const auto Slice=RenderVolumePreview(V,O); CHECK(Slice!=Axis);
    O.Palette=VolumePreviewPalette::Confidence; const auto Confidence=RenderVolumePreview(V,O); CHECK(Confidence!=Slice);
    O.Yaw+=.4f; const auto Orbit=RenderVolumePreview(V,O); CHECK(Orbit!=Confidence);
    O.Zoom=1.6f; CHECK(RenderVolumePreview(V,O)!=Orbit);
    const size_t Index=V.Distances.size()/2;
    V.Distances[Index]=0.f; V.Signed=true;
    CHECK(VolumePreviewColour(V,Index,VolumePreviewPalette::Normalized)[0]==128);
    V.Signed=false; CHECK(VolumePreviewColour(V,Index,VolumePreviewPalette::Normalized)[0]==0);
    CHECK(VolumePreviewColour(V,Index,VolumePreviewPalette::Inside)[0]==245);
    V.Distances.pop_back(); bool Rejected=false;
    try { (void)RenderVolumePreview(V,O); } catch(const std::runtime_error &) { Rejected=true; }
    CHECK(Rejected);
    std::cout << "Volume preview modes, controls, palettes and validation passed.\n";
}
