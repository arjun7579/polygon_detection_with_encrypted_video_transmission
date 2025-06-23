#include <opencv2/opencv.hpp>
#include <sodium.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
#include <iostream>
#include <cstdint>
#include <map>

#define PORT 9999
#define MAX_BUF   65536
#define CHUNK_SIZE 60000   // <— Add this to match the sender

const unsigned char KEY[crypto_stream_chacha20_IETF_KEYBYTES] =
    "thisisaverysecretkey12345678";

#pragma pack(push,1)
struct PacketHeader {
    uint32_t frame_id;
    uint16_t packet_idx;
    uint16_t packet_count;
    unsigned char nonce[crypto_stream_chacha20_IETF_NONCEBYTES];
};
#pragma pack(pop)

struct FrameBuffer {
    std::vector<uchar> data;
    unsigned char nonce[crypto_stream_chacha20_IETF_NONCEBYTES];
    uint16_t received = 0;
};

int main(){
    if (sodium_init() < 0) return -1;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return -1;
    }

    sockaddr_in src{};
    src.sin_family      = AF_INET;
    src.sin_port        = htons(PORT);
    src.sin_addr.s_addr = INADDR_ANY;
    if (bind(sock, (sockaddr*)&src, sizeof(src)) < 0) {
        perror("bind");
        return -1;
    }

    std::map<uint32_t, FrameBuffer> frames;
    std::vector<uchar> buf(MAX_BUF);

    while (true) {
        ssize_t len = recvfrom(sock, buf.data(), buf.size(), 0, nullptr, nullptr);
        if (len <= sizeof(PacketHeader)) continue;

        PacketHeader hdr;
        memcpy(&hdr, buf.data(), sizeof(hdr));
        uint32_t fid = ntohl(hdr.frame_id);
        uint16_t idx = ntohs(hdr.packet_idx);
        uint16_t cnt = ntohs(hdr.packet_count);

        auto &fb = frames[fid];
        if (fb.data.empty()) {
            // First packet of this frame: allocate full buffer
            fb.data.resize(static_cast<size_t>(cnt) * CHUNK_SIZE);
            memcpy(fb.nonce, hdr.nonce, sizeof(hdr.nonce));
        }

        size_t offset = idx * CHUNK_SIZE;
        size_t chunk_len = len - sizeof(hdr);
        memcpy(fb.data.data() + offset,
               buf.data() + sizeof(hdr),
               chunk_len);

        fb.received++;
        if (fb.received == cnt) {
            // Reassemble exact-size cipher blob
            size_t total_bytes = (cnt - 1) * CHUNK_SIZE + chunk_len;
            std::vector<uchar> cipher(
                fb.data.begin(),
                fb.data.begin() + total_bytes
            );
            std::vector<uchar> plain(total_bytes);

            crypto_stream_chacha20_ietf_xor(
                plain.data(),
                cipher.data(),
                total_bytes,
                fb.nonce,
                KEY
            );

            // Decode and display
            cv::Mat frame = cv::imdecode(plain, cv::IMREAD_COLOR);
            if (!frame.empty()) {
                cv::imshow("Receiver", frame);
                if (cv::waitKey(1) == 27) break;
            }
            frames.erase(fid);
        }
    }

    close(sock);
    return 0;
}
