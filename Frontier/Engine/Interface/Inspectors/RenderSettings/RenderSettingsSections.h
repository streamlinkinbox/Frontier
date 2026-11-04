/*==========================================================================================================================================
                                                         RENDERSETTINGSSECTIONS.H
==========================================================================================================================================*/
// 🧩 The Render settings page's sections, one class per file: resolution and sampling, global illumination, reflections, sun and
//    shadows, denoising, exposure, geometry, debug views and refresh timing (their rows: Controls/SectionRows.h).

#pragma once

#include "Engine/Interface/Controls/SectionRows.h"
#include "Engine/Interface/Inspectors/RenderSettings/RenderSettingsReadings.h"
#include "Engine/Scene/EngineConfigurations.h"

namespace Frontier
{

class ControlColumn;

//------------------------------------------------------------------------------------------------------------------------
//                                                    SECTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Each Construct draws its section and returns true when a setting changed.
class SamplingSection
{
  public:
    static bool Construct(ControlColumn& Page, RenderPipelineConfiguration& Parameters, const RenderSettingsReading& Reading);
};

class IlluminationSection
{
  public:
    static bool Construct(ControlColumn& Page, RenderPipelineConfiguration& Parameters, const RenderSettingsReading& Reading);
};

class ReflectionSection
{
  public:
    static bool Construct(ControlColumn& Page, RenderPipelineConfiguration& Parameters, const RenderSettingsReading& Reading);
};

class ShadowSection
{
  public:
    static bool Construct(ControlColumn& Page, RenderPipelineConfiguration& Parameters, const RenderSettingsReading& Reading);
};

class DenoiseSection
{
  public:
    static bool Construct(ControlColumn& Page, RenderPipelineConfiguration& Parameters, const RenderSettingsReading& Reading);
};

class ExposureSection
{
  public:
    static bool Construct(ControlColumn& Page, RenderPipelineConfiguration& Parameters, const RenderSettingsReading& Reading);
};

class GeometrySection
{
  public:
    static bool Construct(ControlColumn& Page, RenderPipelineConfiguration& Parameters, const RenderSettingsReading& Reading);
};

class ViewSection
{
  public:
    static bool Construct(ControlColumn& Page, RenderPipelineConfiguration& Parameters, const RenderSettingsReading& Reading);
};

class TimingSection
{
  public:
    static bool Construct(ControlColumn& Page, RenderPipelineConfiguration& Parameters, const RenderSettingsReading& Reading);
};

} // namespace Frontier
