cmake_minimum_required(VERSION 4.3.0)

project(
	F4MP
	VERSION 0.1.0
	DESCRIPTION "Fallout 4 Multiplayer"
	LANGUAGES CXX
)

if("${PROJECT_SOURCE_DIR}" STREQUAL "${PROJECT_BINARY_DIR}")
	message(FATAL_ERROR "In-source builds are not allowed. Use a CMake preset (see CMakePresets.json).")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(F4MP_BUILD_CLIENT "Build the F4SE client plugin (F4MP.dll)" ON)
option(F4MP_BUILD_SERVER "Build the dedicated server (F4MPServer.exe)" ON)

find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(unofficial-enet CONFIG REQUIRED)

# --- Shared protocol (header-only) ---
add_library(f4mp_shared INTERFACE)
add_library(F4MP::Shared ALIAS f4mp_shared)
target_include_directories(f4mp_shared INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/shared/include")
target_compile_definitions(f4mp_shared INTERFACE
	"F4MP_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}"
	"F4MP_VERSION_MINOR=${PROJECT_VERSION_MINOR}"
	"F4MP_VERSION_PATCH=${PROJECT_VERSION_PATCH}"
	"F4MP_VERSION_STRING=\"${PROJECT_VERSION}\""
)

if(F4MP_BUILD_SERVER)
	add_subdirectory(server)
endif()

if(F4MP_BUILD_CLIENT)
	add_subdirectory(client)
endif()
