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

# The wheel-vs-stash rows (docs/perf/wheel-vs-stash.md) need only the landed wheel and stash:
# -DBENCH_WHEEL_VS_STASH=ON builds wheel_vs_stash and stash_niche and nothing else. stash is the pin
# scheduler's CMakeLists declares; wheel is the commit of the numbers.
option(BENCH_WHEEL_VS_STASH "Build only wheel_vs_stash and stash_niche" OFF)
if(BENCH_WHEEL_VS_STASH)
	FetchContent_Declare(wheel GIT_REPOSITORY https://github.com/Kronuz/wheel.git GIT_TAG bfd72bc13395e4a232701e752be97ccd236530fc)
	FetchContent_Declare(stash GIT_REPOSITORY https://github.com/Kronuz/stash.git GIT_TAG b7d887678a2149e38fd4bc9ec4491e414d76a81f)
	FetchContent_MakeAvailable(wheel stash)
	find_package(Threads REQUIRED)
	add_executable(wheel_vs_stash wheel_vs_stash.cc)
	add_executable(stash_niche stash_niche.cc)
	foreach(t wheel_vs_stash stash_niche)
		target_link_libraries(${t} PRIVATE wheel::wheel stash::stash Threads::Threads)
		target_compile_features(${t} PRIVATE cxx_std_20)
	endforeach()
	return()
endif()

set(COMPLETION_ASIO ON CACHE BOOL "" FORCE)
# peers_cost and conn_cost need consensus::transport (reactor and OpenSSL 3); run.sh turns it on for them.
option(BENCH_TRANSPORT "Build peers_cost over consensus::transport" OFF)
if(BENCH_TRANSPORT)
	set(CONSENSUS_TRANSPORT ON CACHE BOOL "" FORCE)
endif()
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

# Protocol tests and probes for the design's threading rules (review F1-F5, F11-F14, X2-X5, R1-R4, Xapiand review 2): assertions stay
# on in every build type, so the wheel's owner-thread checks run.
foreach(t wheel_test waiter_test blocking_entry reader_test placement_test lone_voter repartition_lost frame_admission)
	add_executable(${t} ${t}.cc)
	target_link_libraries(${t} PRIVATE consensus::consensus completion::asio)
	target_compile_options(${t} PRIVATE -UNDEBUG)
endforeach()

# The cross-loop hop a node-pair connection pays per received batch (D10, Xapiand review 2 X5).
add_executable(hop_cost hop_cost.cc)
target_link_libraries(hop_cost PRIVATE completion::asio)

if(BENCH_TRANSPORT)
	add_executable(peers_cost peers_cost.cc)
	target_link_libraries(peers_cost PRIVATE consensus::transport)
	add_executable(conn_cost conn_cost.cc)
	target_link_libraries(conn_cost PRIVATE consensus::transport)
endif()
