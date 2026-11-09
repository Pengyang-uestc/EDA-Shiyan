cmake_minimum_required(VERSION 3.21)
project(EDAExperiment LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
option(BUILD_TESTING "Build core tests" ON)
option(EDA_DEFAULT_BOARD_DEMO "Open the PCB demonstration on startup" OFF)
enable_testing()

find_package(Qt6 6.11.1 REQUIRED COMPONENTS Core Gui Widgets Test)

add_library(
  eda_core
  core/footprint.cpp
  core/footprint_io.cpp
  core/board_io.cpp
  core/netlist_io.cpp
  core/library.cpp
  core/geometry.cpp
  core/board.cpp
  core/api.cpp
)
target_include_directories(eda_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(eda_core PUBLIC Qt6::Core Qt6::Gui)

add_library(
  eda_app
  app/commands.h
  app/document_context.cpp
  app/document_context.h
  app/document.h
  app/documents.cpp
  app/demo_documents.h
  app/demo_documents.cpp
  app/local_services.h
  app/local_services.cpp
  app/main_window.h
  app/main_window.cpp
  app/panels.cpp
  app/panels.h
  app/services.h
  render/canvas.h
  render/canvas.cpp
  render/tool.h
  feat_fp/basic_commands.cpp
  feat_fp/basic_commands.h
  feat_pcb/board_commands.cpp
  feat_pcb/board_commands.h
)
target_link_libraries(eda_app PUBLIC eda_core Qt6::Widgets)
target_include_directories(eda_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(MSVC)
  add_compile_options(/utf-8 /W4)
  target_compile_options(eda_core PRIVATE /utf-8 /W4)
  target_compile_options(eda_app PRIVATE /utf-8 /W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
  target_compile_options(eda_core PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(eda_app PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(eda_core_demo examples/core_demo.cpp)
target_link_libraries(eda_core_demo PRIVATE eda_core)

add_executable(eda_board_demo examples/board_demo.cpp)
target_link_libraries(eda_board_demo PRIVATE eda_core)

add_executable(eda_gui app/main.cpp)
target_link_libraries(eda_gui PRIVATE eda_app)
if(EDA_DEFAULT_BOARD_DEMO)
  target_compile_definitions(eda_gui PRIVATE EDA_DEFAULT_BOARD_DEMO=1)
endif()

if(BUILD_TESTING)
  add_executable(test_core tests/tst_core.cpp)
  target_link_libraries(test_core PRIVATE eda_core Qt6::Test)
  add_test(NAME core COMMAND test_core)

  add_executable(test_b tests/tst_b.cpp)
  target_link_libraries(test_b PRIVATE eda_app Qt6::Test)
  add_test(NAME b_gui COMMAND test_b)
endif()
