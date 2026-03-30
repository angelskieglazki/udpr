#include "file_storage.h"
#include <cstring>
#include <fstream>
#include <iostream>

using namespace file_transfer;

namespace file_transfer {

FileState::FileState(const std::string &id, uint64_t size,
                     const std::string &path)
    : file_id(id), file_size(size), total_chunks(calc_total_chunks(size)),
      data(size), received_chunks(total_chunks, false),
      start_time(std::chrono::steady_clock::now()), output_path(path) {}

bool FileState::add_chunk(uint32_t chunk_index, const uint8_t *chunk_data,
                          size_t chunk_len) {
  if (chunk_index >= total_chunks) {
    return false;
  }
  if (received_chunks[chunk_index]) {
    return false; // Чанк уже получен
  }

  // Вычисляем позицию в файле
  uint64_t position = static_cast<uint64_t>(chunk_index) * CHUNK_SIZE;
  if (position + chunk_len > file_size) {
    chunk_len = static_cast<size_t>(file_size - position);
  }

  // Копируем данные
  std::memcpy(data.data() + position, chunk_data, chunk_len);
  received_chunks[chunk_index] = true;
  received_count.fetch_add(1, std::memory_order_relaxed);

  // Проверяем завершение
  if (received_count.load(std::memory_order_relaxed) == total_chunks) {
    complete = true;
    save_to_disk();
    cv.notify_all();
  }

  return true;
}

bool FileState::wait_for_complete(std::chrono::seconds timeout) const {
  std::unique_lock<std::mutex> lock(cv_mutex);
  return cv.wait_for(lock, timeout, [this] { return complete; });
}

double FileState::get_progress() const {
  uint32_t received = received_count.load(std::memory_order_relaxed);
  return static_cast<double>(received) / static_cast<double>(total_chunks);
}

bool FileState::save_to_disk() {
  if (!complete) {
    return false;
  }
  std::ofstream file(output_path, std::ios::binary);
  if (!file) {
    std::cerr << "Failed to create output file: " << output_path << std::endl;
    return false;
  }
  file.write(reinterpret_cast<const char *>(data.data()), data.size());
  return file.good();
}

// ============================================================================
// FileStorage
// ============================================================================

bool FileStorage::request_file(const std::string &file_id, uint64_t file_size,
                               const std::string &output_path) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (files_.count(file_id)) {
    return false; // Файл уже существует
  }
  files_[file_id] =
      std::make_unique<FileState>(file_id, file_size, output_path);
  return true;
}

bool FileStorage::add_chunk(const std::string &file_id, uint32_t chunk_index,
                            const uint8_t *data, size_t len) {
  std::unique_lock<std::mutex> lock(mutex_);
  auto it = files_.find(file_id);
  if (it == files_.end()) {
    return false;
  }

  FileState *state = it->second.get();

  // Копируем данные для безопасности
  std::vector<uint8_t> chunk_data(data, data + len);
  lock.unlock();

  // Добавляем чанк
  bool is_complete =
      state->add_chunk(chunk_index, chunk_data.data(), chunk_data.size());

  return is_complete;
}

std::vector<uint8_t> FileStorage::get_file(const std::string &file_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = files_.find(file_id);
  if (it == files_.end()) {
    throw std::runtime_error("File not found: " + file_id);
  }
  if (!it->second->is_complete()) {
    throw std::runtime_error("File is not ready: " + file_id);
  }
  return it->second->data;
}

bool FileStorage::has_file(const std::string &file_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return files_.count(file_id) > 0;
}

bool FileStorage::is_file_complete(const std::string &file_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = files_.find(file_id);
  if (it == files_.end()) {
    return false;
  }
  return it->second->is_complete();
}

double FileStorage::get_progress(const std::string &file_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = files_.find(file_id);
  if (it == files_.end()) {
    return 0.0;
  }
  return it->second->get_progress();
}

void FileStorage::remove_file(const std::string &file_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  files_.erase(file_id);
}

void FileStorage::cleanup_completed() {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto it = files_.begin(); it != files_.end();) {
    if (it->second->is_complete()) {
      it = files_.erase(it);
    } else {
      ++it;
    }
  }
}

void FileStorage::cleanup_old(std::chrono::seconds timeout) {
  auto now = std::chrono::steady_clock::now();
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto it = files_.begin(); it != files_.end();) {
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - it->second->start_time);
    if (elapsed > timeout) {
      it = files_.erase(it);
    } else {
      ++it;
    }
  }
}

std::vector<std::string> FileStorage::get_active_files() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> result;
  result.reserve(files_.size());
  for (const auto &[id, state] : files_) {
    if (!state->is_complete()) {
      result.push_back(id);
    }
  }
  return result;
}

bool FileStorage::wait_for_file(const std::string& file_id, std::chrono::seconds timeout) {
  std::unique_lock<std::mutex> lock(mutex_);
  auto it = files_.find(file_id);
  if (it == files_.end()) {
    return false;
  }

  FileState* state = it->second.get();
  lock.unlock();
  
  return state->wait_for_complete(timeout);
}

} // namespace file_transfer