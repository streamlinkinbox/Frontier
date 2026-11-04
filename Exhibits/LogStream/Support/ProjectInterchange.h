/*==========================================================================================================================================
                                           RECREATED HOST CONTRACT — VALIDATION SCAFFOLDING ONLY
                                     ------------------------------------------------------------------
                                       The real ProjectInterchange.h ships with the host SDK from the
                                    upstream checkout (see Frontier/Projects/Project-Zero/Upstream.json)
                                     and is absent here. This header recreates the v3 contract exactly
                                        as exercised by the committed Project-Networking interchange
                                     implementation so the Project Zero code image can be compile- and
                                        negotiation-tested headlessly. Never ship a host against it;
                                               replace it with the SDK header when available.
==========================================================================================================================================*/

// 🧩 Scaffold v3 host C ABI: negotiation export, project lifecycle table, host diagnostics channel.

#pragma once

#include <cstddef>
#include <cstdint>

#define FRONTIER_CODE_IMAGE_CALL
#define FRONTIER_CODE_INTERCHANGE_NUMBER 3u
#define FRONTIER_INTERFACE_FINGERPRINT 0xb6725f24d0869173ull

struct FrontierProjectDiagnostic
{
    uint32_t    StructureSize = 0;
    const char* SubjectName   = nullptr;   // [-] - e.g. "Project-Zero/Foliage"
    const char* Explanation   = nullptr;   // [-] - Diagnostic text
};

using FrontierReceiveDiagnostic = void(FRONTIER_CODE_IMAGE_CALL*)(
    const FrontierProjectDiagnostic* Diagnostic, void* ProjectReception);

struct FrontierHostInterchange
{
    uint32_t                  StructureSize    = 0;
    FrontierReceiveDiagnostic ReceiveDiagnostic = nullptr;
    void*                     ProjectReception = nullptr;
};

using FrontierConstructProject = bool(FRONTIER_CODE_IMAGE_CALL*)(
    const FrontierHostInterchange* HostInterchange, void* ProjectReception);
using FrontierAdvanceProject   = void(FRONTIER_CODE_IMAGE_CALL*)();
using FrontierRetireProject    = void(FRONTIER_CODE_IMAGE_CALL*)();

struct FrontierProjectInterchange
{
    uint32_t               StructureSize   = 0;
    FrontierConstructProject ConstructProject = nullptr;
    FrontierAdvanceProject   AdvanceProject   = nullptr;
    FrontierRetireProject    RetireProject    = nullptr;
};

extern "C" bool FRONTIER_CODE_IMAGE_CALL ConstructProjectInterchange(
    uint32_t InterchangeNumber, uint64_t InterfaceFingerprint,
    FrontierProjectInterchange* InterchangeDelivery, char* RefusalExplanation,
    size_t RefusalExplanationCapacity);
