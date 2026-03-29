#pragma once
#include "packet.h"
#include <vector>
#include <cstdint>

namespace rudpr {

// Класс для разбиения данных на фрагменты
class Fragmenter {
public:
  // Разбивает данные на фрагменты
  // next_seq - ссылка на счетчик sequence numbers (будет увеличен)
  // Возвращает вектор пакетов-фрагментов
  std::vector<Packet> fragment(const void* data, size_t len, 
                                uint32_t msg_id, uint32_t& next_seq);
  
  // Вычисляет количество фрагментов для данных
  static uint16_t calc_fragment_count(size_t data_len);
};

} // namespace rudpr
