//============================================================================================================================================
//                                                   PROJECTOPENINGSEQUENCE.H
//============================================================================================================================================
// 📦 Native project browser before the renderer starts; explicit child-ready handshake retains startup diagnostics.

#pragma once

namespace Frontier
{
/// 📦 Shows the host-owned project/scene card; launching never invokes a batch file or command shell.
int RunProjectBrowser(const char* ProofImage = nullptr);

/// 📦 Releases the browser only after the project renderer has presented its first frame.
void CompleteProjectOpening(int ArgumentCount, char** Arguments) noexcept;
}
