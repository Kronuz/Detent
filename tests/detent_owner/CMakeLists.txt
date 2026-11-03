# T1 (docs/design/http-coroutines.md): the owner wait. TASK_COUNT_FRAMES is set in the source, so this
# program is its own ODR island.
add_executable(test_owner_wait test_owner_wait.cc)
target_link_libraries(test_owner_wait PRIVATE detent::owner_loop)
add_test(NAME test_owner_wait COMMAND test_owner_wait)
set_tests_properties(test_owner_wait PROPERTIES TIMEOUT 60)
