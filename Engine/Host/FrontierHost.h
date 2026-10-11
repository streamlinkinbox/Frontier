//============================================================================================================================================
//                                                        FRONTIERHOST.H
//============================================================================================================================================
// 📦 Sole executable host that resolves one project specification before shared facilities begin.

#pragma once

namespace Frontier
{

/// 📦 Resolves one .frontier opening and transfers it into the shared windowed Frontier runtime.
/// in    ArgumentCount   [-]  command-line reading supplied by the operating system
/// in    ArgumentVector  [-]  command-line text supplied by the operating system
/// out   ExitNumber      [-]  process completion reading
int RunFrontierHost(int ArgumentCount, char** ArgumentVector);

} // namespace Frontier
