#pragma once
#include <cstdint>
#include <chrono>

namespace rudpr {

// Флаги пакета
constexpr uint16_t FLAG_DATA = 0x0001;
constexpr uint16_t FLAG_ACK = 0x0002;
constexpr uint16_t FLAG_FRAG_START = 0x0010;
constexpr uint16_t FLAG_FRAG_MIDDLE = 0x0020;
constexpr uint16_t FLAG_FRAG_END = 0x0040;

// MTU и размеры
constexpr size_t MTU_PAYLOAD_SIZE = 1400;
constexpr size_t MAX_PACKET_SIZE = 1500;

// Таймауты
constexpr int DEFAULT_TIMEOUT_MS = 500;
constexpr int MAX_RETRIES = 10;
constexpr auto ASSEMBLER_TIMEOUT = std::chrono::seconds(30);
constexpr auto RETRANSMISSION_INTERVAL = std::chrono::milliseconds(100);

// Заголовок пакета
struct PacketHeader {
  uint32_t seq = 0;           // Sequence number
  uint32_t ack = 0;           // ACK number
  uint16_t flag = 0;          // Флаги (DATA, ACK, FRAG_*)
  uint16_t len = 0;           // Размер payload
  uint32_t msg_id = 0;        // ID сообщения (для фрагментов)
  uint16_t frag_index = 0;    // Индекс фрагмента
  uint16_t frag_total = 0;    // Общее число фрагментов
  
  static constexpr size_t SIZE = 20; // sizeof(PacketHeader)
};

// Хелперы для флагов
inline bool is_fragment(uint16_t flag) {
  return flag & (FLAG_FRAG_START | FLAG_FRAG_MIDDLE | FLAG_FRAG_END);
}

inline uint16_t get_fragment_flag(uint16_t frag_index, uint16_t total_frags) {
  if (total_frags == 1) return 0;
  if (frag_index == 0) return FLAG_FRAG_START;
  if (frag_index == total_frags - 1) return FLAG_FRAG_END;
  return FLAG_FRAG_MIDDLE;
}

} // namespace rudpr
