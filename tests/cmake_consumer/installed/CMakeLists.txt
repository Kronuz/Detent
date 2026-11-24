# An application test against the installed development component (D149): find_package(Detent), no
# Detent source tree.
cmake_minimum_required(VERSION 3.24)
project(detent_installed_check LANGUAGES CXX)
find_package(Detent CONFIG REQUIRED)
add_executable(detent_installed_check main.cc)
target_link_libraries(detent_installed_check PRIVATE detent::reader detent::testing)
