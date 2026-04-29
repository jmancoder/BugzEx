include(FetchContent)


set(REDSCORE_LOCAL_DIR "/home/red_eye/CLionProjects/RedsCore")
if(EXISTS "${REDSCORE_LOCAL_DIR}/CMakeLists.txt")
    add_subdirectory(
            "${REDSCORE_LOCAL_DIR}"
            "${CMAKE_BINARY_DIR}/_deps/RedsCore-build"
    )
else()
    FetchContent_Declare(
            RedsCore
            GIT_REPOSITORY https://github.com/REDxEYE/RedsCore.git
            GIT_TAG origin/master
            GIT_SHALLOW TRUE
            GIT_REMOTE_UPDATE_STRATEGY CHECKOUT
            GIT_PROGRESS TRUE
    )
    FetchContent_MakeAvailable(RedsCore)
endif()

FetchContent_Declare(
        SQLiteCpp
        QUIET
        GIT_REPOSITORY https://github.com/SRombauts/SQLiteCpp.git
        GIT_TAG 3.3.3
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(SQLiteCpp)
