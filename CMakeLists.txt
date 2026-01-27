cmake_minimum_required(VERSION 3.9) 

project(PBA-STA LANGUAGES CXX) 
include(CTest) 

# Turn on the verbose
set(CMAKE_VERBOSE_MAKEFILE ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -O2")

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
  ${FLEX_verilog_lexer_OUTPUTS}
  ${BISON_verilog_parser_OUTPUTS}
)

