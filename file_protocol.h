#pragma once
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace file_transfer {

// Размер чанка файла
constexpr size_t CHUNK_SIZE = 64 * 1024; // 64KB
constexpr size_t MAX_FILE_ID_LEN = 256;

// Типы команд
enum CommandType : uint8_t {
  CMD_FILE_REQUEST = 0x01,
  CMD_FILE_RESPONSE = 0x02,
  CMD_FILE_CHUNK = 0x03
};

// Статусы ответа
enum ResponseStatus : uint8_t {
  STATUS_OK = 0x00,
  STATUS_NOT_FOUND = 0x01,
  STATUS_ERROR = 0x02
};

// ============================================================================
// FileRequest - клиент запрашивает файл
// [CMD:1][file_id:N] (null-terminated string)
// ============================================================================
struct FileRequest {
  std::string file_id;

  std::vector<uint8_t> serialize() const {
    std::vector<uint8_t> data;
    data.reserve(1 + file_id.size() + 1);
    data.push_back(CMD_FILE_REQUEST);
    data.insert(data.end(), file_id.begin(), file_id.end());
    data.push_back('\0');
    return data;
  }

  static bool deserialize(const uint8_t *data, size_t len, FileRequest &req) {
    if (len < 2 || data[0] != CMD_FILE_REQUEST)
      return false;
    const char *str = reinterpret_cast<const char *>(data + 1);
    size_t max_len = len - 1;
    size_t str_len = strnlen(str, max_len);
    if (str_len >= max_len)
      return false;
    req.file_id = std::string(str, str_len);
    return !req.file_id.empty();
  }
};

// ============================================================================
// FileResponse - сервер отвечает с метаданными
// [CMD:1][status:1][file_id:N][file_size:8][total_chunks:4]
// ============================================================================
struct FileResponse {
  ResponseStatus status;
  std::string file_id;
  uint64_t file_size;
  uint32_t total_chunks;

  std::vector<uint8_t> serialize() const {
    std::vector<uint8_t> data;
    data.reserve(1 + 1 + file_id.size() + 1 + 8 + 4);
    data.push_back(CMD_FILE_RESPONSE);
    data.push_back(static_cast<uint8_t>(status));
    data.insert(data.end(), file_id.begin(), file_id.end());
    data.push_back('\0');
    // file_size (little-endian)
    for (int i = 0; i < 8; ++i) {
      data.push_back(static_cast<uint8_t>((file_size >> (i * 8)) & 0xFF));
    }
    // total_chunks (little-endian)
    for (int i = 0; i < 4; ++i) {
      data.push_back(static_cast<uint8_t>((total_chunks >> (i * 8)) & 0xFF));
    }
    return data;
  }

  static bool deserialize(const uint8_t *data, size_t len, FileResponse &resp) {
    if (len < 15 || data[0] != CMD_FILE_RESPONSE)
      return false;
    resp.status = static_cast<ResponseStatus>(data[1]);
    const char *str = reinterpret_cast<const char *>(data + 2);
    size_t max_str_len = len - 14;
    size_t str_len = strnlen(str, max_str_len);
    if (str_len >= max_str_len)
      return false;
    resp.file_id = std::string(str, str_len);
    size_t offset = 2 + str_len + 1;
    if (offset + 12 > len)
      return false;
    // file_size
    resp.file_size = 0;
    for (int i = 0; i < 8; ++i) {
      resp.file_size |= static_cast<uint64_t>(data[offset + i]) << (i * 8);
    }
    offset += 8;
    // total_chunks
    resp.total_chunks = 0;
    for (int i = 0; i < 4; ++i) {
      resp.total_chunks |= static_cast<uint32_t>(data[offset + i]) << (i * 8);
    }
    return true;
  }
};

// ============================================================================
// FileChunk - сервер отправляет чанк данных
// [CMD:1][file_id:N][chunk_idx:4][data_len:4][data...]
// ============================================================================
struct FileChunk {
  std::string file_id;
  uint32_t chunk_index;
  std::vector<uint8_t> data;

  std::vector<uint8_t> serialize() const {
    std::vector<uint8_t> result;
    result.reserve(1 + file_id.size() + 1 + 4 + 4 + data.size());
    result.push_back(CMD_FILE_CHUNK);
    result.insert(result.end(), file_id.begin(), file_id.end());
    result.push_back('\0');
    // chunk_index (little-endian)
    for (int i = 0; i < 4; ++i) {
      result.push_back(static_cast<uint8_t>((chunk_index >> (i * 8)) & 0xFF));
    }
    // data_len (little-endian)
    uint32_t data_len = static_cast<uint32_t>(data.size());
    for (int i = 0; i < 4; ++i) {
      result.push_back(static_cast<uint8_t>((data_len >> (i * 8)) & 0xFF));
    }
    // data
    result.insert(result.end(), data.begin(), data.end());
    return result;
  }

  static bool deserialize(const uint8_t *data, size_t len, FileChunk &chunk) {
    if (len < 11 || data[0] != CMD_FILE_CHUNK)
      return false;
    const char *str = reinterpret_cast<const char *>(data + 1);
    size_t max_str_len = len - 10;
    size_t str_len = strnlen(str, max_str_len);
    if (str_len >= max_str_len)
      return false;
    chunk.file_id = std::string(str, str_len);
    size_t offset = 1 + str_len + 1;
    if (offset + 8 > len)
      return false;
    // chunk_index
    chunk.chunk_index = 0;
    for (int i = 0; i < 4; ++i) {
      chunk.chunk_index |= static_cast<uint32_t>(data[offset + i]) << (i * 8);
    }
    offset += 4;
    // data_len
    uint32_t data_len = 0;
    for (int i = 0; i < 4; ++i) {
      data_len |= static_cast<uint32_t>(data[offset + i]) << (i * 8);
    }
    offset += 4;
    if (offset + data_len > len)
      return false;
    // data
    chunk.data.assign(data + offset, data + offset + data_len);
    return true;
  }
};

inline CommandType get_command_type(const uint8_t *data, size_t len) {
  if (len < 1)
    return static_cast<CommandType>(0);
  return static_cast<CommandType>(data[0]);
}

inline uint32_t calc_total_chunks(uint64_t file_size) {
  return static_cast<uint32_t>((file_size + CHUNK_SIZE - 1) / CHUNK_SIZE);
}

} // namespace file_transfer