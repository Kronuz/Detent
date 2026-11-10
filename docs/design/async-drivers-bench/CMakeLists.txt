cmake_minimum_required(VERSION 3.16)
project(async_drivers_bench LANGUAGES CXX)

# Measures what docs/design/async-drivers.md relies on. The libraries come from the
# commits a job pins: -DFETCHCONTENT_SOURCE_DIR_<NAME>=<checkout> for consensus, journal,
# completion and task (run.sh passes the job's --ref checkouts); everything else at the
# commits those libraries declare.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)
set(COMPLETION_ASIO ON CACHE BOOL "" FORCE)
FetchContent_Declare(consensus GIT_REPOSITORY https://github.com/Kronuz/consensus.git GIT_TAG ab05507d8c88bb28d650c15dfe11c95e5db0eaf6)
FetchContent_Declare(scheduler GIT_REPOSITORY https://github.com/Kronuz/scheduler.git GIT_TAG d57e8fc051097d84500b4036b85daadd00c00b14)
FetchContent_MakeAvailable(consensus scheduler)

# Every translation unit sends task frames to operator new, so frames are counted (task.hh:438).
add_compile_definitions(TASK_FRAME_POOL=0)

add_executable(idle_cost idle_cost.cc)
target_link_libraries(idle_cost PRIVATE consensus::consensus completion::asio)

add_executable(driver_cost driver_cost.cc)
target_link_libraries(driver_cost PRIVATE consensus::consensus completion::asio scheduler)

add_executable(channel_layout channel_layout.cc)
target_link_libraries(channel_layout PRIVATE consensus::consensus)
