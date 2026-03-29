#pragma once
#include "packet.h"
#include <unordered_map>
#include <vector>
#include <chrono>
#include <sys/socket.h>

namespace rudpr {

// Ожидающий подтверждения пакет
struct PendingPacket {
  Packet packet;
  sockaddr_storage dest;
  socklen_t destlen;
  std::chrono::steady_clock::time_point send_time;
  int retries = 0;
  bool acked = false;
  
  PendingPacket() = default;
  PendingPacket(const Packet& p, const sockaddr* dst, socklen_t dstlen);
};


class RetransmissionManager {
public:
  // Добавляет пакет на отслеживание
  void add(uint32_t seq, const Packet& packet, const sockaddr* dest, socklen_t destlen);
  
  // Подтверждает получение (вызывать при получении ACK)
  void ack(uint32_t seq);
  
  // Проверяет таймауты, возвращает список seq для ретрансмиссии
  std::vector<uint32_t> check_timeouts();
  
  // Получает пакет для ретрансмиссии
  const PendingPacket* get(uint32_t seq) const;
  
  // Удаляет подтвержденные и превысившие лимит попыток
  void cleanup();
  
  // Есть ли ожидающие пакеты
  bool has_pending() const { return !pending_.empty(); }

private:
  std::unordered_map<uint32_t, PendingPacket> pending_;
};

} // namespace rudpr
