#pragma once
#include "protocol.h"
#include "packet.h"
#include "fragmenter.h"
#include "assembler.h"
#include "retransmission.h"
#include "epoll_loop.h"
#include <functional>
#include <queue>
#include <atomic>
#include <unordered_map>

namespace rudpr {

// Адрес отправителя + данные
struct ReceivedMessage {
  std::vector<uint8_t> data;
  sockaddr_storage addr;
  socklen_t addrlen;
};

// Callback для получения сообщений
using ReceiveCallback = std::function<void(const std::vector<uint8_t>&, 
                                            const sockaddr*, socklen_t)>;

class ReliableUDP {
public:
  explicit ReliableUDP(int sock_fd);
  ~ReliableUDP();
  ReliableUDP(const ReliableUDP&) = delete;
  ReliableUDP& operator=(const ReliableUDP&) = delete;
  
  // Инициализация (epoll + неблокирующий режим)
  bool init();
  
  // Неблокирующая отправка
  // Данные добавляются в очередь и отправляются при вызове poll()
  bool send(const void* data, size_t len, const sockaddr* dest, socklen_t destlen);

  void on_receive(ReceiveCallback cb) { receive_cb_ = std::move(cb); }
  
  // Обработка событий (вызывать периодически)
  // Обрабатывает входящие данные, ретрансмиссии, отправку из очереди
  void poll(int timeout_ms = 0);
 
  // Проверить, есть ли готовые к отправке данные
  bool has_pending_send() const { return !send_queue_.empty(); }

private: 
  bool set_nonblocking(int fd);
  bool send_raw(const Packet& packet, const sockaddr* dest, socklen_t destlen);
  bool recv_raw(Packet& packet, sockaddr* src, socklen_t* srclen);
  
  void process_incoming();
  void process_send_queue();
  void process_retransmissions();
  void handle_packet(const Packet& packet, const sockaddr* src, socklen_t srclen);
  
  uint64_t hash_addr(const sockaddr* addr, socklen_t len) const;
  
private:
  int sock_fd_;
  bool initialized_ = false;
  
  std::atomic<uint32_t> next_seq_{0};
  std::atomic<uint32_t> next_msg_id_{1};
  uint32_t expected_seq_ = 0;
  
  // Per-client sequence numbers for sending
  std::unordered_map<uint64_t, uint32_t> client_next_seq_;
  
  EpollLoop epoll_;
  Fragmenter fragmenter_;
  Assembler assembler_;
  RetransmissionManager rtx_manager_;
  
  // Очередь на отправку
  struct SendTask {
    std::vector<uint8_t> data;
    sockaddr_storage dest;
    socklen_t destlen;
    uint32_t msg_id;
  };
  std::queue<SendTask> send_queue_;
  
  // Callback
  ReceiveCallback receive_cb_;
};

} // namespace rudpr
