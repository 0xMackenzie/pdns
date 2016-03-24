#pragma once
#include "config.h"

#include <condition_variable>
#include <queue>

class RemoteLogger
{
public:
  RemoteLogger(const ComboAddress& remote, uint16_t timeout=2, uint64_t maxQueuedEntries=100, uint8_t reconnectWaitTime=1);
  ~RemoteLogger();
  void logQuery(const DNSQuestion& dq);
  void logResponse(const DNSQuestion& dr);
  std::string toString()
  {
    return d_remote.toStringWithPort();
  }
private:
  bool reconnect();
  bool sendData(const char* buffer, size_t bufferSize);
  void worker();
  void queueData(const std::string& data);

  std::queue<std::string> d_writeQueue;
  std::mutex d_writeMutex;
  std::condition_variable d_queueCond;
  ComboAddress d_remote;
  uint64_t d_maxQueuedEntries;
  int d_socket{-1};
  uint16_t d_timeout;
  uint8_t d_reconnectWaitTime;
  std::thread d_thread;
};

