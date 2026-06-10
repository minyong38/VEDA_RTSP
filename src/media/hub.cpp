// src/media/hub.cpp
//
// 소스 1개 → 구독자 N명 분배 허브 구현.

#include "media/hub.hpp"

#include "camera/source.hpp"
#include "media/annexb_reader.hpp"
#include "media/file_source.hpp"
#include "util/base64.hpp"

#include <algorithm>
#include <cstdio>
#include <iostream>

namespace veda::media {

namespace {

// NAL 헤더의 하위 5비트가 NAL type.
//   1 = non-IDR slice (P 프레임), 5 = IDR slice (키프레임)
//   7 = SPS, 8 = PPS (프레임 아님)
constexpr uint8_t kNalSlice    = 1;
constexpr uint8_t kNalIdr      = 5;
constexpr uint8_t kNalSps      = 7;
constexpr uint8_t kNalPps      = 8;

uint8_t nal_type_of(const uint8_t* nal) {
    return nal[0] & 0x1F;
}

}  // namespace

Hub::Hub(SourceConfig cfg)
    : cfg_(std::move(cfg)),
      ts_increment_(static_cast<uint32_t>(90000 / (cfg_.fps > 0 ? cfg_.fps : 30))) {
    // 파일 소스는 내용이 고정돼 있으니 시작 전에 SPS/PPS를 미리 뽑아둘 수
    // 있다. 그러면 첫 DESCRIBE 응답부터 sprop-parameter-sets가 채워져서
    // 클라이언트가 디코더를 미리 준비한다 (첫 화면까지의 지연 감소).
    if (cfg_.source != "camera") {
        prescan_parameter_sets();
    }
}

Hub::~Hub() {
    // 펌프 스레드가 살아 있으면 멈추고 join. running_=false를 본 직후
    // 루프를 빠져나오므로 최대 NAL 한 개 읽는 시간만큼만 기다린다.
    running_ = false;
    if (pump_thread_.joinable()) {
        pump_thread_.join();
    }
}

std::unique_ptr<NalSource> Hub::make_source() const {
    if (cfg_.source == "camera") {
        return std::make_unique<camera::Source>(cfg_.width, cfg_.height, cfg_.fps);
    }
    return std::make_unique<FileSource>(cfg_.source, cfg_.fps, /*loop=*/true);
}

bool Hub::subscribe(std::shared_ptr<rtp::Streamer> streamer) {
    if (!streamer) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mu_);

    if (!running_) {
        // 직전 펌프 스레드가 스스로 종료한 경우(소스 EOF/에러)가 있을 수
        // 있다. joinable인 채로 새 스레드를 대입하면 std::terminate이므로
        // 먼저 거둬들인다. (running_=false이므로 곧바로 join된다)
        if (pump_thread_.joinable()) {
            pump_thread_.join();
        }

        source_ = make_source();
        if (!source_ || !source_->start()) {
            source_.reset();
            std::cerr << "[Hub] Failed to start media source: " << cfg_.source << "\n";
            return false;
        }

        running_ = true;
        pump_thread_ = std::thread(&Hub::pump, this);
        std::cout << "[Hub] Media source started (" << cfg_.source << ")\n";
    }

    subscribers_.push_back(std::move(streamer));
    std::cout << "[Hub] Subscriber added (total: " << subscribers_.size() << ")\n";
    return true;
}

void Hub::unsubscribe(const std::shared_ptr<rtp::Streamer>& streamer) {
    std::thread to_join;
    {
        std::lock_guard<std::mutex> lock(mu_);

        subscribers_.erase(
            std::remove(subscribers_.begin(), subscribers_.end(), streamer),
            subscribers_.end());
        std::cout << "[Hub] Subscriber removed (total: " << subscribers_.size() << ")\n";

        // 마지막 구독자가 나갔으면 소스를 끈다. join은 락 밖에서 —
        // 펌프 스레드가 on_nal에서 같은 락을 기다리고 있을 수 있어서,
        // 락을 쥔 채 join하면 데드락이다.
        if (subscribers_.empty() && running_) {
            running_ = false;
            to_join = std::move(pump_thread_);
        }
    }

    if (to_join.joinable()) {
        to_join.join();
        std::cout << "[Hub] Media source stopped (no subscribers)\n";
    }
}

void Hub::pump() {
    // 소스가 NAL을 내놓는 족족 분배한다. read_nal은 블로킹이므로 이 루프가
    // 도는 동안에도 epoll 스레드는 자유롭게 RTSP 요청을 처리한다.
    while (running_) {
        bool ok = source_->read_nal([this](const uint8_t* nal, std::size_t len) {
            on_nal(nal, len);
        });
        if (!ok) {
            std::cerr << "[Hub] Media source ended (EOF or error)\n";
            break;
        }
    }
    source_->stop();
    running_ = false;
}

void Hub::on_nal(const uint8_t* nal, std::size_t len) {
    if (nal == nullptr || len == 0) {
        return;
    }
    uint8_t type = nal_type_of(nal);

    std::lock_guard<std::mutex> lock(mu_);

    // SPS/PPS는 처음 본 것을 캐시 — 이후 DESCRIBE의 sprop 재료가 된다.
    if (type == kNalSps && sps_.empty()) {
        sps_.assign(nal, nal + len);
    } else if (type == kNalPps && pps_.empty()) {
        pps_.assign(nal, nal + len);
    }

    // 구독자 전원에게 같은 NAL을 전송. 각 Streamer가 자기 SSRC/sequence/
    // timestamp를 따로 들고 있으므로 클라이언트별 RTP 세션은 독립적이다.
    for (auto& s : subscribers_) {
        s->send_nal(nal, len);
        // VCL NAL = 프레임 한 장 끝 → timestamp를 다음 프레임으로 전진.
        // (SPS/PPS/SEI는 프레임이 아니므로 전진시키지 않는다)
        if (type == kNalSlice || type == kNalIdr) {
            s->advance_timestamp(ts_increment_);
        }
    }
}

void Hub::prescan_parameter_sets() {
    // 파일 앞부분만 잠깐 읽어 SPS/PPS를 찾는다. 보통 스트림 맨 앞 두 NAL이
    // SPS/PPS라 몇 KB 안에 끝난다. 못 찾아도 치명적이지 않으므로 (sprop이
    // 빈 채로 DESCRIBE 응답) 상한을 두고 포기한다.
    FILE* fp = std::fopen(cfg_.source.c_str(), "rb");
    if (!fp) {
        std::cerr << "[Hub] Prescan: cannot open " << cfg_.source << "\n";
        return;
    }

    AnnexBReader reader;
    int scanned = 0;
    constexpr int kMaxNals = 64;
    while (scanned < kMaxNals && (sps_.empty() || pps_.empty())) {
        bool ok = reader.read_nal(fp, [this](const uint8_t* nal, std::size_t len) {
            if (len == 0) return;
            uint8_t type = nal_type_of(nal);
            if (type == kNalSps && sps_.empty()) {
                sps_.assign(nal, nal + len);
            } else if (type == kNalPps && pps_.empty()) {
                pps_.assign(nal, nal + len);
            }
        });
        if (!ok) break;
        ++scanned;
    }
    std::fclose(fp);

    if (!sps_.empty() && !pps_.empty()) {
        std::cout << "[Hub] Prescan: SPS(" << sps_.size() << "B) / PPS("
                  << pps_.size() << "B) cached for SDP\n";
    }
}

std::string Hub::sprop_parameter_sets() {
    std::lock_guard<std::mutex> lock(mu_);
    if (sps_.empty() || pps_.empty()) {
        return {};
    }
    return util::base64_encode(sps_.data(), sps_.size()) + "," +
           util::base64_encode(pps_.data(), pps_.size());
}

}  // namespace veda::media
