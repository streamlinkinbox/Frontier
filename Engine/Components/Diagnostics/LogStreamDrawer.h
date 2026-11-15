/*============================================================================================================================================
                                                            LOGSTREAMDRAWER.H
============================================================================================================================================*/
// 📜 Reusable collapsible live diagnostic event stream drawer with severity filters & clear/copy actions

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlAppearance.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class LogStreamSeverity : uint8_t
{
    Notice  = 0u,
    Success = 1u,
    Warning = 2u,
    Error   = 3u,
};

enum class LogStreamFilter : uint8_t
{
    All     = 0u,
    Compute = 1u,
    Codec   = 2u,
    Force   = 3u,
    Rig     = 4u,
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct LogStreamEntry
{
    std::string       TimestampText { "00:00.00" };
    std::string       CategoryTag   { "Compute" };
    std::string       Message       {};
    LogStreamSeverity Severity      = LogStreamSeverity::Notice;
};

struct LogStreamDrawerSpec
{
    bool             Expanded     = false;
    LogStreamFilter  ActiveFilter = LogStreamFilter::All;
    bool             ClearClicked = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

namespace LogStreamDrawer
{

void Construct(
    PixelSpace&                     Surface,
    Rectangle2D                     Bounds,
    std::span<const LogStreamEntry> Entries,
    LogStreamDrawerSpec&            Spec,
    const ControlPointer&           Pointer,
    float                           Opacity = 1.0f) noexcept;

} // namespace LogStreamDrawer

} // namespace Frontier
