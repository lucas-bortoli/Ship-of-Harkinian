# Build the deprecated but MIT-licensed discord-rpc client directly from pinned source.
# Populate the repositories without adding upstream's legacy top-level build files.
include(FetchContent)

cmake_policy(PUSH)
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

FetchContent_Declare(
    discord_rpc
    GIT_REPOSITORY https://github.com/discord/discord-rpc.git
    GIT_TAG 963aa9f3e5ce81a4682c6ca3d136cddda614db33
    GIT_SHALLOW FALSE
)
FetchContent_Declare(
    discord_rpc_rapidjson
    GIT_REPOSITORY https://github.com/Tencent/rapidjson.git
    GIT_TAG f54b0e47a08782a6131cc3d60f94d038fa6e0a51
    GIT_SHALLOW FALSE
)

FetchContent_GetProperties(discord_rpc)
if(NOT discord_rpc_POPULATED)
    FetchContent_Populate(discord_rpc)
endif()
FetchContent_GetProperties(discord_rpc_rapidjson)
if(NOT discord_rpc_rapidjson_POPULATED)
    FetchContent_Populate(discord_rpc_rapidjson)
endif()
cmake_policy(POP)

set(DISCORD_RPC_SOURCES
    "${discord_rpc_SOURCE_DIR}/src/discord_rpc.cpp"
    "${discord_rpc_SOURCE_DIR}/src/rpc_connection.cpp"
    "${discord_rpc_SOURCE_DIR}/src/serialization.cpp"
)

if(WIN32)
    list(APPEND DISCORD_RPC_SOURCES
        "${discord_rpc_SOURCE_DIR}/src/connection_win.cpp"
        "${discord_rpc_SOURCE_DIR}/src/discord_register_win.cpp"
    )
    set(DISCORD_RPC_PLATFORM_DEFINITION DISCORD_WINDOWS)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND DISCORD_RPC_SOURCES
        "${discord_rpc_SOURCE_DIR}/src/connection_unix.cpp"
        "${discord_rpc_SOURCE_DIR}/src/discord_register_linux.cpp"
    )
    set(DISCORD_RPC_PLATFORM_DEFINITION DISCORD_LINUX)
else()
    message(FATAL_ERROR "discord-rpc has no configured transport for ${CMAKE_SYSTEM_NAME}")
endif()

add_library(discord-rpc STATIC ${DISCORD_RPC_SOURCES})
add_library(discord-rpc::discord-rpc ALIAS discord-rpc)
target_include_directories(discord-rpc
    PRIVATE "${discord_rpc_SOURCE_DIR}/include" "${discord_rpc_SOURCE_DIR}/src" "${discord_rpc_rapidjson_SOURCE_DIR}/include"
)
target_compile_definitions(discord-rpc PRIVATE ${DISCORD_RPC_PLATFORM_DEFINITION})
target_compile_features(discord-rpc PRIVATE cxx_std_14)
set_target_properties(discord-rpc PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

if(MSVC)
    set_target_properties(discord-rpc PROPERTIES
        MSVC_RUNTIME_LIBRARY "$<IF:$<CONFIG:Debug>,MultiThreadedDebug,MultiThreaded>"
    )
    target_compile_options(discord-rpc PRIVATE /EHsc /w /utf-8)
    target_link_libraries(discord-rpc PRIVATE psapi advapi32)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(discord-rpc PRIVATE pthread)
else()
    target_link_libraries(discord-rpc PRIVATE psapi advapi32)
endif()

install(FILES
    "${discord_rpc_SOURCE_DIR}/LICENSE"
    DESTINATION licenses/discord-rpc
    RENAME LICENSE.txt
    COMPONENT ship
)
install(FILES
    "${discord_rpc_rapidjson_SOURCE_DIR}/license.txt"
    DESTINATION licenses/rapidjson
    RENAME LICENSE.txt
    COMPONENT ship
)
