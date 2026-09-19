#=============================================================================================================================================
# 📦 Frontier/BuildConfiguration/WindowsConfiguration.cmake — Windows MSVC Compiler Definitions
#=============================================================================================================================================

if(MSVC)
    add_compile_options(/W4 /WX /wd4324 /permissive- /Zc:preprocessor /MD$<$<CONFIG:Debug>:d>)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    add_compile_options(-Wall -Wextra -Werror -pedantic)
endif()

add_compile_definitions(FRONTIER_DEVELOPMENT)
