#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <sys/socket.h>

constexpr uint16_t FLAG_DATA = 0x0001;
constexpr uint16_t FLAG_ACK = 0x0002;

struct PacketHeader {
  uint32_t seq = 0;
  uint32_t ack = 0;
  uint16_t flag = 0;
  uint16_t len = 0;
  // checksum
};

class ReliableUDP {
public:
  explicit ReliableUDP(int sock_fd);
  ~ReliableUDP() = default;

  // blocking
  bool send(const void *data, size_t length, const sockaddr *dest,
            socklen_t destlen);
  std::vector<uint8_t> recv(sockaddr *src = nullptr,
                            socklen_t *srclen = nullptr);

  // non-blocking (epoll)
  // bool send_noblock(const void* data, size_t length, const sockaddr* dest,
  // socklen_t destlen); std::vector<uint8_t> recv_noblock(sockaddr* src =
  // nullptr, socklen_t* srclen = nullptr);
private:
  bool send_packet(const PacketHeader &header, const void *payload, size_t plen,
                   const sockaddr *dest, socklen_t destlen);

  bool recv_packet(PacketHeader &header, std::vector<uint8_t> &payload,
                   sockaddr *src = nullptr, socklen_t *srclen = nullptr);

  int sock_fd_;
  uint32_t next_seq_ = 0;
  uint32_t expected_seq_ = 0;
};