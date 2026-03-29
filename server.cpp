#include "src/reliable_udp.h"
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace rudpr;

int main() {
  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) {
    std::cerr << "Failed to create socket\n";
    return 1;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(8080);
  addr.sin_addr.s_addr = INADDR_ANY;

  if (bind(sock, (sockaddr *)&addr, sizeof(addr)) < 0) {
    std::cerr << "Failed to bind socket\n";
    close(sock);
    return 1;
  }

  ReliableUDP rudp(sock);
  
  if (!rudp.init()) {
    std::cerr << "Failed to init RUDP\n";
    close(sock);
    return 1;
  }

  int msg_count = 0;
  
  // Устанавливаем callback для получения сообщений
  rudp.on_receive([&msg_count](const std::vector<uint8_t>& data, 
                                const sockaddr* src, socklen_t srclen) {
    msg_count++;
    std::cout << "[" << msg_count << "] Received " << data.size() << " bytes\n";
    
    // Выводим первые 80 символов
    std::string preview(data.begin(), data.begin() + std::min(size_t(80), data.size()));
    std::cout << "Preview: " << preview << "...\n\n";
  });

  std::cout << "Reliable UDP server listening on :8080 (epoll mode)\n";

  // Event loop
  while (true) {
    rudp.poll(-1);  // Бесконечное ожидание
  }

  close(sock);
}
