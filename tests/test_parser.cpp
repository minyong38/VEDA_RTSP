// tests/test_parser.cpp

#include "rtsp/parser.hpp"

#include <cassert>
#include <iostream>
#include <string>

using namespace veda::rtsp;

void test_options_request() {
    const std::string raw =
        "OPTIONS rtsp://localhost:8554/stream RTSP/1.0\r\n"
        "CSeq: 1\r\n"
        "User-Agent: ffplay\r\n"
        "\r\n";

    auto req = parse_request(raw);
    assert(req.has_value());
    assert(req->method == Method::OPTIONS);
    assert(req->uri == "rtsp://localhost:8554/stream");
    assert(req->cseq == 1);
    assert(header(*req, "User-Agent").has_value());
    assert(header(*req, "User-Agent").value() == "ffplay");

    std::cout << "[PASS] test_options_request\n";
}

void test_describe_request() {
    const std::string raw =
        "DESCRIBE rtsp://192.168.0.10:8554/live RTSP/1.0\r\n"
        "CSeq: 2\r\n"
        "Accept: application/sdp\r\n"
        "\r\n";

    auto req = parse_request(raw);
    assert(req.has_value());
    assert(req->method == Method::DESCRIBE);
    assert(req->uri == "rtsp://192.168.0.10:8554/live");
    assert(req->cseq == 2);
    assert(header(*req, "Accept").value() == "application/sdp");

    std::cout << "[PASS] test_describe_request\n";
}

void test_setup_request() {
    const std::string raw =
        "SETUP rtsp://localhost:8554/stream/track1 RTSP/1.0\r\n"
        "CSeq: 3\r\n"
        "Transport: RTP/AVP;unicast;client_port=5000-5001\r\n"
        "\r\n";

    auto req = parse_request(raw);
    assert(req.has_value());
    assert(req->method == Method::SETUP);
    assert(req->cseq == 3);

    auto transport = header(*req, "Transport");
    assert(transport.has_value());
    assert(transport->find("client_port=5000-5001") != std::string_view::npos);

    std::cout << "[PASS] test_setup_request\n";
}

void test_play_request() {
    const std::string raw =
        "PLAY rtsp://localhost:8554/stream RTSP/1.0\r\n"
        "CSeq: 4\r\n"
        "Session: 12345678\r\n"
        "Range: npt=0.000-\r\n"
        "\r\n";

    auto req = parse_request(raw);
    assert(req.has_value());
    assert(req->method == Method::PLAY);
    assert(req->cseq == 4);
    assert(header(*req, "Session").value() == "12345678");

    std::cout << "[PASS] test_play_request\n";
}

void test_teardown_request() {
    const std::string raw =
        "TEARDOWN rtsp://localhost:8554/stream RTSP/1.0\r\n"
        "CSeq: 5\r\n"
        "Session: 12345678\r\n"
        "\r\n";

    auto req = parse_request(raw);
    assert(req.has_value());
    assert(req->method == Method::TEARDOWN);
    assert(req->cseq == 5);

    std::cout << "[PASS] test_teardown_request\n";
}

void test_case_insensitive_header() {
    const std::string raw =
        "OPTIONS rtsp://localhost:8554/stream RTSP/1.0\r\n"
        "cseq: 10\r\n"
        "user-agent: VLC\r\n"
        "\r\n";

    auto req = parse_request(raw);
    assert(req.has_value());
    assert(req->cseq == 10);
    // 대소문자 무시해서 찾기
    assert(header(*req, "USER-AGENT").has_value());
    assert(header(*req, "User-Agent").has_value());

    std::cout << "[PASS] test_case_insensitive_header\n";
}

void test_invalid_request() {
    // 잘못된 형식
    assert(!parse_request("").has_value());
    assert(!parse_request("INVALID").has_value());
    assert(!parse_request("GET / HTTP/1.1\r\n\r\n").has_value());

    std::cout << "[PASS] test_invalid_request\n";
}

int main() {
    std::cout << "=== RTSP Parser Tests ===\n\n";

    test_options_request();
    test_describe_request();
    test_setup_request();
    test_play_request();
    test_teardown_request();
    test_case_insensitive_header();
    test_invalid_request();

    std::cout << "\n=== All tests passed! ===\n";
    return 0;
}
