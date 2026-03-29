#include "fragmenter.h"
#include <algorithm>
#include <cstring>

namespace rudpr {

uint16_t Fragmenter::calc_fragment_count(size_t data_len) {
  if (data_len == 0) return 1;
  return (data_len + MTU_PAYLOAD_SIZE - 1) / MTU_PAYLOAD_SIZE;
}

std::vector<Packet> Fragmenter::fragment(const void* data, size_t len, 
                                          uint32_t msg_id, uint32_t& next_seq) {
  std::vector<Packet> fragments;
  
  const uint8_t* ptr = static_cast<const uint8_t*>(data);
  uint16_t total_frags = calc_fragment_count(len);
  
  for (uint16_t i = 0; i < total_frags; ++i) {
    size_t offset = i * MTU_PAYLOAD_SIZE;
    size_t frag_len = std::min(MTU_PAYLOAD_SIZE, len - offset);
    
    PacketHeader header;
    header.seq = next_seq++;
    header.flag = FLAG_DATA | get_fragment_flag(i, total_frags);
    header.len = static_cast<uint16_t>(frag_len);
    header.msg_id = msg_id;
    header.frag_index = i;
    header.frag_total = total_frags;
    
    fragments.emplace_back(header, ptr + offset, frag_len);
  }
  
  return fragments;
}

} // namespace rudpr
