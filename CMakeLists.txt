# Copyright 2019-2021 Lawrence Livermore National Security, LLC and other YGM
# Project Developers. See the top-level COPYRIGHT file for details.
#
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.14)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake") 
project(ygm-bench         
	VERSION 0.9
	DESCRIPTION "YGM Benchmarks"         
	LANGUAGES CXX) 

set(CMAKE_CXX_STANDARD 17) 

include(FetchContent)

#
#  MPI
find_package(MPI)

#
#  ygm
find_package(ygm CONFIG)
if(NOT ygm_FOUND)
	if(DEFINED ENV{YGM_BENCH_YGM_REPO})
		set(YGM_REPO $ENV{YGM_BENCH_YGM_REPO})
	else()
		set(YGM_REPO "https://github.com/llnl/ygm.git")
	endif()
	if(DEFINED ENV{YGM_BENCH_YGM_TAG})
		set(YGM_TAG $ENV{YGM_BENCH_YGM_TAG})
	else()
		set(YGM_TAG "v0.9")
	endif()

	FetchContent_Declare(
	ygm
	GIT_REPOSITORY 	${YGM_REPO}
	GIT_TAG 		${YGM_TAG}
	)         
	set(JUST_INSTALL_YGM ON)
	FetchContent_MakeAvailable(ygm)
	message(STATUS "Cloned ygm dependency " ${ygm_SOURCE_DIR})
else()
	message(STATUS "Found ygm dependency " ${ygm_DIR})
endif()

#
# krokee
find_package(krowkee CONFIG)
if(NOT krowkee_FOUND)
  if(DEFINED ENV{YGM_BENCH_KROWKEE_REPO})
	  set(KROWKEE_REPO $ENV{YGM_BENCH_KROWKEE_REPO})
	else()
		set(KROWKEE_REPO "https://github.com/LLNL/krowkee.git")
	endif()
	if(DEFINED ENV{YGM_BENCH_KROWKEE_TAG})
		set(KROWKEE_TAG $ENV{YGM_BENCH_KROWKEE_TAG})
	else()
		set(KROWKEE_TAG "v0.1.0")
	endif()
	FetchContent_Declare(
		krowkee
		GIT_REPOSITORY 	${KROWKEE_REPO}
		GIT_TAG 				${KROWKEE_TAG}
	)
	set(JUST_INSTALL_KROWKEE ON)
	FetchContent_MakeAvailable(krowkee)
	message(STATUS "Cloned krowkee dependency " ${krowkee_SOURCE_DIR})
else()
	message(STATUS "Found krowkee dependency " ${krowkee_DIR})
endif()

function(setup_ygm_target exe_name)
	add_executable(${exe_name} ${exe_name}.cpp)
	target_link_libraries(${exe_name} PRIVATE ygm::ygm)
	target_include_directories(${exe_name} PRIVATE "${PROJECT_SOURCE_DIR}/include")
	target_include_directories(${exe_name} PRIVATE ${PROJECT_SOURCE_DIR}/include ${BOOST_INCLUDE_DIRS})
endfunction()

function(setup_krowkee_target exe_name)
  setup_ygm_target(${exe_name})
	target_link_libraries(${exe_name} PRIVATE krowkee::krowkee)
endfunction()

function(setup_mpi_target exe_name)
	add_executable(${exe_name} ${exe_name}.cpp)
	target_link_libraries(${exe_name} PRIVATE MPI::MPI_CXX)
	target_include_directories(${exe_name} PRIVATE "${PROJECT_SOURCE_DIR}/include")
	target_include_directories(${exe_name} PRIVATE ${PROJECT_SOURCE_DIR}/include ${BOOST_INCLUDE_DIRS})
	target_link_libraries(${exe_name} PRIVATE Boost::json)
endfunction()

if (NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
	message(STATUS "CMAKE_BUILD_TYPE is set as Release")
endif ()

add_subdirectory(src)
