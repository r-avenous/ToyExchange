cmake_minimum_required(VERSION 3.20)

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)  # use upstream BoostConfig.cmake, not FindBoost
endif()

project(ToyExchange LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# lockfree::spsc_queue and static_string are both header-only, so the
# umbrella Boost::headers interface target is all that's needed.
find_package(Boost 1.81 REQUIRED)

include(FetchContent)
FetchContent_Declare(
    unordered_dense
    GIT_REPOSITORY https://github.com/martinus/unordered_dense.git
    GIT_TAG v4.8.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(unordered_dense)

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.15.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(spdlog)

find_package(Threads REQUIRED)

enable_testing()

add_subdirectory(common)
add_subdirectory(exchange)
add_subdirectory(tests)
