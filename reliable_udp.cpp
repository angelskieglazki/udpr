#include "reliable_udp.h"
#include <arpa/inet.h>
#include <chrono>
#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

constexpr int TIMEOUT_MS = 500;
constexpr int MAX_RETRIES = 10;

ReliableUDP::ReliableUDP(int sock_fd) : sock_fd_(sock_fd) {}

bool ReliableUDP::send_packet(const PacketHeader &header, const void *payload,
                              size_t plen, const sockaddr *dest,
                              socklen_t destlen) {
  std::vector<uint8_t> buffer(sizeof(PacketHeader) + plen);
  memcpy(buffer.data(), &header, sizeof(PacketHeader));

  if (plen > 0 && payload != nullptr) {
    memcpy(buffer.data() + sizeof(PacketHeader), payload, plen);
  }

  ssize_t sent =
      sendto(sock_fd_, buffer.data(), buffer.size(), 0, dest, destlen);
  return sent == static_cast<ssize_t>(buffer.size());
}

bool ReliableUDP::recv_packet(PacketHeader &header,
                              std::vector<uint8_t> &payload, sockaddr *src,
                              socklen_t *srclen) {
  std::vector<uint8_t> buffer(4096);
  socklen_t len = sizeof(sockaddr_in);

  ssize_t received = recvfrom(sock_fd_, buffer.data(), buffer.size(), 0, src,
                              srclen ? &len : nullptr);
  if (received < static_cast<ssize_t>(sizeof(PacketHeader))) {
    return false;
  }

  memcpy(&header, buffer.data(), sizeof(PacketHeader));
  payload.resize(header.len);
  if (header.len > 0 &&
      received >= static_cast<ssize_t>(sizeof(PacketHeader) + header.len)) {
    memcpy(payload.data(), buffer.data() + sizeof(PacketHeader), header.len);
  }

  if (srclen) {
    *srclen = len;
  }
  return true;
}

bool ReliableUDP::send(const void *data, size_t length, const sockaddr *dest,
                       socklen_t destlen) {
  uint32_t seq = next_seq_++;
  PacketHeader header{};
  header.seq = seq;
  header.flag = FLAG_DATA;
  header.len = static_cast<uint16_t>(length);

  auto start_time = std::chrono::steady_clock::now();

  for (int attempt = 0; attempt < MAX_RETRIES; ++attempt) {
    if (!send_packet(header, data, length, dest, destlen)) {
      std::cerr << "Failed to send packet" << '\n';
      return false;
    }

    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(sock_fd_, &read_fds);

    struct timeval timeout;
    timeout.tv_sec = TIMEOUT_MS / 1000;
    timeout.tv_usec = (TIMEOUT_MS % 1000) * 1000;

    int ret = select(sock_fd_ + 1, &read_fds, nullptr, nullptr, &timeout);

    if (ret > 0) {
      PacketHeader ack_header{};
      std::vector<uint8_t> ack_payload;
      if (recv_packet(ack_header, ack_payload)) {
        if (ack_header.flag & FLAG_ACK && ack_header.ack == seq) {
          return true;
        }
      }
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = now - start_time;
    if (elapsed > std::chrono::milliseconds(TIMEOUT_MS)) {
      std::cout << "Timeout/retry for seq=" << seq << " (attempt "
                << attempt + 1 << ")\n";
    }
  }
  std::cerr << "Max retries exceeded for seq=" << seq << "\n";
  return false;
}

std::vector<uint8_t> ReliableUDP::recv(sockaddr *src, socklen_t *srclen) {
  while (true) {
    PacketHeader header{};
    std::vector<uint8_t> payload;

    if (!recv_packet(header, payload, src, srclen)) {
      continue;
    }

    if (header.flag & FLAG_DATA) {
      // Отправляем ACK только если известен адрес отправителя
      if (src && srclen) {
        PacketHeader ack_header{};
        ack_header.flag = FLAG_ACK;
        ack_header.ack = header.seq;
        send_packet(ack_header, nullptr, 0, src, *srclen);
      }

      if (header.seq == expected_seq_) {
        expected_seq_++;
        return payload;
      }
    }
  }
}