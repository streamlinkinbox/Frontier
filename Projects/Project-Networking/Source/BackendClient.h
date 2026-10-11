//============================================================================================================================================
//                                                          BACKENDCLIENT.H
//============================================================================================================================================
// Talks to the Charge backend (Backend/) for premium + coin balance. Synchronous lookup at
// transport start; any failure yields Queried=false so callers fall back to the manual toggle.

#pragma once
#include "EpicExchange.h" // DiagnosticReception

namespace Networking
{
struct BackendVerdict
{
    bool Queried = false; // a backend round-trip completed and parsed
    bool Premium = false;
    int Coins = 0;
};

// Remembers which Epic account the premium verdict is for (copied, truncated,
// cleared when null). Call after EOS login reaches Connected, before transport start.
void SetBackendIdentity(const char* EpicAccountId) noexcept;

// Reads Charge.backend.ini (version=1, backend_url, backend_key) from the same folders
// as Charge.local.ini and queries GET /v1/premium. Emits one redacted line (never secrets).
BackendVerdict QueryBackendPremium(DiagnosticReception Emit) noexcept;
}
