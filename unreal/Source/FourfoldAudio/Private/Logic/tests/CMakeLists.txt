# FourfoldAudio logic island - unit tests (engine-free; run in the Linux container or on the Mac).
#   cmake -S unreal/Source/FourfoldAudio/Private/Logic/tests -B <build> -G Ninja && cmake --build <build> && <build>/ffa_logic_tests
# UnrealBuildTool also compiles logic_tests.cpp as part of the FourfoldAudio module; the file is empty there because
# FF_LOGIC_TESTS is only defined here.
cmake_minimum_required(VERSION 3.20)
project(ffa_logic_tests CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(LOGIC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
get_filename_component(SOURCE_DIR "${LOGIC_DIR}/../../.." ABSOLUTE)       # unreal/Source
get_filename_component(UNREAL_DIR "${SOURCE_DIR}/.." ABSOLUTE)            # unreal/

file(GLOB LOGIC_SOURCES "${LOGIC_DIR}/*.cpp")
set(CORE_JSON "${SOURCE_DIR}/FourfoldCore/Private/Util/Json.cpp")

set(FFA_WARNINGS -Wall -Wextra -Wshadow -Wconversion -Wundef -Werror)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  list(APPEND FFA_WARNINGS -Wshorten-64-to-32)
endif()

# The island once as a library, every source compiled against the Unreal macro poison (check, PI, TEXT, INDEX_NONE ...
# must not be names), no exceptions, no RTTI (Unreal builds without both).
set(FFA_OPTIONS -fno-exceptions -fno-rtti ${FFA_WARNINGS} -include "${UNREAL_DIR}/CoreTests/ue_macro_poison.h")
add_library(ffa_logic STATIC ${LOGIC_SOURCES} ${CORE_JSON})
target_include_directories(ffa_logic PUBLIC "${LOGIC_DIR}" "${SOURCE_DIR}/FourfoldCore/Public")
target_compile_options(ffa_logic PRIVATE ${FFA_OPTIONS})

add_executable(ffa_logic_tests logic_tests.cpp)
target_link_libraries(ffa_logic_tests PRIVATE ffa_logic)
target_compile_definitions(ffa_logic_tests PRIVATE FF_LOGIC_TESTS=1 FFA_UNREAL_DIR="${UNREAL_DIR}")
target_compile_options(ffa_logic_tests PRIVATE ${FFA_OPTIONS})
