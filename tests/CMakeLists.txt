# tests/CMakeLists.txt
#
# Minimal in-tree tests with no external framework dependency.
# Each test is a standalone executable that returns 0 on success.
# Swap for GoogleTest later if needed.

# 이 테스트들은 검증을 전부 assert()로 한다. 그런데 Release 빌드는 NDEBUG를
# 정의해 assert를 통째로 날려버린다 → 테스트가 아무것도 검증하지 않고 무조건
# 통과하는 사고가 난다. 빌드 타입과 무관하게 assert가 항상 살아 있도록
# 테스트 타겟에서만 NDEBUG를 해제한다(-UNDEBUG).
function(veda_add_test name src)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE veda_rtsp_core)
    target_compile_options(${name} PRIVATE -UNDEBUG)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

veda_add_test(test_parser  test_parser.cpp)
veda_add_test(test_sdp     test_sdp.cpp)
veda_add_test(test_rtp     test_rtp.cpp)
veda_add_test(test_session test_session.cpp)
