#include "assembler.h"
#include <algorithm>

namespace rudpr {

AssemblyState::AssemblyState(uint16_t total) 
    : fragments(total), received(total, false), 
      total_fragments(total), received_count(0),
      start_time(std::chrono::steady_clock::now()) {}

std::vector<uint8_t> AssemblyState::assemble() const {
  size_t total_size = 0;
  for (const auto& frag : fragments) {
    total_size += frag.size();
  }
  
  std::vector<uint8_t> result;
  result.reserve(total_size);
  
  for (const auto& frag : fragments) {
    result.insert(result.end(), frag.begin(), frag.end());
  }
  
  return result;
}

bool Assembler::add_fragment(uint32_t msg_id, uint64_t addr_hash, const Packet& packet) {
  Key key = {msg_id, addr_hash};
  
  auto it = assemblies_.find(key);
  if (it == assemblies_.end()) {
    it = assemblies_.emplace(key, AssemblyState(packet.header.frag_total)).first;
  }
  
  AssemblyState& state = it->second;
  uint16_t idx = packet.header.frag_index;
  
  if (idx < state.total_fragments && !state.received[idx]) {
    state.fragments[idx] = packet.payload;
    state.received[idx] = true;
    state.received_count++;
  }
  
  return state.is_complete();
}

std::vector<uint8_t> Assembler::take_message(uint32_t msg_id, uint64_t addr_hash) {
  Key key = {msg_id, addr_hash};
  auto it = assemblies_.find(key);
  
  if (it == assemblies_.end() || !it->second.is_complete()) {
    return {};
  }
  
  std::vector<uint8_t> result = it->second.assemble();
  assemblies_.erase(it);
  return result;
}

bool Assembler::has_complete_message(uint32_t msg_id, uint64_t addr_hash) const {
  Key key = {msg_id, addr_hash};
  auto it = assemblies_.find(key);
  return it != assemblies_.end() && it->second.is_complete();
}

void Assembler::cleanup_old(std::chrono::seconds timeout) {
  auto now = std::chrono::steady_clock::now();
  for (auto it = assemblies_.begin(); it != assemblies_.end();) {
    if (now - it->second.start_time > timeout) {
      it = assemblies_.erase(it);
    } else {
      ++it;
    }
  }
}

} // namespace rudpr
