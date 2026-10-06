# FourfoldFX logic island - unit tests (engine-free; run in the Linux container or on the Mac).
#   cmake -S unreal/Source/FourfoldFX/Private/Logic/tests -B <build> -G Ninja && cmake --build <build> && <build>/ffx_logic_tests [filter]
# UnrealBuildTool also compiles the .cpp files of this folder as part of the FourfoldFX module; they are empty there
# because FF_LOGIC_TESTS is only defined here.
cmake_minimum_required(VERSION 3.20)
project(ffx_logic_tests CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

get_filename_component(LOGIC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
get_filename_component(SOURCE_DIR "${LOGIC_DIR}/../../.." ABSOLUTE)       # unreal/Source
get_filename_component(UNREAL_DIR "${SOURCE_DIR}/.." ABSOLUTE)            # unreal/

file(GLOB LOGIC_SOURCES "${LOGIC_DIR}/*.cpp")
set(CORE_JSON "${SOURCE_DIR}/FourfoldCore/Private/Util/Json.cpp")

set(FFX_WARNINGS -Wall -Wextra -Wshadow -Wconversion -Wundef -Werror)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  list(APPEND FFX_WARNINGS -Wshorten-64-to-32 -Wshadow-all)
else()
  # g++ 13 false positives on std::variant / shared_ptr at -O2 (GCC PR 107138, 107532), as in CoreTests.
  list(APPEND FFX_WARNINGS -Wno-maybe-uninitialized)
endif()

# Every source is compiled against the Unreal macro poison (check, PI, TEXT, INDEX_NONE ... must not be names).
set(FFX_OPTIONS -fno-exceptions -fno-rtti ${FFX_WARNINGS} -include "${UNREAL_DIR}/CoreTests/ue_macro_poison.h")
add_library(ffx_logic STATIC ${LOGIC_SOURCES} ${CORE_JSON})
target_include_directories(ffx_logic PUBLIC "${LOGIC_DIR}" "${SOURCE_DIR}/FourfoldCore/Public")
target_compile_options(ffx_logic PRIVATE ${FFX_OPTIONS})

# Unreal compiles the module in unity batches: every Logic source in ONE translation unit must still compile (no
# colliding helper names in anonymous namespaces).
add_library(ffx_unity_check OBJECT ${LOGIC_SOURCES})
target_include_directories(ffx_unity_check PRIVATE "${LOGIC_DIR}" "${SOURCE_DIR}/FourfoldCore/Public")
target_compile_options(ffx_unity_check PRIVATE ${FFX_OPTIONS})
set_target_properties(ffx_unity_check PROPERTIES UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE 0)

file(GLOB TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
list(FILTER TEST_SOURCES EXCLUDE REGEX "(fx_preview|core_soak)\\.cpp$")
add_executable(ffx_logic_tests ${TEST_SOURCES})
target_link_libraries(ffx_logic_tests PRIVATE ffx_logic)
target_compile_definitions(ffx_logic_tests PRIVATE FF_LOGIC_TESTS=1 FFX_UNREAL_DIR="${UNREAL_DIR}")
target_compile_options(ffx_logic_tests PRIVATE ${FFX_OPTIONS})

# Integration soak: the real engine-free sim (FourfoldCore sources) plays every move of every sub-element (tap and
# T3) through the Lab input path plus an AI duel, and every frame goes through the FX director (no unmapped body, no
# invalid mesh, budgets). Off with -DFFX_WITH_CORE=OFF.
option(FFX_WITH_CORE "Build the FourfoldCore integration soak" ON)
if(FFX_WITH_CORE AND EXISTS "${SOURCE_DIR}/FourfoldCore/Private")
  file(GLOB_RECURSE CORE_SOURCES "${SOURCE_DIR}/FourfoldCore/Private/*.cpp")
  list(FILTER CORE_SOURCES EXCLUDE REGEX ".*/Private/UE/.*")
  list(REMOVE_ITEM CORE_SOURCES "${CORE_JSON}")
  add_library(ffx_core_sim STATIC ${CORE_SOURCES})
  target_include_directories(ffx_core_sim PUBLIC "${SOURCE_DIR}/FourfoldCore/Public" PRIVATE "${SOURCE_DIR}/FourfoldCore/Private")
  target_compile_definitions(ffx_core_sim PUBLIC FOURFOLDCORE_API=)
  target_compile_options(ffx_core_sim PRIVATE -fno-exceptions -fno-rtti -w)
  target_link_libraries(ffx_core_sim PUBLIC ffx_logic)   # ff::ParseJson lives in ffx_logic (Json.cpp)
  add_executable(ffx_core_soak core_soak.cpp)
  target_link_libraries(ffx_core_soak PRIVATE ffx_logic ffx_core_sim)
  target_compile_definitions(ffx_core_soak PRIVATE FF_LOGIC_TESTS=1 FFX_UNREAL_DIR="${UNREAL_DIR}")
  target_compile_options(ffx_core_soak PRIVATE -fno-exceptions -fno-rtti -Wall -Wextra -Werror)

  # Offline preview renderer: the Lab plays moves, the director draws, a software rasteriser shades every item with
  # the material code of Content/Python/fourfold/fx/spec.py (generated C++) over the shared HLSL (hlsl_shim.h).
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    set(FFX_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/gen")
    file(GLOB FFX_USH "${UNREAL_DIR}/Shaders/Common/*.ush" "${UNREAL_DIR}/Shaders/FX/*.ush")
    set(FFX_GEN_SCRIPT "${UNREAL_DIR}/Tools/vfx/shader_check/gen_preview_nodes.py")
    add_custom_command(OUTPUT "${FFX_GEN_DIR}/fx_nodes.gen.h"
      COMMAND ${Python3_EXECUTABLE} "${FFX_GEN_SCRIPT}" "${FFX_GEN_DIR}/fx_nodes.gen.h"
      DEPENDS "${FFX_GEN_SCRIPT}" "${UNREAL_DIR}/Content/Python/fourfold/fx/spec.py" ${FFX_USH}
      COMMENT "Generating fx_nodes.gen.h from the FX material spec")
    add_executable(ffx_preview fx_preview.cpp "${FFX_GEN_DIR}/fx_nodes.gen.h")
    target_include_directories(ffx_preview PRIVATE "${FFX_GEN_DIR}")
    target_link_libraries(ffx_preview PRIVATE ffx_logic ffx_core_sim)
    target_compile_definitions(ffx_preview PRIVATE FF_LOGIC_TESTS=1 FFX_UNREAL_DIR="${UNREAL_DIR}")
    # the shared HLSL mixes float / double literals like HLSL does: no -Wconversion here
    target_compile_options(ffx_preview PRIVATE -fno-exceptions -fno-rtti -O2 -Wall -Wno-unused-parameter
      -Wno-unused-variable -Wno-unused-function -Wno-unused-but-set-variable)
  endif()
endif()
