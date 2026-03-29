#pragma once
#include "protocol.h"
#include <vector>
#include <cstdint>
#include <cstring>

namespace rudpr {

// Класс для работы с пакетом
class Packet {
public:
  PacketHeader header;
  std::vector<uint8_t> payload;
  
  Packet() = default;
  Packet(const PacketHeader& h, const void* data, size_t len);
  
  // Сериализация в буфер для отправки
  std::vector<uint8_t> serialize() const;
  
  // Десериализация из буфера
  // Возвращает true если пакет валиден
  static bool deserialize(const uint8_t* data, size_t len, Packet& out);
  
  // Размер полного пакета (header + payload)
  size_t size() const { return PacketHeader::SIZE + payload.size(); }
  
  bool is_ack() const { return header.flag & FLAG_ACK; }
  bool is_data() const { return header.flag & FLAG_DATA; }
  bool is_fragment() const { return rudpr::is_fragment(header.flag); }
};

} // namespace rudpr
