cmake_minimum_required (VERSION 3.28.0)
project(c2c_benchmark)

################################
# general config
################################

# print compiler
message("-- use c compiler ${CMAKE_C_COMPILER}")
message("-- use c++ compiler ${CMAKE_CXX_COMPILER}")

# set compile parameter
if (NOT CMAKE_CROSSCOMPILING)
	if (${CMAKE_C_COMPILER_ID} STREQUAL GNU)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
	elseif (${CMAKE_C_COMPILER_ID} MATCHES Clang)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
	elseif (${CMAKE_C_COMPILER_ID} STREQUAL MSVC)
		add_definitions(-D_CRT_SECURE_NO_WARNINGS=1 -D_UNICODE -DUNICODE)
		add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
		add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
	endif()

	# NOTE: by default, without -march=native
	#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=native")
	#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native")
else()
	if (ANDROID)
		message("-- cmake run cross compiling, building for android")
	else()
		message(FATAL_ERROR "-- building for unsupport platform")
	endif()
endif()

# set standard and print features
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

message("-- c compiler support features: ")
foreach(feature ${CMAKE_C_COMPILE_FEATURES})
	message("support feature: ${feature}")
endforeach()

# set output directory
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# for LSP
if (NOT ${CMAKE_CXX_COMPILER_ID} STREQUAL MSVC)
	set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

# set use folder in vs
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

################################
# options
################################

option(BUILD_SHARED_LIBS "Build shared or static library" ON)
option(BUILD_TESTING "Build testing" OFF)
option(BUILD_SANITIZER "Compile with sanitizer" OFF)

################################
# deps
################################

include(FetchContent)
set(FETCHCONTENT_BASE_DIR ${CMAKE_BINARY_DIR}/_deps)

# set mugglec compile options
set(MUGGLE_BUILD_TRACE OFF CACHE BOOL "")
set(MUGGLE_BUILD_SHARED_LIB OFF CACHE BOOL "")
set(MUGGLE_BUILD_STATIC_PIC ON CACHE BOOL "")
set(MUGGLE_BUILD_BENCHMARK OFF CACHE BOOL "")
set(MUGGLE_BUILD_TESTING OFF CACHE BOOL "")
set(MUGGLE_BUILD_EXAMPLE OFF CACHE BOOL "")

message("# fetch mugglec")
FetchContent_Declare(
	mugglec
	GIT_REPOSITORY https://github.com/MuggleWei/mugglec.git
	GIT_TAG v2.1.4
	GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(mugglec)

################################
# app
################################
FILE(GLOB_RECURSE tmp_h ${CMAKE_CURRENT_LIST_DIR}/src/*.h)
FILE(GLOB_RECURSE tmp_c ${CMAKE_CURRENT_LIST_DIR}/src/*.c)
if (MSVC OR MINGW)
	add_library(c2c_benchmark STATIC ${tmp_h} ${tmp_c})
else()
	add_library(c2c_benchmark STATIC ${tmp_c})
endif()
target_link_libraries(c2c_benchmark PUBLIC mugglec)
target_include_directories(c2c_benchmark PUBLIC ${CMAKE_CURRENT_LIST_DIR}/src)

set(app_dir ${CMAKE_CURRENT_LIST_DIR}/app)
FILE(GLOB subdirs RELATIVE ${app_dir} ${app_dir}/*)
FOREACH(subdir ${subdirs})
	IF(IS_DIRECTORY ${app_dir}/${subdir})
		file(GLOB_RECURSE tmp_h ${app_dir}/${subdir}/*.h)
		file(GLOB_RECURSE tmp_c ${app_dir}/${subdir}/*.c)
		if (MSVC OR MINGW)
			add_executable(${subdir} ${tmp_h} ${tmp_c})
		else()
			add_executable(${subdir} ${tmp_c})
		endif()
		target_link_libraries(${subdir} PUBLIC c2c_benchmark)
	ENDIF()
ENDFOREACH()
