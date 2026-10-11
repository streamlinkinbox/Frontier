//============================================================================================================================================
//                                                      UFBXTRANSLATION.CPP
//============================================================================================================================================
// 🧩 The single translation unit that compiles ufbx (ExternalPackages/ufbx/ufbx.c, header-only style, C++-clean per its
//    README). Kept as a .cpp so both build lists (CMake and ToolchainSequence.ps1) stay C++-only and no C-language flags
//    are needed. Nothing else in the engine may include ufbx.c.

#if defined(_MSC_VER)
    #pragma warning(push, 0)
#elif defined(__GNUC__) || defined(__clang__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wall"
    #pragma GCC diagnostic ignored "-Wextra"
#endif

#include <ufbx.c>

#if defined(_MSC_VER)
    #pragma warning(pop)
#elif defined(__GNUC__) || defined(__clang__)
    #pragma GCC diagnostic pop
#endif
