#include "packet.h"
#include <cstring>

namespace rudpr {

Packet::Packet(const PacketHeader& h, const void* data, size_t len) 
    : header(h) {
  if (len > 0 && data != nullptr) {
    payload.resize(len);
    memcpy(payload.data(), data, len);
  }
}

std::vector<uint8_t> Packet::serialize() const {
  std::vector<uint8_t> buffer(size());
  memcpy(buffer.data(), &header, PacketHeader::SIZE);
  if (!payload.empty()) {
    memcpy(buffer.data() + PacketHeader::SIZE, payload.data(), payload.size());
  }
  return buffer;
}

bool Packet::deserialize(const uint8_t* data, size_t len, Packet& out) {
  if (len < PacketHeader::SIZE) {
    return false;
  }
  
  memcpy(&out.header, data, PacketHeader::SIZE);
  
  if (out.header.len > 0) {
    if (len < PacketHeader::SIZE + out.header.len) {
      return false;
    }
    out.payload.resize(out.header.len);
    memcpy(out.payload.data(), data + PacketHeader::SIZE, out.header.len);
  } else {
    out.payload.clear();
  }
  
  return true;
}

} // namespace rudpr
