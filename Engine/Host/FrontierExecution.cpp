//============================================================================================================================================
//                                                     FRONTIEREXECUTION.CPP
//============================================================================================================================================
// 📦 The only windowed process entry; all projects open through this Frontier.exe entry point.

#include "FrontierHost.h"

int main(int ArgumentCount, char** ArgumentVector)
{
    return Frontier::RunFrontierHost(ArgumentCount, ArgumentVector);
}
