# FourfoldCore - engine-free build + tests (stream `core`). See unreal/docs/core/README.md.
#   cmake -S unreal/CoreTests -B <build> -G Ninja && cmake --build <build> && <build>/ff_tests [filter]
# Targets:
#   ff_core              static library: Source/FourfoldCore/Private/** (except Private/UE)
#   ff_tests             test runner (ported Godot sim suites, golden matrix, facade tests)
#   ff_core_shared       shared library, -fvisibility=hidden, FOURFOLDCORE_API = visibility("default")
#   ff_facade_link_test  uses ONLY Public/ff and links ff_core_shared (proves every facade symbol is exported)
#   ff_poison            every library source compiled with -include ue_macro_poison.h (compile only)
#   ff_unity             every library source in CMake unity batches (compile only)
#   ff_perf              soak / perf / determinism runner
cmake_minimum_required(VERSION 3.20)
project(FourfoldCore CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

get_filename_component(FF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(FF_MODULE "${FF_ROOT}/Source/FourfoldCore")
set(FF_DATA_DIR "${FF_MODULE}/Data")

file(GLOB_RECURSE FF_LIB_SOURCES CONFIGURE_DEPENDS "${FF_MODULE}/Private/*.cpp")
list(FILTER FF_LIB_SOURCES EXCLUDE REGEX ".*/Private/UE/.*")
file(GLOB_RECURSE FF_PUBLIC_HEADERS CONFIGURE_DEPENDS "${FF_MODULE}/Public/ff/*.h")

# Warnings that the Unreal toolchains (clang on Mac / iOS) turn into errors, plus the strict set of the contract.
set(FF_WARN -Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion -Wundef -Werror)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  list(APPEND FF_WARN -Wshorten-64-to-32)
else()
  # g++ 13 false positives on std::variant / shared_ptr at -O2 (GCC PR 107138, 107532); Unreal never uses g++.
  list(APPEND FF_WARN -Wno-maybe-uninitialized -Wno-dangling-reference)
endif()
set(FF_LANG -fno-exceptions -fno-rtti)
# Test code is never compiled by Unreal: it keeps the language rules but not the conversion warnings.
set(FF_TEST_WARN -Wall -Wextra -Wshadow -Wno-sign-conversion -Wundef -Werror -Wno-unused-parameter -Wno-unused-variable)
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  list(APPEND FF_TEST_WARN -Wno-maybe-uninitialized -Wno-dangling-reference)
endif()

function(ff_lib_options tgt)
  target_include_directories(${tgt} PUBLIC "${FF_MODULE}/Public" PRIVATE "${FF_MODULE}/Private")
  target_compile_options(${tgt} PRIVATE ${FF_WARN} ${FF_LANG})
endfunction()

# ---------------------------------------------------------------- library
add_library(ff_core STATIC ${FF_LIB_SOURCES})
ff_lib_options(ff_core)
target_compile_definitions(ff_core PUBLIC FOURFOLDCORE_API=)

# ---------------------------------------------------------------- tests
file(GLOB FF_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
add_executable(ff_tests ${FF_TEST_SOURCES})
target_link_libraries(ff_tests PRIVATE ff_core)
target_include_directories(ff_tests PRIVATE "${FF_MODULE}/Private" "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_options(ff_tests PRIVATE ${FF_TEST_WARN} ${FF_LANG})
target_compile_definitions(ff_tests PRIVATE FF_DATA_DIR="${FF_DATA_DIR}" FF_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")

# ---------------------------------------------------------------- perf / soak
add_executable(ff_perf "${CMAKE_CURRENT_SOURCE_DIR}/perf/perf_main.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/sim_harness.cpp"
                       "${CMAKE_CURRENT_SOURCE_DIR}/src/test_kit.cpp")
target_link_libraries(ff_perf PRIVATE ff_core)
target_include_directories(ff_perf PRIVATE "${FF_MODULE}/Private" "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_options(ff_perf PRIVATE ${FF_TEST_WARN} ${FF_LANG})

# ---------------------------------------------------------------- export check (modular editor build on the Mac)
add_library(ff_core_shared SHARED ${FF_LIB_SOURCES})
target_include_directories(ff_core_shared PUBLIC "${FF_MODULE}/Public" PRIVATE "${FF_MODULE}/Private")
target_compile_options(ff_core_shared PRIVATE ${FF_WARN} ${FF_LANG} -fvisibility=hidden -fvisibility-inlines-hidden)
target_compile_definitions(ff_core_shared PUBLIC "FOURFOLDCORE_API=__attribute__((visibility(\"default\")))")
set_target_properties(ff_core_shared PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
if(NOT APPLE)
  target_link_options(ff_core_shared PRIVATE -Wl,--no-undefined)
endif()

add_executable(ff_facade_link_test "${CMAKE_CURRENT_SOURCE_DIR}/link/facade_link_test.cpp")
target_link_libraries(ff_facade_link_test PRIVATE ff_core_shared)
target_compile_options(ff_facade_link_test PRIVATE ${FF_TEST_WARN} ${FF_LANG})

# ---------------------------------------------------------------- Unreal macro poison (compile only)
add_library(ff_poison OBJECT ${FF_LIB_SOURCES})
ff_lib_options(ff_poison)
target_compile_definitions(ff_poison PRIVATE FOURFOLDCORE_API=)
target_compile_options(ff_poison PRIVATE -include "${CMAKE_CURRENT_SOURCE_DIR}/ue_macro_poison.h")

# ---------------------------------------------------------------- unity batches (compile only)
add_library(ff_unity OBJECT ${FF_LIB_SOURCES})
ff_lib_options(ff_unity)
target_compile_definitions(ff_unity PRIVATE FOURFOLDCORE_API=)
set_target_properties(ff_unity PROPERTIES UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE 16)

# Convenience: everything (`cmake --build <dir> --target ff_all`).
add_custom_target(ff_all DEPENDS ff_tests ff_perf ff_facade_link_test ff_poison ff_unity)
