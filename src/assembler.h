#pragma once
#include "packet.h"
#include <map>
#include <vector>
#include <cstdint>
#include <chrono>

namespace rudpr {


struct AssemblyState {
  std::vector<std::vector<uint8_t>> fragments;
  std::vector<bool> received;
  uint16_t total_fragments = 0;
  uint16_t received_count = 0;
  std::chrono::steady_clock::time_point start_time;
  
  explicit AssemblyState(uint16_t total);
  bool is_complete() const { return received_count == total_fragments; }
  std::vector<uint8_t> assemble() const;
};


class Assembler {
public:
  // Добавляет фрагмент
  // key = (msg_id, addr_hash)
  // Возвращает true если сообщение полностью собрано
  bool add_fragment(uint32_t msg_id, uint64_t addr_hash, const Packet& packet);
  
  // Забирает собранное сообщение
  // Возвращает пустой вектор если сообщение не найдено
  std::vector<uint8_t> take_message(uint32_t msg_id, uint64_t addr_hash);

  bool has_complete_message(uint32_t msg_id, uint64_t addr_hash) const;
  void cleanup_old(std::chrono::seconds timeout = ASSEMBLER_TIMEOUT);

private:
  using Key = std::pair<uint32_t, uint64_t>;
  std::map<Key, AssemblyState> assemblies_;
};

} // namespace rudpr
