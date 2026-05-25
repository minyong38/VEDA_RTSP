// src/rtsp/response.cpp

#include "response.hpp"

#include <sstream>

namespace veda::rtsp {

std::string Response::ok(int cseq) {
    std::ostringstream oss;
    oss << "RTSP/1.0 200 OK\r\n"
        << "CSeq: " << cseq << "\r\n"
        << "\r\n";
    return oss.str();
}

std::string Response::options(int cseq) {
    std::ostringstream oss;
    oss << "RTSP/1.0 200 OK\r\n"
        << "CSeq: " << cseq << "\r\n"
        << "Public: OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN\r\n"
        << "\r\n";
    return oss.str();
}

std::string Response::describe(int cseq, const std::string& sdp,
                                const std::string& content_base) {
    std::ostringstream oss;
    oss << "RTSP/1.0 200 OK\r\n"
        << "CSeq: " << cseq << "\r\n"
        << "Content-Type: application/sdp\r\n"
        << "Content-Base: " << content_base << "\r\n"
        << "Content-Length: " << sdp.size() << "\r\n"
        << "\r\n"
        << sdp;
    return oss.str();
}

std::string Response::setup(int cseq, const std::string& session_id,
                             int client_rtp_port, int client_rtcp_port,
                             int server_rtp_port, int server_rtcp_port) {
    std::ostringstream oss;
    oss << "RTSP/1.0 200 OK\r\n"
        << "CSeq: " << cseq << "\r\n"
        << "Session: " << session_id << "\r\n"
        << "Transport: RTP/AVP;unicast;"
        << "client_port=" << client_rtp_port << "-" << client_rtcp_port << ";"
        << "server_port=" << server_rtp_port << "-" << server_rtcp_port << "\r\n"
        << "\r\n";
    return oss.str();
}

std::string Response::play(int cseq, const std::string& session_id) {
    std::ostringstream oss;
    oss << "RTSP/1.0 200 OK\r\n"
        << "CSeq: " << cseq << "\r\n"
        << "Session: " << session_id << "\r\n"
        << "Range: npt=0.000-\r\n"
        << "\r\n";
    return oss.str();
}

std::string Response::teardown(int cseq, const std::string& session_id) {
    std::ostringstream oss;
    oss << "RTSP/1.0 200 OK\r\n"
        << "CSeq: " << cseq << "\r\n"
        << "Session: " << session_id << "\r\n"
        << "\r\n";
    return oss.str();
}

std::string Response::error(int cseq, int status_code, const std::string& reason) {
    std::ostringstream oss;
    oss << "RTSP/1.0 " << status_code << " " << reason << "\r\n"
        << "CSeq: " << cseq << "\r\n"
        << "\r\n";
    return oss.str();
}

}  // namespace veda::rtsp
