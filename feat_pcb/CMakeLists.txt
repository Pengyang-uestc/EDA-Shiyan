add_library(eda_pcb_features STATIC
    board_validation.cpp
    connectivity.cpp
    pad_geometry.cpp
    drc.cpp
    netlist_service.cpp
    union_find.cpp)

target_link_libraries(eda_pcb_features PUBLIC eda_core)

if (BUILD_TESTING)
    add_executable(test_c_board_validation
        ${CMAKE_SOURCE_DIR}/tests/tst_board_validation.cpp)
    target_link_libraries(test_c_board_validation PRIVATE eda_pcb_features Qt6::Test)
    add_test(NAME c_board_validation COMMAND test_c_board_validation)

    add_executable(test_c_connectivity
        ${CMAKE_SOURCE_DIR}/tests/tst_connectivity.cpp)
    target_link_libraries(test_c_connectivity PRIVATE eda_pcb_features Qt6::Test)
    add_test(NAME c_connectivity COMMAND test_c_connectivity)

    add_executable(test_c_pad_geometry
        ${CMAKE_SOURCE_DIR}/tests/tst_pad_geometry.cpp)
    target_link_libraries(test_c_pad_geometry PRIVATE eda_pcb_features Qt6::Test)
    add_test(NAME c_pad_geometry COMMAND test_c_pad_geometry)

    add_executable(test_c_drc
        ${CMAKE_SOURCE_DIR}/tests/tst_drc.cpp)
    target_link_libraries(test_c_drc PRIVATE eda_pcb_features Qt6::Test)
    add_test(NAME c_drc COMMAND test_c_drc)

    add_executable(test_c_netlist_service
        ${CMAKE_SOURCE_DIR}/tests/tst_netlist_service.cpp)
    target_link_libraries(test_c_netlist_service PRIVATE eda_pcb_features Qt6::Test)
    add_test(NAME c_netlist_service COMMAND test_c_netlist_service)
endif()