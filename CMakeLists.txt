cmake_minimum_required(VERSION 3.21)

if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
	set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

set(VCPKG_TARGET_TRIPLET "x64-windows-static-md" CACHE STRING "")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" CACHE STRING "")

project(HeadshotsKill VERSION 1.0.0 LANGUAGES CXX)

if(DEFINED ENV{SKYRIM_FOLDER} AND IS_DIRECTORY "$ENV{SKYRIM_FOLDER}/Data")
	set(OUTPUT_FOLDER "$ENV{SKYRIM_FOLDER}/Data")
endif()

if(DEFINED ENV{SKYRIM_MODS_FOLDER} AND IS_DIRECTORY "$ENV{SKYRIM_MODS_FOLDER}")
	set(OUTPUT_FOLDER "$ENV{SKYRIM_MODS_FOLDER}/${PROJECT_NAME}")
endif()

find_package(CommonLibSSE CONFIG REQUIRED)
add_commonlibsse_plugin(${PROJECT_NAME} SOURCES
	src/main.cpp
	src/Settings.cpp
	src/Menu.cpp
	src/Hooks.cpp
	src/HeadshotLogic.cpp
	src/HeadshotSound.cpp
	src/PlayerHelmetTracker.cpp
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)

target_compile_definitions(${PROJECT_NAME} PRIVATE
	_UNICODE
	UNICODE
)

set(_compile_plugins_dir "${CMAKE_SOURCE_DIR}/Compile/SKSE/Plugins")
set_target_properties(${PROJECT_NAME} PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY "${_compile_plugins_dir}"
	RUNTIME_OUTPUT_DIRECTORY_DEBUG "${_compile_plugins_dir}"
	RUNTIME_OUTPUT_DIRECTORY_RELEASE "${_compile_plugins_dir}"
	LIBRARY_OUTPUT_DIRECTORY "${_compile_plugins_dir}"
	LIBRARY_OUTPUT_DIRECTORY_DEBUG "${_compile_plugins_dir}"
	LIBRARY_OUTPUT_DIRECTORY_RELEASE "${_compile_plugins_dir}"
)

add_custom_command(
	TARGET "${PROJECT_NAME}"
	POST_BUILD
	COMMAND "${CMAKE_COMMAND}" -E make_directory "${_compile_plugins_dir}"
	COMMAND "${CMAKE_COMMAND}" -E copy_if_different
		"${CMAKE_SOURCE_DIR}/HeadshotsKill.ini"
		"${_compile_plugins_dir}/HeadshotsKill.ini"
	VERBATIM
)

if(DEFINED OUTPUT_FOLDER)
	set(DLL_FOLDER "${OUTPUT_FOLDER}/SKSE/Plugins")
	message(STATUS "SKSE plugin output folder: ${DLL_FOLDER}")

	add_custom_command(
		TARGET "${PROJECT_NAME}"
		POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E make_directory "${DLL_FOLDER}"
		COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
		VERBATIM
	)

	if(CMAKE_BUILD_TYPE STREQUAL "Debug")
		add_custom_command(
			TARGET "${PROJECT_NAME}"
			POST_BUILD
			COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_PDB_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_PDB_FILE_NAME:${PROJECT_NAME}>"
			VERBATIM
		)
	endif()
endif()
