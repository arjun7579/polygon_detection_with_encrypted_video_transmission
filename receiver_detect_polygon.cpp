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
#define MAX_BUF    65536
#define CHUNK_SIZE 60000   // must match sender

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

// Rectangle detector + drawer (red lines + size label)
// Replace detectAndDrawRectangles() with this:

void detectAndDrawPolygons(cv::Mat &frame) {
    cv::Mat gray, blur, edges;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(gray, blur, {5,5}, 1.5);
    cv::Canny(blur, edges, 50, 150);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(edges, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

    for (auto &c : contours) {
        double peri = cv::arcLength(c, true);
        std::vector<cv::Point> poly;
        cv::approxPolyDP(c, poly, 0.02 * peri, true);

        double area = std::fabs(cv::contourArea(poly));
        if (area < 500) 
            continue;  // filter out noise

        // Draw polygon in red
        cv::polylines(frame, poly, true, {0,0,255}, 2);

        // Determine shape name
        int v = (int)poly.size();
        std::string name;
        switch (v) {
            case 3:  name = "Triangle";    break;
            case 4:  name = "Quadrilateral"; break;
            case 5:  name = "Pentagon";     break;
            case 6:  name = "Hexagon";      break;
            default: name = std::to_string(v) + "-gon"; break;
        }

        // Compute centroid for label placement
        cv::Moments m = cv::moments(poly);
        int cx = int(m.m10 / m.m00);
        int cy = int(m.m01 / m.m00);

        // Draw the label background
        int font = cv::FONT_HERSHEY_SIMPLEX;
        double scale = 0.5;
        int thickness = 1, baseline = 0;
        cv::Size ts = cv::getTextSize(name, font, scale, thickness, &baseline);
        cv::Point org(cx - ts.width/2, cy - ts.height/2);
        cv::rectangle(frame,
                      org + cv::Point(0, baseline),
                      org + cv::Point(ts.width, -ts.height),
                      {0,0,255}, cv::FILLED);
        // Draw the text
        cv::putText(frame, name, org, font, scale, {255,255,255}, thickness);
    }
}


int main(){
    if (sodium_init() < 0) {
        std::cerr << "libsodium init failed\n";
        return -1;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); return -1; }

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
            fb.data.resize(static_cast<size_t>(cnt) * CHUNK_SIZE);
            memcpy(fb.nonce, hdr.nonce, sizeof(hdr.nonce));
        }

        size_t offset    = idx * CHUNK_SIZE;
        size_t chunk_len = len - sizeof(hdr);
        memcpy(fb.data.data() + offset,
               buf.data() + sizeof(hdr),
               chunk_len);
        fb.received++;

        if (fb.received == cnt) {
            size_t total_bytes = (cnt - 1)*CHUNK_SIZE + chunk_len;
            std::vector<uchar> cipher(fb.data.begin(),
                                      fb.data.begin() + total_bytes);
            std::vector<uchar> plain(total_bytes);

            crypto_stream_chacha20_ietf_xor(
                plain.data(), cipher.data(), total_bytes,
                fb.nonce, KEY
            );

            cv::Mat frame = cv::imdecode(plain, cv::IMREAD_COLOR);
            if (!frame.empty()) {
                detectAndDrawPolygons(frame);
                cv::imshow("Receiver with Rectangles", frame);
                if (cv::waitKey(1) == 27) break;
            }

            frames.erase(fid);
        }
    }

    close(sock);
    return 0;
}
