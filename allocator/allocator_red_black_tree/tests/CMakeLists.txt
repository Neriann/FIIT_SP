add_executable(
        sys_prog_allctr_allctr_rb_tr_tests
        allocator_red_black_tree_tests.cpp)

# Core tree unit-tests (insert/find/erase and RB invariants).
add_executable(
        sys_prog_allctr_allctr_rb_tr_core_tests
        ../core/tests/red_black_tree_tests.cpp)

target_link_libraries(
        sys_prog_allctr_allctr_rb_tr_tests
        PRIVATE
        gtest_main)
target_link_libraries(
        sys_prog_allctr_allctr_rb_tr_tests
        PRIVATE
        sys_prog_allctr_allctr_rb_tr)

target_link_libraries(
        sys_prog_allctr_allctr_rb_tr_core_tests
        PRIVATE
        gtest_main)

target_link_libraries(
        sys_prog_allctr_allctr_rb_tr_core_tests
        PRIVATE
        sys_prog_allctr_allctr_rb_tr)
