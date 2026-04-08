# CMake generated Testfile for 
# Source directory: /home/user/claude_workspace/shm_mmap
# Build directory: /home/user/claude_workspace/shm_mmap/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(test_mmap_file "/home/user/claude_workspace/shm_mmap/build/test_mmap_file")
set_tests_properties(test_mmap_file PROPERTIES  TIMEOUT "60" _BACKTRACE_TRIPLES "/home/user/claude_workspace/shm_mmap/CMakeLists.txt;55;add_test;/home/user/claude_workspace/shm_mmap/CMakeLists.txt;59;add_shm_test;/home/user/claude_workspace/shm_mmap/CMakeLists.txt;0;")
add_test(test_tick_pool "/home/user/claude_workspace/shm_mmap/build/test_tick_pool")
set_tests_properties(test_tick_pool PROPERTIES  TIMEOUT "60" _BACKTRACE_TRIPLES "/home/user/claude_workspace/shm_mmap/CMakeLists.txt;55;add_test;/home/user/claude_workspace/shm_mmap/CMakeLists.txt;60;add_shm_test;/home/user/claude_workspace/shm_mmap/CMakeLists.txt;0;")
add_test(test_snapshot "/home/user/claude_workspace/shm_mmap/build/test_snapshot")
set_tests_properties(test_snapshot PROPERTIES  TIMEOUT "60" _BACKTRACE_TRIPLES "/home/user/claude_workspace/shm_mmap/CMakeLists.txt;55;add_test;/home/user/claude_workspace/shm_mmap/CMakeLists.txt;61;add_shm_test;/home/user/claude_workspace/shm_mmap/CMakeLists.txt;0;")
add_test(test_tick "/home/user/claude_workspace/shm_mmap/build/test_tick")
set_tests_properties(test_tick PROPERTIES  TIMEOUT "60" _BACKTRACE_TRIPLES "/home/user/claude_workspace/shm_mmap/CMakeLists.txt;55;add_test;/home/user/claude_workspace/shm_mmap/CMakeLists.txt;62;add_shm_test;/home/user/claude_workspace/shm_mmap/CMakeLists.txt;0;")
add_test(test_mmap_manager "/home/user/claude_workspace/shm_mmap/build/test_mmap_manager")
set_tests_properties(test_mmap_manager PROPERTIES  TIMEOUT "60" _BACKTRACE_TRIPLES "/home/user/claude_workspace/shm_mmap/CMakeLists.txt;55;add_test;/home/user/claude_workspace/shm_mmap/CMakeLists.txt;63;add_shm_test;/home/user/claude_workspace/shm_mmap/CMakeLists.txt;0;")
subdirs("_deps/googletest-build")
