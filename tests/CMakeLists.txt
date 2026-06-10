# tests/CMakeLists.txt
#
# Minimal in-tree tests with no external framework dependency.
# Each test is a standalone executable that returns 0 on success.
# Swap for GoogleTest later if needed.

function(veda_add_test name src)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE veda_rtsp_core)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

veda_add_test(test_parser  test_parser.cpp)
veda_add_test(test_sdp     test_sdp.cpp)
veda_add_test(test_rtp     test_rtp.cpp)
veda_add_test(test_session test_session.cpp)
