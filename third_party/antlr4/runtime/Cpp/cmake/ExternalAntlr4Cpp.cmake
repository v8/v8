# -*- mode:cmake -*-
#
# This Cmake file is for those using a superbuild Cmake Pattern, it
# will download the tools and build locally
#
# use 2the antlr4cpp_process_grammar to support multiple grammars in the
# same project
#
# - Getting quicky started with Antlr4cpp
#
# Here is how you can use this external project to create the antlr4cpp
# demo to start your project off.
#
# create your project source folder somewhere. e.g. ~/srcfolder/
# + make a subfolder cmake
# + Copy this file to srcfolder/cmake
# + cut below and use it to create srcfolder/CMakeLists.txt,
# + from https://github.com/DanMcLaughlin/antlr4/tree/master/runtime/Cpp/demo Copy main.cpp, TLexer.g4 and TParser.g4 to ./srcfolder/
#
# next make a build folder e.g. ~/buildfolder/
# from the buildfolder, run cmake ~/srcfolder; make
#
###############################################################
# # minimum required CMAKE version
# CMAKE_MINIMUM_REQUIRED(VERSION 2.8.12.2 FATAL_ERROR)
#
# LIST( APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake )
#
# # compiler must be 11 or 14
# SET (CMAKE_CXX_STANDARD 11)
#
# # set variable pointing to the antlr tool that supports C++
# set(ANTLR4CPP_JAR_LOCATION /home/user/antlr4-4.5.4-SNAPSHOT.jar)
# # add external build for antlrcpp
# include( ExternalAntlr4Cpp )
# # add antrl4cpp artifacts to project environment
# include_directories( ${ANTLR4CPP_INCLUDE_DIRS} )
# link_directories( ${ANTLR4CPP_LIBS} )
# message(STATUS "Found antlr4cpp libs: ${ANTLR4CPP_LIBS} and includes: ${ANTLR4CPP_INCLUDE_DIRS} ")
#
# # Call macro to add lexer and grammar to your build dependencies.
# antlr4cpp_process_grammar(demo antlrcpptest
#   ${CMAKE_CURRENT_SOURCE_DIR}/TLexer.g4
#   ${CMAKE_CURRENT_SOURCE_DIR}/TParser.g4)
# # include generated files in project environment
# include_directories(${antlr4cpp_include_dirs_antlrcpptest})
#
# # add generated grammar to demo binary target
# add_executable(demo main.cpp ${antlr4cpp_src_files_antlrcpptest})
# add_dependencies(demo antlr4cpp antlr4cpp_generation_antlrcpptest)
# target_link_libraries(demo antlr4-runtime)
#
###############################################################

CMAKE_MINIMUM_REQUIRED(VERSION 2.8.12.2)
PROJECT(antlr4cpp_fetcher CXX)
INCLUDE(ExternalProject)
FIND_PACKAGE(Git REQUIRED)

# only JRE required
FIND_PACKAGE(Java COMPONENTS Runtime REQUIRED)

############ Download and Generate runtime #################
set(ANTLR4CPP_EXTERNAL_ROOT ${CMAKE_BINARY_DIR}/externals/antlr4cpp)

# external repository
# GIT_REPOSITORY     https://github.com/antlr/antlr4.git
set(ANTLR4CPP_EXTERNAL_REPO "https://github.com/antlr/antlr4.git")
set(ANTLR4CPP_EXTERNAL_TAG  "4.7.1")

if(NOT EXISTS "${ANTLR4CPP_JAR_LOCATION}")
  message(FATAL_ERROR "Unable to find antlr tool. ANTLR4CPP_JAR_LOCATION:${ANTLR4CPP_JAR_LOCATION}")
endif()

# default path for source files
if (NOT ANTLR4CPP_GENERATED_SRC_DIR)
  set(ANTLR4CPP_GENERATED_SRC_DIR ${CMAKE_BINARY_DIR}/antlr4cpp_generated_src)
endif()

# !TODO! This should probably check with Cmake Find first?
# set(ANTLR4CPP_JAR_LOCATION ${ANTLR4CPP_EXTERNAL_ROOT}/${ANTLR4CPP_JAR_NAME})
#
# !TODO! Ensure Antlr tool available - copy from internet
#
# # !TODO! this shold be calculated based on the tags
# if (NOT ANTLR4CPP_JAR_NAME)
#   # default location to find antlr Java binary
#   set(ANTLR4CPP_JAR_NAME antlr4-4.5.4-SNAPSHOT.jar)
# endif()
#
# if(NOT EXISTS "${ANTLR4CPP_JAR_LOCATION}")
#   # download Java tool if not installed
#   ExternalProject_ADD(
#     #--External-project-name------
#     antlrtool
#     #--Core-directories-----------
#     PREFIX             ${ANTLR4CPP_EXTERNAL_ROOT}
#     #--Download step--------------
#     DOWNLOAD_DIR       ${ANTLR4CPP_EXTERNAL_ROOT}
#     DOWNLOAD_COMMAND   ""
#     # URL              http://www.antlr.org/download/${ANTLR4CPP_JAR_NAME}
#     # antlr4-4.5.4-SNAPSHOT.jar
#     # GIT_TAG            v4.5.4
#     TIMEOUT            10
#     LOG_DOWNLOAD       ON
#     #--Update step----------
#     # UPDATE_COMMAND     ${GIT_EXECUTABLE} pull
#     #--Patch step----------
#     # PATCH_COMMAND sh -c "cp <SOURCE_DIR>/scripts/CMakeLists.txt <SOURCE_DIR>"
#     #--Configure step-------------
#     CMAKE_ARGS         ""
#     CONFIGURE_COMMAND  ""
#     LOG_CONFIGURE ON
#     #--Build step-----------------
#     BUILD_COMMAND      ""
#     LOG_BUILD ON
#     #--Install step---------------
#     INSTALL_COMMAND    ""
#     )
#   # Verify Antlr Available
#   if(NOT EXISTS "${ANTLR4CPP_JAR_LOCATION}")
#     message(FATAL_ERROR "Unable to find ANTLR4CPP_JAR_LOCATION:${ANTLR4CPP_JAR_LOCATION} -> ${ANTLR4CPP_JAR_NAME} not in ${ANTLR4CPP_DIR} ")
#   endif()
# endif()

# download runtime environment
ExternalProject_ADD(
  #--External-project-name------
  antlr4cpp
  #--Depend-on-antrl-tool-----------
  # DEPENDS antlrtool
  #--Core-directories-----------
  PREFIX             ${ANTLR4CPP_EXTERNAL_ROOT}
  #--Download step--------------
  GIT_REPOSITORY     ${ANTLR4CPP_EXTERNAL_REPO}
  # GIT_TAG          ${ANTLR4CPP_EXTERNAL_TAG}
  TIMEOUT            10
  LOG_DOWNLOAD       ON
  #--Update step----------
  UPDATE_COMMAND     ${GIT_EXECUTABLE} pull
  #--Patch step----------
  # PATCH_COMMAND sh -c "cp <SOURCE_DIR>/scripts/CMakeLists.txt <SOURCE_DIR>"
  #--Configure step-------------
  CONFIGURE_COMMAND  ${CMAKE_COMMAND} -DCMAKE_BUILD_TYPE=Release -DANTLR4CPP_JAR_LOCATION=${ANTLR4CPP_JAR_LOCATION} -DBUILD_SHARED_LIBS=ON -BUILD_TESTS=OFF -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR> -DCMAKE_SOURCE_DIR:PATH=<SOURCE_DIR>/runtime/Cpp <SOURCE_DIR>/runtime/Cpp
  LOG_CONFIGURE ON
  #--Build step-----------------
  # BUILD_COMMAND ${CMAKE_MAKE_PROGRAM}
  LOG_BUILD ON
  #--Install step---------------
  # INSTALL_COMMAND    ""
  # INSTALL_DIR ${CMAKE_BINARY_DIR}/
  #--Install step---------------
  # INSTALL_COMMAND    ""
)

ExternalProject_Get_Property(antlr4cpp INSTALL_DIR)

list(APPEND ANTLR4CPP_INCLUDE_DIRS ${INSTALL_DIR}/include/antlr4-runtime)
foreach(src_path misc atn dfa tree support)
  list(APPEND ANTLR4CPP_INCLUDE_DIRS ${INSTALL_DIR}/include/antlr4-runtime/${src_path})
endforeach(src_path)

set(ANTLR4CPP_LIBS "${INSTALL_DIR}/lib")

# antlr4_shared ${INSTALL_DIR}/lib/libantlr4-runtime.so
# antlr4_static ${INSTALL_DIR}/lib/libantlr4-runtime.a

############ Generate runtime #################
# macro to add dependencies to target
#
# Param 1 project name
# Param 1 namespace (postfix for dependencies)
# Param 2 Lexer file (full path)
# Param 3 Parser File (full path)
#
# output
#
# antlr4cpp_src_files_{namespace} - src files for add_executable
# antlr4cpp_include_dirs_{namespace} - include dir for generated headers
# antlr4cpp_generation_{namespace} - for add_dependencies tracking

macro(antlr4cpp_process_grammar
    antlr4cpp_project
    antlr4cpp_project_namespace
    antlr4cpp_grammar_lexer
    antlr4cpp_grammar_parser)

  if(EXISTS "${ANTLR4CPP_JAR_LOCATION}")
    message(STATUS "Found antlr tool: ${ANTLR4CPP_JAR_LOCATION}")
  else()
    message(FATAL_ERROR "Unable to find antlr tool. ANTLR4CPP_JAR_LOCATION:${ANTLR4CPP_JAR_LOCATION}")
  endif()

  add_custom_target("antlr4cpp_generation_${antlr4cpp_project_namespace}"
    COMMAND
    ${CMAKE_COMMAND} -E make_directory ${ANTLR4CPP_GENERATED_SRC_DIR}
    COMMAND
    "${Java_JAVA_EXECUTABLE}" -jar "${ANTLR4CPP_JAR_LOCATION}" -Werror -Dlanguage=Cpp -listener -visitor -o "${ANTLR4CPP_GENERATED_SRC_DIR}/${antlr4cpp_project_namespace}" -package ${antlr4cpp_project_namespace} "${antlr4cpp_grammar_lexer}" "${antlr4cpp_grammar_parser}"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    DEPENDS "${antlr4cpp_grammar_lexer}" "${antlr4cpp_grammar_parser}"
    )

  # Find all the input files
  FILE(GLOB generated_files ${ANTLR4CPP_GENERATED_SRC_DIR}/${antlr4cpp_project_namespace}/*.cpp)

  # export generated cpp files into list
  foreach(generated_file ${generated_files})
    list(APPEND antlr4cpp_src_files_${antlr4cpp_project_namespace} ${generated_file})
    if (NOT CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    set_source_files_properties(
      ${generated_file}
      PROPERTIES
      COMPILE_FLAGS -Wno-overloaded-virtual
      )
    endif ()
  endforeach(generated_file)
  message(STATUS "Antlr4Cpp  ${antlr4cpp_project_namespace} Generated: ${generated_files}")

  # export generated include directory
  set(antlr4cpp_include_dirs_${antlr4cpp_project_namespace} ${ANTLR4CPP_GENERATED_SRC_DIR}/${antlr4cpp_project_namespace})
  message(STATUS "Antlr4Cpp ${antlr4cpp_project_namespace} include: ${ANTLR4CPP_GENERATED_SRC_DIR}/${antlr4cpp_project_namespace}")

endmacro()
