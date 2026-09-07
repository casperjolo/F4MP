project(F4MPTests LANGUAGES CXX)

# Everything here is engine-free so it runs on any machine that can build the server, including
# CI. The client plugin's engine calls cannot be covered this way; they need the game.

add_executable(f4mp_unit_tests "${CMAKE_CURRENT_SOURCE_DIR}/UnitTests.cpp")
target_include_directories(f4mp_unit_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(f4mp_unit_tests PRIVATE F4MP::Shared)

add_executable(f4mp_smoke_test "${CMAKE_CURRENT_SOURCE_DIR}/SmokeTest.cpp")
target_include_directories(f4mp_smoke_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(f4mp_smoke_test PRIVATE F4MP::Shared unofficial::enet::enet)

if(NOT TARGET F4MPServer)
	message(FATAL_ERROR "The smoke test needs the server target. Configure with F4MP_BUILD_SERVER=ON.")
endif()

# The smoke test launches the server it was built alongside.
target_compile_definitions(f4mp_smoke_test PRIVATE "F4MP_SERVER_EXE=\"$<TARGET_FILE:F4MPServer>\"")
add_dependencies(f4mp_smoke_test F4MPServer)

foreach(target f4mp_unit_tests f4mp_smoke_test)
	if(WIN32)
		target_compile_definitions(${target} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
		target_link_libraries(${target} PRIVATE ws2_32 winmm)
	endif()
	if(MSVC)
		target_compile_options(${target} PRIVATE /W4 /utf-8 /permissive- /EHsc /Zc:preprocessor)
	else()
		target_compile_options(${target} PRIVATE -Wall -Wextra)
	endif()
	set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist/tests$<0:>")
endforeach()

add_test(NAME unit COMMAND f4mp_unit_tests)
add_test(NAME smoke COMMAND f4mp_smoke_test)
set_tests_properties(smoke PROPERTIES TIMEOUT 120)
