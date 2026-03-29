#include "reliable_udp.h"
#include <cstring>
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

namespace rudpr {

ReliableUDP::ReliableUDP(int sock_fd) : sock_fd_(sock_fd) {}

ReliableUDP::~ReliableUDP() {
  // Ничего не делаем, socket закрывается снаружи
}

bool ReliableUDP::set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) return false;
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK) >= 0;
}

bool ReliableUDP::init() {
  if (initialized_) return true;
  
  // Неблокирующий режим
  if (!set_nonblocking(sock_fd_)) {
    std::cerr << "Failed to set non-blocking mode\n";
    return false;
  }
  
  // Epoll
  if (epoll_.fd() < 0) {
    std::cerr << "Failed to create epoll\n";
    return false;
  }
  
  if (!epoll_.add(sock_fd_, EPOLLIN)) {
    std::cerr << "Failed to add socket to epoll\n";
    return false;
  }
  
  initialized_ = true;
  return true;
}

bool ReliableUDP::send(const void* data, size_t len, 
                        const sockaddr* dest, socklen_t destlen) {
  if (!initialized_ && !init()) {
    return false;
  }
  
  SendTask task;
  task.data.resize(len);
  memcpy(task.data.data(), data, len);
  memcpy(&task.dest, dest, destlen);
  task.destlen = destlen;
  task.msg_id = next_msg_id_++;
  
  send_queue_.push(std::move(task));
  return true;
}

bool ReliableUDP::send_raw(const Packet& packet, const sockaddr* dest, socklen_t destlen) {
  auto buffer = packet.serialize();
  ssize_t sent = ::sendto(sock_fd_, buffer.data(), buffer.size(), 0, dest, destlen);
  return sent == static_cast<ssize_t>(buffer.size());
}

bool ReliableUDP::recv_raw(Packet& packet, sockaddr* src, socklen_t* srclen) {
  std::vector<uint8_t> buffer(MAX_PACKET_SIZE);
  sockaddr_storage src_storage;
  socklen_t len = sizeof(src_storage);
  
  ssize_t received = ::recvfrom(sock_fd_, buffer.data(), buffer.size(), 
                                 MSG_DONTWAIT, (sockaddr*)&src_storage, &len);
  
  if (received < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return false;  // Нет данных
    }
    return false;  // Ошибка
  }
  
  if (!Packet::deserialize(buffer.data(), received, packet)) {
    return false;
  }
  
  if (src) {
    memcpy(src, &src_storage, len);
  }
  if (srclen) {
    *srclen = len;
  }
  
  return true;
}

uint64_t ReliableUDP::hash_addr(const sockaddr* addr, socklen_t len) const {
  uint64_t hash = 0;
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(addr);
  for (size_t i = 0; i < std::min(size_t(len), size_t(16)); ++i) {
    hash = hash * 31 + bytes[i];
  }
  return hash;
}

void ReliableUDP::handle_packet(const Packet& packet, const sockaddr* src, socklen_t srclen) {
  if (packet.is_ack()) {
    // Подтверждение
    rtx_manager_.ack(packet.header.ack);
    return;
  }
  
  if (packet.is_data()) {
    // Отправляем ACK
    PacketHeader ack_hdr;
    ack_hdr.flag = FLAG_ACK;
    ack_hdr.ack = packet.header.seq;
    Packet ack_pkt(ack_hdr, nullptr, 0);
    send_raw(ack_pkt, src, srclen);
    
    // Обрабатываем данные
    if (packet.is_fragment()) {
      uint64_t addr_hash = hash_addr(src, srclen);
      bool complete = assembler_.add_fragment(packet.header.msg_id, addr_hash, packet);
      
      if (complete) {
        std::vector<uint8_t> data = assembler_.take_message(packet.header.msg_id, addr_hash);
        if (receive_cb_ && !data.empty()) {
          receive_cb_(data, src, srclen);
        }
      }
    } else {
      // Нефрагментированный пакет
      if (packet.header.seq == expected_seq_) {
        expected_seq_++;
        if (receive_cb_) {
          receive_cb_(packet.payload, src, srclen);
        }
      }
    }
  }
}

void ReliableUDP::process_incoming() {
  // Читаем все доступные пакеты
  while (true) {
    Packet packet;
    sockaddr_storage src;
    socklen_t srclen = sizeof(src);
    
    if (!recv_raw(packet, (sockaddr*)&src, &srclen)) {
      break;
    }
    
    handle_packet(packet, (sockaddr*)&src, srclen);
  }
}

void ReliableUDP::process_send_queue() {
  while (!send_queue_.empty()) {
    auto& task = send_queue_.front();
    
    uint32_t seq_start = next_seq_;
    uint32_t seq = seq_start;
    auto packets = fragmenter_.fragment(task.data.data(), task.data.size(),
                                        task.msg_id, seq);
    next_seq_ = seq;
    
    // Отправляем все фрагменты
    bool all_sent = true;
    for (size_t i = 0; i < packets.size(); ++i) {
      if (!send_raw(packets[i], (sockaddr*)&task.dest, task.destlen)) {
        all_sent = false;
        break;
      }
      // Добавляем на отслеживание
      rtx_manager_.add(seq_start + i, packets[i], (sockaddr*)&task.dest, task.destlen);
    }
    
    if (all_sent) {
      send_queue_.pop();
    } else {
      break;
    }
  }
}

void ReliableUDP::process_retransmissions() {
  // Проверяем таймауты
  auto to_retransmit = rtx_manager_.check_timeouts();
  
  for (uint32_t seq : to_retransmit) {
    const PendingPacket* pending = rtx_manager_.get(seq);
    if (pending) {
      send_raw(pending->packet, (sockaddr*)&pending->dest, pending->destlen);
    }
  }
  
  // Очистка
  rtx_manager_.cleanup();
  
  // Очистка сборщиков
  assembler_.cleanup_old();
}

void ReliableUDP::poll(int timeout_ms) {
  if (!initialized_) {
    init();
  }
  
  // Обрабатываем исходящую очередь
  process_send_queue();
  
  // Ждем события
  std::vector<EpollEvent> events;
  int nfds = epoll_.wait(events, timeout_ms);
  
  if (nfds > 0) {
    for (const auto& ev : events) {
      if (ev.fd == sock_fd_ && (ev.events & EPOLLIN)) {
        process_incoming();
      }
    }
  }
  
  // Ретрансмиссии
  process_retransmissions();
}

} // namespace rudpr
