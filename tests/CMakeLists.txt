add_executable(test_c_geometry tst_geometry.cpp)
target_link_libraries(test_c_geometry PRIVATE eda_core Qt6::Test)
add_test(NAME c_geometry COMMAND test_c_geometry)

add_executable(test_c_board tst_board.cpp)
target_link_libraries(test_c_board PRIVATE eda_core Qt6::Test)
add_test(NAME c_board COMMAND test_c_board)