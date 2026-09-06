project(F4MPServer VERSION 0.1.0 LANGUAGES CXX)

file(GLOB_RECURSE SERVER_SOURCES CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
)

add_executable(F4MPServer ${SERVER_SOURCES})

target_include_directories(F4MPServer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries(
	F4MPServer
	PRIVATE
		F4MP::Shared
		unofficial::enet::enet
		spdlog::spdlog
		fmt::fmt
)

if(WIN32)
	target_compile_definitions(F4MPServer PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
	target_link_libraries(F4MPServer PRIVATE ws2_32 winmm)
endif()

if(MSVC)
	target_compile_options(F4MPServer PRIVATE /W4 /utf-8 /permissive- /EHsc /Zc:preprocessor /Zi)
	target_link_options(F4MPServer PRIVATE /DEBUG:FULL)
else()
	target_compile_options(F4MPServer PRIVATE -Wall -Wextra)
endif()

# Everything the server needs to run ends up in build/<preset>/dist/server.
set_target_properties(
	F4MPServer
	PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist/server$<0:>"
		PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist/server$<0:>"
)

add_custom_command(
	TARGET F4MPServer POST_BUILD
	COMMAND "${CMAKE_COMMAND}" -E copy_if_different
		"${CMAKE_CURRENT_SOURCE_DIR}/res/server.ini"
		"$<TARGET_FILE_DIR:F4MPServer>/server.ini"
)
