# Drone Video Transmission with ChaCha20 Encryption and Polygon Detection

This project demonstrates a complete end-to-end C++ solution for securely streaming video frames from a sender (e.g., a drone) to a receiver (ground station) using ChaCha20-IETF encryption, UDP transport with manual fragmentation, and real-time polygon (shape) detection overlay.

## Features

* **Video Capture & Transmission**: Uses OpenCV to capture frames from a camera and send them over UDP in encrypted form.
* **ChaCha20-IETF Encryption**: Each frame is encrypted with a unique 12-byte nonce for cryptographic security using libsodium.
* **UDP Fragmentation**: Frames are JPEG-encoded, encrypted, then split into MTU-safe UDP packets. Custom headers include frame ID, packet index/count, and nonce.
* **Reassembly & Decryption**: Receiver collects and reassembles packets per frame, decrypts using the stored nonce, and decodes back into an image.
* **Polygon Detection**: On the receiver, frames are processed with an OpenCV pipeline to detect contours, approximate polygons, and overlay each polygon in red with its type (triangle, quadrilateral, etc.).

##

### Ubuntu/Debian Installation

```bash
sudo apt update
sudo apt install build-essential libopencv-dev libsodium-dev cmake
```

## Build Instructions

### Using g++ directly

```bash
# Compile sender
g++ sender.cpp -o sender `pkg-config --cflags --libs opencv4` -lsodium

# Compile receiver 
g++ receiver.cpp -o receiver `pkg-config --cflags --libs opencv4` -lsodium

# Compile receiver with shape detection
g++ receiver_detect_polygon.cpp -o receiver `pkg-config --cflags --libs opencv4` -lsodium
```


## Usage

1. **Start the Receiver** (ground station):

   ```bash
   ./receiver 
   ```
   or
   ```
   ./receiver_detect_polygon

   ```
2. **Start the Sender** (drone):

   ```bash
   ./sender
   ```
3. A window will open on the receiver displaying the live video stream with detected polygons outlined in red and labeled by type.
4. Press **Esc** in either window to exit.

## How It Works

### Sender Side (`sender.cpp`)

1. **Capture Frame**: Grab a frame from the camera using OpenCV.
2. **JPEG Encode**: Compress the frame to a JPEG byte buffer.
3. **Encrypt**: Generate a random 12-byte nonce and encrypt the JPEG buffer using `crypto_stream_chacha20_ietf_xor`.
4. **Fragmentation**: Split the encrypted buffer into chunks of up to 60,000 bytes. Each UDP packet includes:

   * `frame_id` (4 bytes, network order)
   * `packet_idx` (2 bytes)
   * `packet_count` (2 bytes)
   * `nonce` (12 bytes, only in the first packet)
   * encrypted chunk
5. **Send**: Transmit each packet via UDP to the receiver’s IP/port.

### Receiver Side (`receiver`\_`detect`\_polygon.`cpp`)

1. **Receive Packets**: Listen on the UDP port and receive incoming packets.
2. **Reassemble**: Group packets by `frame_id`, allocate a buffer sized `packet_count * CHUNK_SIZE`, and copy each chunk into position.
3. **Decrypt**: Once all packets for a frame arrive, extract the nonce, decrypt the assembled buffer, and JPEG-decode it back to an OpenCV `Mat`.
4. **Shape Detection Pipeline**:

   * Convert to grayscale and apply Gaussian blur.
   * Run Canny edge detection to find edges.
   * Find and approximate contours to polygons using `approxPolyDP`.
   * Filter by area to remove noise.
   * For each polygon, draw its outline in red and label it by the number of sides (e.g., "Triangle", "5-gon").
5. **Display**: Show the annotated frame in a window.

## Customization

* **Port & Address**: Modify `PORT` and destination IP in `sender.cpp` and bind address in `receiver_shapes.cpp`.
* **Chunk Size**: Adjust `CHUNK_SIZE` to fit different MTU constraints.
* **Detection Thresholds**: Tweak Canny thresholds, blur kernel, and minimum area to suit your environment.
* **Encryption Key Management**: Replace the hardcoded key with a secure key exchange mechanism for production use.

##

Made with C++ 🛠️ by arjun7579