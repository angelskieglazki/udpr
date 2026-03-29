#include "src/reliable_udp.h"
#include <arpa/inet.h>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <thread>

using namespace rudpr;

int main() {
  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) {
    std::cerr << "Failed to create socket\n";
    return 1;
  }
  
  ReliableUDP rudp(sock);
  
  if (!rudp.init()) {
    std::cerr << "Failed to init RUDP\n";
    close(sock);
    return 1;
  }

  sockaddr_in server{};
  server.sin_family = AF_INET;
  server.sin_port = htons(8080);
  inet_pton(AF_INET, "127.0.0.1", &server.sin_addr);

  // Тест 1: Короткое сообщение
  std::string short_msg = "Привет от надёжного UDP!";
  std::cout << "Test 1: Sending short message (" << short_msg.size() << " bytes)...\n";
  
  rudp.send(short_msg.data(), short_msg.size(), (sockaddr *)&server, sizeof(server));
  
  // Ждем отправки
  auto start = std::chrono::steady_clock::now();
  while (rudp.has_pending_send() || 
         std::chrono::steady_clock::now() - start < std::chrono::seconds(2)) {
    rudp.poll(100);
  }
  std::cout << "Short message sent\n\n";
  
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Тест 2: Большое сообщение с фрагментацией
  std::cout << "Test 2: Sending large message...\n";
  std::string large_msg;
  large_msg.reserve(50000);
  for (int i = 0; i < 500; ++i) {
    large_msg += "Строка " + std::to_string(i) + ": Это тестовые данные. ";
  }
  
  std::cout << "Large message size: " << large_msg.size() << " bytes\n";
  std::cout << "Expected fragments: " << (large_msg.size() + 1399) / 1400 << "\n";
  
  rudp.send(large_msg.data(), large_msg.size(), (sockaddr *)&server, sizeof(server));
  
  // Ждем завершения отправки
  start = std::chrono::steady_clock::now();
  int loops = 0;
  while (std::chrono::steady_clock::now() - start < std::chrono::seconds(10)) {
    rudp.poll(100);
    loops++;
    if (!rudp.has_pending_send()) {
      // Проверяем еще немного для ретрансмиссий
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      rudp.poll(0);
      break;
    }
  }
  
  std::cout << "Completed in " << loops << " loops\n";
  std::cout << "Large message transmission finished\n";

  close(sock);
}
