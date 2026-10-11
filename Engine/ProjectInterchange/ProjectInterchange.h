//============================================================================================================================================
//                                                     PROJECTINTERCHANGE.H
//============================================================================================================================================
// 📦 Versioned C-layout records crossing from Frontier.exe into project code images.

#pragma once

#include <stdint.h>

#if defined(_WIN32)
#define FRONTIER_CODE_IMAGE_EXPORT __declspec(dllexport)
#define FRONTIER_CODE_IMAGE_CALL __cdecl
#else
#define FRONTIER_CODE_IMAGE_EXPORT __attribute__((visibility("default")))
#define FRONTIER_CODE_IMAGE_CALL
#endif

#ifdef __cplusplus
extern "C"
{
#endif

enum
{
    // Revision 3 adds transport, pause, stepping and vehicle input readings. Earlier images are refused.
    FrontierCodeInterchangeNumber = 3u,
    FrontierProjectInterchangeMaximumText = 1024u,
};

// Revision-3 C-layout fingerprint. An MSVC C enum would truncate this to 32 bits.
static const uint64_t FrontierCodeInterchangeFingerprint = UINT64_C(0xb6725f24d0869173);

typedef enum FrontierProjectRefusalNumber
{
    FrontierProjectRefusalNone = 0u,
    FrontierProjectRefusalStructure = 1u,
    FrontierProjectRefusalInterchange = 2u,
    FrontierProjectRefusalFingerprint = 3u,
    FrontierProjectRefusalConstruction = 4u,
    FrontierProjectRefusalAdvance = 5u,
} FrontierProjectRefusalNumber;

typedef struct FrontierProjectRefusal
{
    uint32_t Number;                                                   // [-] - precise refusal reading
    char     Explanation[FrontierProjectInterchangeMaximumText];      // [-] - UTF-8 diagnostic owned by the caller
} FrontierProjectRefusal;

typedef struct FrontierProjectLaunch
{
    uint32_t    StructureSize;             // [B] - guards additions at this C edge
    const char* ProjectName;               // [-] - project identity, owned by FrontierHost
    const char* SpecificationLocation;     // [-] - absolute .frontier location, owned by FrontierHost
    const char* ContentLocation;           // [-] - absolute project content location, owned by FrontierHost
    const char* OpeningSceneLocation;      // [-] - absolute opening scene location, owned by FrontierHost
} FrontierProjectLaunch;

typedef struct FrontierProjectInputReading FrontierProjectInputReading;

typedef struct FrontierProjectCycle
{
    uint32_t                            StructureSize;                 // [B] - guards additions at this C edge
    float                               ElapsedSeconds;                // [s] - monotonic project-running duration
    float                               CycleSeconds;                  // [s] - completed host display-cycle duration
    const FrontierProjectInputReading*  InputReading;                  // [-] - host-owned read-only input for this cycle
} FrontierProjectCycle;

struct FrontierProjectInputReading
{
    uint32_t StructureSize;                 // [B] - guards additions at this C edge
    float    PointerX;                      // [px] - window-local pointer horizontal position
    float    PointerY;                      // [px] - window-local pointer vertical position
    float    MoveAxisX;                     // [-] - project-neutral lateral input reading
    float    MoveAxisY;                     // [-] - project-neutral forward input reading
    uint32_t PrimaryPressed;                // [-] - primary interaction reading
    uint32_t SecondaryPressed;              // [-] - secondary interaction reading
    uint32_t TransportNumber;               // [-] - 0 edit, 1 play, 2 simulate
    uint32_t Paused;                        // [-] - suspend time integration
    uint32_t SimulationStep;                // [-] - consume one fixed display interval while paused
    uint32_t HandbrakePressed;              // [-] - vehicle parking/drift input
    uint32_t ResetPressed;                  // [-] - reset to the project spawn
    uint32_t KeyboardCaptured;              // [-] - editing text or a modal control owns keyboard input
};

typedef struct FrontierProjectSceneMutation
{
    uint32_t    StructureSize;              // [B] - guards additions at this C edge
    uint32_t    MutationNumber;             // [-] - project-declared scene mutation reading
    const char* SubjectName;                // [-] - stable project-local mutation subject
    float       Transform[16];              // [-] - column-major world transform request
} FrontierProjectSceneMutation;

typedef struct FrontierProjectCameraRequest
{
    uint32_t StructureSize;                 // [B] - guards additions at this C edge
    float    Eye[3];                        // [m] - requested camera world position
    float    Forward[3];                    // [-] - requested unit viewing direction
    float    VerticalFieldOfView;           // [rad] - requested vertical aperture
} FrontierProjectCameraRequest;

typedef struct FrontierProjectRenderingPreference
{
    uint32_t StructureSize;                 // [B] - guards additions at this C edge
    uint32_t RenderModeNumber;              // [-] - project preference; engine selects supported shared mode
    uint32_t IndirectIlluminationEnabled;   // [-] - shared ReSTIR or Surfel-GI preference
} FrontierProjectRenderingPreference;

typedef struct FrontierProjectPanel
{
    uint32_t    StructureSize;              // [B] - guards additions at this C edge
    const char* StableName;                 // [-] - project-local editor panel identity
    const char* DisplayName;                // [-] - presentation label, owned by project code
} FrontierProjectPanel;

typedef struct FrontierProjectDiagnostic
{
    uint32_t    StructureSize;              // [B] - guards additions at this C edge
    uint32_t    SeverityNumber;             // [-] - project diagnostic severity reading
    const char* SubjectName;                // [-] - project diagnostic subject
    const char* Explanation;                // [-] - project diagnostic detail
} FrontierProjectDiagnostic;

typedef void(FRONTIER_CODE_IMAGE_CALL* FrontierProjectSceneMutationReception)(
    const FrontierProjectSceneMutation* ActiveMutation,
    void* ProjectReception);
typedef void(FRONTIER_CODE_IMAGE_CALL* FrontierProjectCameraRequestReception)(
    const FrontierProjectCameraRequest* ActiveRequest,
    void* ProjectReception);
typedef void(FRONTIER_CODE_IMAGE_CALL* FrontierProjectRenderingPreferenceReception)(
    const FrontierProjectRenderingPreference* ActivePreference,
    void* ProjectReception);
typedef void(FRONTIER_CODE_IMAGE_CALL* FrontierProjectPanelReception)(
    const FrontierProjectPanel* ActivePanel,
    void* ProjectReception);
typedef void(FRONTIER_CODE_IMAGE_CALL* FrontierProjectDiagnosticReception)(
    const FrontierProjectDiagnostic* ActiveDiagnostic,
    void* ProjectReception);

typedef struct FrontierProjectHostInterchange
{
    uint32_t                                      StructureSize;       // [B] - guards additions at this C edge
    const FrontierProjectInputReading*            InputReading;        // [-] - host-owned read-only current input
    FrontierProjectSceneMutationReception         ReceiveSceneMutation;// [-] - engine-owned scene mutation reception
    FrontierProjectCameraRequestReception         ReceiveCameraRequest; // [-] - engine-owned camera request reception
    FrontierProjectRenderingPreferenceReception   ReceiveRenderingPreference;// [-] - engine-owned render preference reception
    FrontierProjectPanelReception                 ReceivePanel;        // [-] - engine-owned panel declaration reception
    FrontierProjectDiagnosticReception            ReceiveDiagnostic;   // [-] - engine-owned diagnostic reception
    void*                                         ProjectReception;     // [-] - engine-owned reception context
} FrontierProjectHostInterchange;

typedef uint32_t(FRONTIER_CODE_IMAGE_CALL* FrontierProjectConstruct)(
    const FrontierProjectLaunch* ActiveLaunch,
    const FrontierProjectHostInterchange* HostInterchange,
    void** ProjectRecord,
    FrontierProjectRefusal* Refusal);

typedef uint32_t(FRONTIER_CODE_IMAGE_CALL* FrontierProjectAdvance)(
    void* ProjectRecord,
    const FrontierProjectCycle* ActiveCycle,
    FrontierProjectRefusal* Refusal);

typedef void(FRONTIER_CODE_IMAGE_CALL* FrontierProjectRetire)(void* ProjectRecord);

typedef struct FrontierProjectInterchange
{
    uint32_t                    StructureSize;            // [B] - guards additions at this C edge
    uint32_t                    CodeInterchangeNumber;    // [-] - C-layout interchange revision
    uint64_t                    InterfaceFingerprint;      // [-] - exact callback-record layout fingerprint
    FrontierProjectConstruct    ConstructProject;          // [-] - project-owned scene and simulation construction
    FrontierProjectAdvance      AdvanceProject;            // [-] - one project simulation cycle
    FrontierProjectRetire       RetireProject;             // [-] - project-memory retirement before image release
} FrontierProjectInterchange;

typedef uint32_t(FRONTIER_CODE_IMAGE_CALL* FrontierConstructProjectInterchange)(
    uint32_t RequestedInterchangeNumber,
    uint64_t RequestedFingerprint,
    FrontierProjectInterchange* DeliveredInterchange,
    FrontierProjectRefusal* Refusal);

FRONTIER_CODE_IMAGE_EXPORT uint32_t FRONTIER_CODE_IMAGE_CALL ConstructProjectInterchange(
    uint32_t RequestedInterchangeNumber,
    uint64_t RequestedFingerprint,
    FrontierProjectInterchange* DeliveredInterchange,
    FrontierProjectRefusal* Refusal);

#ifdef __cplusplus
}
#endif
