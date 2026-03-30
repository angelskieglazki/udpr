#pragma once
#include "file_protocol.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace file_transfer {

// Структура для хранения состояния сборки файла
struct FileState {
  std::string file_id;
  uint64_t file_size;
  uint32_t total_chunks;
  std::vector<uint8_t> data;
  std::vector<bool> received_chunks;
  std::atomic<uint32_t> received_count{0};
  std::chrono::steady_clock::time_point start_time;
  bool complete = false;
  std::string output_path;

  mutable std::mutex cv_mutex;
  mutable std::condition_variable cv;

  FileState() = default;
  FileState(const std::string &id, uint64_t size, const std::string &path);

  bool add_chunk(uint32_t chunk_index, const uint8_t *chunk_data,
                 size_t chunk_len);

  bool is_complete() const { return complete; }

  double get_progress() const;
  bool save_to_disk();
  bool wait_for_complete(std::chrono::seconds timeout) const;
};

// Thread-safe хранилище файлов
class FileStorage {
public:
  FileStorage() = default;
  ~FileStorage() = default;

  // Запросить новый файл для скачивания
  // Создаёт запись в хранилище
  bool request_file(const std::string &file_id, uint64_t file_size,
                    const std::string &output_path);

  // Добавить чанк к файлу
  // Возвращает true если файл полностью собран
  bool add_chunk(const std::string &file_id, uint32_t chunk_index,
                 const uint8_t *data, size_t len);

  // Получить файл (если полностью собран)
  // Выбрасывает исключение если файл не готов
  std::vector<uint8_t> get_file(const std::string &file_id);

  bool has_file(const std::string &file_id) const;
  bool is_file_complete(const std::string &file_id) const;

  double get_progress(const std::string &file_id) const;

  void remove_file(const std::string &file_id);

  void cleanup_completed();
  void cleanup_old(std::chrono::seconds timeout);

  std::vector<std::string> get_active_files() const;

  bool wait_for_file(const std::string &file_id, std::chrono::seconds timeout);

private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, std::unique_ptr<FileState>> files_;
};

} // namespace file_transfer