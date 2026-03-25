cmake_minimum_required(VERSION 3.9) 

project(PBA-STA LANGUAGES CXX) 
include(CTest) 

# Turn on the verbose
set(CMAKE_VERBOSE_MAKEFILE ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置默认构建类型为 Debug（如果没有指定）
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

# 根据构建类型设置编译选项
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    # -g: 生成调试符号
    # -O0: 关闭优化，便于调试
    # -fno-omit-frame-pointer: 保留帧指针，有助于反向调试
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -g -O0 -fno-omit-frame-pointer")
    message(STATUS "Build type: Debug (with debug symbols, no optimization, frame pointer enabled for reverse debugging)")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -O2")
    message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
endif()

# add the binary tree to the search path for include files
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${PROJECT_SOURCE_DIR}/parser-verilog)  # For Flex/Bison generated files

# -----------------------------------------------------------------------------
# Rust Liberty Parser (from iEDA)
# -----------------------------------------------------------------------------
set(LIBERTY_RUST_PROJECT liberty_parser)
set(LIBERTY_RUST_DIR ${PROJECT_SOURCE_DIR}/third_party/liberty-parser/lib-rust/liberty-parser)
set(LIBERTY_RUST_LIB_PATH ${LIBERTY_RUST_DIR}/target/release/libliberty_parser.a)
set(LIBERTY_RUST_CARGO_FLAGS "--release")


include(ExternalProject)
ExternalProject_Add(${LIBERTY_RUST_PROJECT}
  PREFIX ${LIBERTY_RUST_DIR}
  SOURCE_DIR ${LIBERTY_RUST_DIR}
  BINARY_DIR ${LIBERTY_RUST_DIR}
  CONFIGURE_COMMAND ""
  BUILD_COMMAND cargo build ${LIBERTY_RUST_CARGO_FLAGS}
  INSTALL_COMMAND ""
  BUILD_BYPRODUCTS ${LIBERTY_RUST_LIB_PATH}
)

add_library(liberty_rust STATIC IMPORTED GLOBAL)
set_target_properties(liberty_rust PROPERTIES
  IMPORTED_LOCATION ${LIBERTY_RUST_LIB_PATH}
)
add_dependencies(liberty_rust ${LIBERTY_RUST_PROJECT})

target_include_directories(liberty_rust INTERFACE
  ${PROJECT_SOURCE_DIR}/third_party/liberty-parser
)

message(STATUS "Liberty Rust parser: ${LIBERTY_RUST_LIB_PATH}")

# -----------------------------------------------------------------------------
# nlohmann/json (for LibLibrary::printLibertyLibraryJson)
# -----------------------------------------------------------------------------
find_package(nlohmann_json 3.2.0 QUIET)
if(NOT nlohmann_json_FOUND)
  include(FetchContent)
  FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.2
  )
  set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(json)
endif()

# -----------------------------------------------------------------------------
# Lib parser C++ (ported from third_party/liberty-parser, no iEDA deps)
# -----------------------------------------------------------------------------
add_library(lib_parser_cpp STATIC
  ${PROJECT_SOURCE_DIR}/src/lib_parser/Lib.cc
  ${PROJECT_SOURCE_DIR}/src/lib_parser/LibParserRustC.cc
  ${PROJECT_SOURCE_DIR}/src/lib_parser/LibClassifyCell.cc
  ${PROJECT_SOURCE_DIR}/src/lib_parser/lib_to_celllib_converter.cpp
)
target_include_directories(lib_parser_cpp PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(lib_parser_cpp PUBLIC liberty_rust nlohmann_json::nlohmann_json)
add_dependencies(lib_parser_cpp liberty_rust)

# -----------------------------------------------------------------------------
# Flex & Bison
# -----------------------------------------------------------------------------

find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)

BISON_TARGET(verilog_parser
             ${PROJECT_SOURCE_DIR}/parser-verilog/verilog_parser.yy
             ${CMAKE_CURRENT_BINARY_DIR}/verilog_parser.tab.cc
             DEFINES_FILE ${CMAKE_CURRENT_BINARY_DIR}/verilog_parser.tab.hh
             COMPILE_FLAGS "-d")
FLEX_TARGET(verilog_lexer
            ${PROJECT_SOURCE_DIR}/parser-verilog/verilog_lexer.l
            ${CMAKE_CURRENT_BINARY_DIR}/verilog_lexer.yy.cc)
ADD_FLEX_BISON_DEPENDENCY(verilog_lexer verilog_parser)


# -----------------------------------------------------------------------------
# PbaSta program 
# -----------------------------------------------------------------------------

# Set the output folder to example
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR})

# Main executable
add_executable(pba_sta
  ${PROJECT_SOURCE_DIR}/src/main.cpp
  ${PROJECT_SOURCE_DIR}/src/sta_logger.cpp
  ${PROJECT_SOURCE_DIR}/src/timing/timing_graph_builder.cpp
  ${PROJECT_SOURCE_DIR}/src/timing/timing_point_classifier.cpp
  ${PROJECT_SOURCE_DIR}/src/sta.cpp
  ${PROJECT_SOURCE_DIR}/src/verilog_collect.cpp
  ${PROJECT_SOURCE_DIR}/src/lookuptable.cpp
  ${PROJECT_SOURCE_DIR}/src/trace_debug.cpp
  ${PROJECT_SOURCE_DIR}/src/sta_report.cpp
  ${PROJECT_SOURCE_DIR}/src/sdc_parser.cpp
  ${PROJECT_SOURCE_DIR}/src/cell_data_structure.cpp
  ${PROJECT_SOURCE_DIR}/src/celllib_cache.cpp
  ${PROJECT_SOURCE_DIR}/src/candidate/candidate.cpp
  ${PROJECT_SOURCE_DIR}/src/candidate/candidate_recalculate.cpp
  ${PROJECT_SOURCE_DIR}/src/candidate/candidate_graphy.cpp
  ${PROJECT_SOURCE_DIR}/src/candidate/candidate_utils.cpp
  ${PROJECT_SOURCE_DIR}/src/gba_dfs/gba_dfs.cpp
  ${PROJECT_SOURCE_DIR}/src/utils.cpp
  ${PROJECT_SOURCE_DIR}/src/debug.cpp
  ${FLEX_verilog_lexer_OUTPUTS}
  ${BISON_verilog_parser_OUTPUTS}
)
target_link_libraries(pba_sta PRIVATE lib_parser_cpp nlohmann_json::nlohmann_json pthread dl)

# -----------------------------------------------------------------------------
# Custom targets
# -----------------------------------------------------------------------------

# Target: run - Run the executable
add_custom_target(run
  COMMAND ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/pba_sta
  DEPENDS pba_sta
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  COMMENT "Running pba_sta executable"
)

# Target: tag - Generate tags file using ctags
add_custom_target(tag
  COMMAND ctags -R --c++-kinds=+p --fields=+iaS --extras=+q . || universal-ctags -R --c++-kinds=+p --fields=+iaS --extras=+q . || echo "Warning: ctags or universal-ctags not found. Please install one of them."
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  COMMENT "Generating tags file"
)

# Target: compile-flags - Generate compile_commands.json using bear
# Note: This will rebuild the project with bear to capture compile commands
add_custom_target(compile-flags
  COMMAND bear -- ${CMAKE_COMMAND} --build ${CMAKE_CURRENT_BINARY_DIR} --target pba_sta
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  COMMENT "Generating compile_commands.json using bear (rebuilding project)"
)

# Target: all - Build executable, generate tags, and use bear
add_custom_target(tar
  COMMAND ${CMAKE_COMMAND} -E echo "=== Building executable ==="
  COMMAND ${CMAKE_COMMAND} --build ${CMAKE_CURRENT_BINARY_DIR} --target pba_sta
  COMMAND ${CMAKE_COMMAND} -E echo "=== Generating tags file ==="
  COMMAND sh -c "ctags -R --c++-kinds=+p --fields=+iaS --extras=+q . 2>/dev/null || universal-ctags -R --c++-kinds=+p --fields=+iaS --extras=+q . 2>/dev/null || echo 'Warning: ctags/universal-ctags not found. Install with: sudo apt install universal-ctags'"
  COMMAND ${CMAKE_COMMAND} -E echo "=== Generating compile_commands.json with bear ==="
  COMMAND sh -c "bear -- ${CMAKE_COMMAND} --build ${CMAKE_CURRENT_BINARY_DIR} --target pba_sta 2>/dev/null || echo 'Warning: bear not found. Install with: sudo apt install bear'"
  DEPENDS pba_sta
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  COMMENT "Building executable, generating tags, and compile_commands.json"
)