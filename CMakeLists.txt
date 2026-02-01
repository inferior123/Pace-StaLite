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
  ${PROJECT_SOURCE_DIR}/src/sta.cpp
  ${PROJECT_SOURCE_DIR}/src/trace_debug.cpp
  ${PROJECT_SOURCE_DIR}/src/sta_report.cpp
  ${PROJECT_SOURCE_DIR}/src/sdc_parser.cpp
  ${PROJECT_SOURCE_DIR}/src/cell_data_structure.cpp
  ${PROJECT_SOURCE_DIR}/src/liberty_parser.cpp
  ${FLEX_verilog_lexer_OUTPUTS}
  ${BISON_verilog_parser_OUTPUTS}
)

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

