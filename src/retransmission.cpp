#include "retransmission.h"
#include <cstring>

namespace rudpr {

PendingPacket::PendingPacket(const Packet& p, const sockaddr* dst, socklen_t dstlen)
    : packet(p), destlen(dstlen), send_time(std::chrono::steady_clock::now()), 
      retries(0), acked(false) {
  memcpy(&dest, dst, dstlen);
}

void RetransmissionManager::add(uint32_t seq, const Packet& packet, 
                                 const sockaddr* dest, socklen_t destlen) {
  pending_.emplace(seq, PendingPacket(packet, dest, destlen));
}

void RetransmissionManager::ack(uint32_t seq) {
  auto it = pending_.find(seq);
  if (it != pending_.end()) {
    it->second.acked = true;
  }
}

std::vector<uint32_t> RetransmissionManager::check_timeouts() {
  std::vector<uint32_t> to_retransmit;
  auto now = std::chrono::steady_clock::now();
  
  for (auto& [seq, pending] : pending_) {
    if (pending.acked) continue;
    
    if (now - pending.send_time > RETRANSMISSION_INTERVAL) {
      if (pending.retries < MAX_RETRIES) {
        to_retransmit.push_back(seq);
      } else {
        pending.acked = true;  // Помечаем для удаления
      }
    }
  }
  
  return to_retransmit;
}

const PendingPacket* RetransmissionManager::get(uint32_t seq) const {
  auto it = pending_.find(seq);
  if (it != pending_.end()) {
    return &it->second;
  }
  return nullptr;
}

void RetransmissionManager::cleanup() {
  for (auto it = pending_.begin(); it != pending_.end();) {
    if (it->second.acked) {
      it = pending_.erase(it);
    } else {
      ++it;
    }
  }
}

} // namespace rudpr
