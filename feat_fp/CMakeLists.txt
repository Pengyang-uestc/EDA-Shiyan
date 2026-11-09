add_library(eda_features STATIC
    footprint_validation.cpp
    footprint_templates.cpp)

target_link_libraries(eda_features PUBLIC eda_core)

if (BUILD_TESTING)
    add_executable(test_footprint_validation
        ${CMAKE_SOURCE_DIR}/tests/tst_footprint_validation.cpp)
    target_link_libraries(test_footprint_validation PRIVATE eda_features Qt6::Test)
    add_test(NAME footprint_validation COMMAND test_footprint_validation)

    add_executable(test_footprint_templates
        ${CMAKE_SOURCE_DIR}/tests/tst_footprint_templates.cpp)
    target_link_libraries(test_footprint_templates PRIVATE eda_features Qt6::Test)
    add_test(NAME footprint_templates COMMAND test_footprint_templates)

    endif()