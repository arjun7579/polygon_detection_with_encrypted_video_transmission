#include <opencv2/opencv.hpp>
#include <sodium.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
#include <iostream>
#include <cstdint>

#define PORT 9999
// Max UDP payload (~65K) minus header and some margin
#define CHUNK_SIZE 60000

const unsigned char KEY[crypto_stream_chacha20_IETF_KEYBYTES] =
    "thisisaverysecretkey12345678"; // exactly 32 bytes

#pragma pack(push,1)
struct PacketHeader {
    uint32_t frame_id;
    uint16_t packet_idx;
    uint16_t packet_count;
    // nonce only meaningful in packet_idx==0
    unsigned char nonce[crypto_stream_chacha20_IETF_NONCEBYTES];
};
#pragma pack(pop)

int main(){
    if (sodium_init() < 0) {
        std::cerr << "libsodium init failed\n";
        return -1;
    }

    cv::VideoCapture cap(0);
    if(!cap.isOpened()){
        std::cerr << "Failed to open camera\n";
        return -1;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(PORT);
    dst.sin_addr.s_addr = inet_addr("127.0.0.1");

    uint32_t frame_id = 0;
    while(true){
        cv::Mat frame;
        cap >> frame;
        if(frame.empty()) break;

        // JPEG encode
        std::vector<uchar> plain;
        cv::imencode(".jpg", frame, plain);

        // Encrypt full blob
        std::vector<uchar> cipher(plain.size());
        unsigned char nonce[crypto_stream_chacha20_IETF_NONCEBYTES];
        randombytes_buf(nonce, sizeof nonce);
        crypto_stream_chacha20_ietf_xor(
            cipher.data(), plain.data(), plain.size(), nonce, KEY
        );

        // Fragment
        size_t total = cipher.size();
        uint16_t packet_count = (total + CHUNK_SIZE - 1) / CHUNK_SIZE;

        for(uint16_t idx = 0; idx < packet_count; ++idx){
            size_t offset = idx * CHUNK_SIZE;
            size_t this_size = std::min<size_t>(CHUNK_SIZE, total - offset);

            std::vector<uchar> buffer(sizeof(PacketHeader) + this_size);
            PacketHeader hdr;
            hdr.frame_id = htonl(frame_id);
            hdr.packet_idx = htons(idx);
            hdr.packet_count = htons(packet_count);
            if(idx == 0){
                memcpy(hdr.nonce, nonce, sizeof nonce);
            } else {
                memset(hdr.nonce, 0, sizeof hdr.nonce);
            }

            memcpy(buffer.data(), &hdr, sizeof hdr);
            memcpy(buffer.data() + sizeof hdr,
                   cipher.data() + offset, this_size);

            sendto(sock, buffer.data(), buffer.size(), 0,
                   (sockaddr*)&dst, sizeof dst);
        }

        frame_id++;
        if(cv::waitKey(1) == 27) break;
    }

    close(sock);
    return 0;
}
