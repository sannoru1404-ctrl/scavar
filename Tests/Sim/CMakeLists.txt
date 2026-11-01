cmake_minimum_required(VERSION 3.20)
project(ScavarSim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(SCAVAR_SANITIZE "Build with ASan+UBSan" ON)

set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../Source/ScavarCore)
set(SIM_DIR ${CORE_DIR}/Sim)
set(PERSIST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../Source/ScavarPersistence)
file(GLOB SIM_SOURCES CONFIGURE_DEPENDS ${SIM_DIR}/*.cpp ${PERSIST_DIR}/Db.cpp)
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
list(FILTER TEST_SOURCES EXCLUDE REGEX ".*/FaultInjection\\.cpp$")

find_package(SQLite3 REQUIRED)

add_library(scavar_sim STATIC ${SIM_SOURCES})
target_include_directories(scavar_sim PUBLIC ${SIM_DIR} ${CORE_DIR} ${PERSIST_DIR})
target_link_libraries(scavar_sim PUBLIC SQLite::SQLite3)
target_compile_options(scavar_sim PRIVATE -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti)

add_executable(scavar_sim_tests ${TEST_SOURCES})
target_link_libraries(scavar_sim_tests PRIVATE scavar_sim)
target_compile_definitions(scavar_sim_tests PRIVATE SCAVAR_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../data")
target_compile_options(scavar_sim_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/FaultInjection.cpp)
  add_executable(scavar_fault_injection FaultInjection.cpp)
  target_link_libraries(scavar_fault_injection PRIVATE scavar_sim)
endif()

if(SCAVAR_SANITIZE)
  foreach(t scavar_sim scavar_sim_tests scavar_fault_injection)
    if(NOT TARGET ${t})
      continue()
    endif()
    target_compile_options(${t} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${t} PRIVATE -fsanitize=address,undefined)
  endforeach()
endif()

enable_testing()
add_test(NAME sim_tests COMMAND scavar_sim_tests)
