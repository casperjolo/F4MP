project(F4MPClient VERSION 0.1.0.0 LANGUAGES CXX RC)

# --- Plugin metadata (consumed by CommonLibF4's resource templates) ---

set(COMMONLIB_PLUGIN_NAME "F4MP")
set(COMMONLIB_PLUGIN_AUTHOR "F4MP contributors")
set(COMMONLIB_PLUGIN_VERSION "${PROJECT_VERSION}")
set(COMMONLIB_PLUGIN_LICENSE "MIT License")
set(COMMONLIB_PLUGIN_DESCRIPTION "Fallout 4 Multiplayer client plugin")
set(COMMONLIB_PLUGIN_COMMENTS "https://github.com/casperjolo/F4MP")
string(TIMESTAMP COMMONLIB_PLUGIN_COPYRIGHT_YEAR "%Y" UTC)

# --- Which game runtimes the single DLL supports ---
# NG = 1.10.984 (next-gen update), AE = 1.11.x (current). OG 1.10.163 is not supported.

set(COMMONLIB_RUNTIME_VR OFF)
set(COMMONLIB_RUNTIME_OG OFF)
set(COMMONLIB_RUNTIME_NG ON)
set(COMMONLIB_RUNTIME_AE ON)

set(COMMONLIB_OPTION_FMT ON)
set(COMMONLIB_OPTION_XBYAK OFF)
set(COMMONLIB_OPTION_INI OFF)
set(COMMONLIB_OPTION_TOML OFF)
set(COMMONLIB_OPTION_JSON OFF)

set(COMMONLIB_ROOT_DIR "${CMAKE_SOURCE_DIR}/lib/CommonLibF4")
if(NOT EXISTS "${COMMONLIB_ROOT_DIR}/CMakeLists.txt")
	message(FATAL_ERROR "CommonLibF4 submodule is missing. Run: git submodule update --init --recursive")
endif()

include("${COMMONLIB_ROOT_DIR}/cmake/GenerateXsePluginInfo.cmake")
include("${COMMONLIB_ROOT_DIR}/cmake/SetXseOutputPath.cmake")
include("${COMMONLIB_ROOT_DIR}/cmake/CopyXseOutput.cmake")

# Generates the Win32 version resource. The plugin version block itself lives in src/PluginInfo.cpp
# so that 1.11.240 can be listed explicitly.
generate_xse_plugin_info("${COMMONLIB_ROOT_DIR}")

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)

if(NOT TARGET CommonLibF4)
	add_subdirectory("${COMMONLIB_ROOT_DIR}" CommonLibF4)
endif()

# CommonLibF4 exports /WX to consumers. Keep warnings visible but non-fatal while the project is young.
get_target_property(_clf4_compile_options CommonLibF4 INTERFACE_COMPILE_OPTIONS)
if(_clf4_compile_options)
	list(REMOVE_ITEM _clf4_compile_options "/WX")
	set_target_properties(CommonLibF4 PROPERTIES INTERFACE_COMPILE_OPTIONS "${_clf4_compile_options}")
endif()
get_target_property(_clf4_link_options CommonLibF4 INTERFACE_LINK_OPTIONS)
if(_clf4_link_options)
	list(REMOVE_ITEM _clf4_link_options "/WX")
	set_target_properties(CommonLibF4 PROPERTIES INTERFACE_LINK_OPTIONS "${_clf4_link_options}")
endif()

# --- Sources ---

set(PCH_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/PCH.hpp")

file(GLOB_RECURSE CLIENT_SOURCES CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
)
list(REMOVE_ITEM CLIENT_SOURCES "${PCH_SOURCE}")

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${CLIENT_SOURCES})

add_library(F4MP SHARED ${CLIENT_SOURCES} "${WIN32_VERSION_TARGET}")

target_include_directories(
	F4MP
	PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/src"
		"${CMAKE_CURRENT_BINARY_DIR}/include"
)

target_precompile_headers(F4MP PRIVATE "${PCH_SOURCE}")

target_link_libraries(
	F4MP
	PRIVATE
		CommonLibF4::CommonLibF4
		F4MP::Shared
		unofficial::enet::enet
		ws2_32 # ENet sockets
		winmm  # ENet timers (timeGetTime / timeBeginPeriod); vcpkg's enet config does not add it
)

target_compile_options(F4MP PRIVATE "/WX-")

# --- Older STL compatibility ---
# CommonLibF4's precompiled header includes the C++23 <flat_map> and <flat_set>, which the VS 2022
# STL (MSVC 14.44) does not ship, although the fork never uses either type. When the real headers
# are missing, put empty stand-ins from client/compat on the include path of everything that
# compiles that PCH. VS 2026 has the headers and is left untouched.
include(CheckIncludeFileCXX)
set(CMAKE_REQUIRED_FLAGS "/std:c++latest")
check_include_file_cxx("flat_map" F4MP_STL_HAS_FLAT_MAP)
unset(CMAKE_REQUIRED_FLAGS)
if(NOT F4MP_STL_HAS_FLAT_MAP)
	message(STATUS "F4MP: this MSVC STL has no <flat_map>/<flat_set>; using stand-ins from client/compat")
	target_include_directories(CommonLibF4 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/compat")
	target_include_directories(F4MP PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/compat")
endif()

# --- Output: build/<preset>/dist/client mirrors the game folder layout ---

set_target_properties(
	F4MP
	PROPERTIES
		OUTPUT_NAME "F4MP"
		PREFIX ""
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist/client/Data/F4SE/Plugins$<0:>"
		LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist/client/Data/F4SE/Plugins$<0:>"
		PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist/client/Data/F4SE/Plugins$<0:>"
)

add_custom_command(
	TARGET F4MP POST_BUILD
	COMMAND "${CMAKE_COMMAND}" -E copy_if_different
		"${CMAKE_CURRENT_SOURCE_DIR}/res/F4MP.ini"
		"$<TARGET_FILE_DIR:F4MP>/F4MP.ini"
)

# Optional: set XSE_FO4_GAME_PATH (or XSE_FO4_MODS_PATH) in the environment to also copy the DLL
# straight into the game's Data/F4SE/Plugins after every build.
set_xse_output_path(F4MP)
copy_xse_copy_output(F4MP)
