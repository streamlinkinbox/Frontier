/*============================================================================================================================================
                                                         QUERYELISIONGPU.SLANG
============================================================================================================================================*/
// 🧩 Elide exact-zero and duplicate packet-local queries while retaining the traced quartet estimator.
#define QUARTET_QUERY_ELISION 1
#include "TracedQuartetGpu.slang"
