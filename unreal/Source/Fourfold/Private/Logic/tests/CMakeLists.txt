# Fourfold game logic island - unit tests (engine-free; run in the Linux container or on the Mac).
#   cmake -S unreal/Source/Fourfold/Private/Logic/tests -B <build> -G Ninja && cmake --build <build> && <build>/ffg_logic_tests
# UnrealBuildTool also compiles logic_tests.cpp as part of the Fourfold module; the file is empty there because
# FF_LOGIC_TESTS is only defined here.
cmake_minimum_required(VERSION 3.20)
project(ffg_logic_tests CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(LOGIC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
get_filename_component(SOURCE_DIR "${LOGIC_DIR}/../../.." ABSOLUTE)       # unreal/Source
get_filename_component(UNREAL_DIR "${SOURCE_DIR}/.." ABSOLUTE)            # unreal/

file(GLOB LOGIC_SOURCES "${LOGIC_DIR}/*.cpp")
set(CORE_JSON "${SOURCE_DIR}/FourfoldCore/Private/Util/Json.cpp")

set(FFG_WARNINGS -Wall -Wextra -Wshadow -Wconversion -Wundef -Werror)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  list(APPEND FFG_WARNINGS -Wshorten-64-to-32)
endif()

# The island once as a library, every source compiled once more against the Unreal macro poison (check, PI, TEXT,
# INDEX_NONE ... must not be names).
set(FFG_OPTIONS -fno-exceptions -fno-rtti ${FFG_WARNINGS} -include "${UNREAL_DIR}/CoreTests/ue_macro_poison.h")
add_library(ffg_logic STATIC ${LOGIC_SOURCES} ${CORE_JSON})
target_include_directories(ffg_logic PUBLIC "${LOGIC_DIR}" "${SOURCE_DIR}/FourfoldCore/Public")
target_compile_options(ffg_logic PRIVATE ${FFG_OPTIONS})

add_executable(ffg_logic_tests logic_tests.cpp)
target_link_libraries(ffg_logic_tests PRIVATE ffg_logic)
target_compile_definitions(ffg_logic_tests PRIVATE FF_LOGIC_TESTS=1 FFG_UNREAL_DIR="${UNREAL_DIR}")
target_compile_options(ffg_logic_tests PRIVATE ${FFG_OPTIONS})

# Touch layout dump for visual review: ffg_layout_dump > layouts.json && python3 draw_layouts.py layouts.json <out_dir>
add_executable(ffg_layout_dump layout_dump.cpp)
target_link_libraries(ffg_layout_dump PRIVATE ffg_logic)
target_compile_definitions(ffg_layout_dump PRIVATE FF_LOGIC_TESTS=1)
target_compile_options(ffg_layout_dump PRIVATE ${FFG_OPTIONS})
