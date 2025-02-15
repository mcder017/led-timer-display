//
// Created by WMcD on 12/8/2024.
//

#ifndef RECEIVER_H
#define RECEIVER_H

#include "thread.h"

#include <string>
#include <deque>
#include <chrono>
#include <utility>
#include <netinet/in.h>

class Receiver : public rgb_matrix::Thread {
public:
     static constexpr int TCP_PORT_DEFAULT = 21967;

     static constexpr uint32_t PROTOCOL_MESSAGE_MAX_LENGTH = 96;   // longest valid protocol message, including end-of-line
     enum Protocol {ALGE_DLINE,    // see "Alge timing manual for D-LINE / D-SAT"
                    SIMPLE_TEXT,  // data is short string to display on board
                    UNKNOWN};
     struct RawMessage {
          const Protocol protocol;
          const std::string data;
          const std::chrono::time_point<std::chrono::system_clock> timestamp;

          RawMessage() : protocol(UNKNOWN), data(), timestamp(std::chrono::system_clock::now()) {}
          RawMessage(const Protocol p, std::string  s)
               : protocol(p), data(std::move(s)), timestamp(std::chrono::system_clock::now()) {}
          RawMessage(const Protocol p, std::string  s, std::chrono::time_point<std::chrono::system_clock> t)
               : protocol(p), data(std::move(s)), timestamp(t) {}
     };

     Receiver();    // use default port
     explicit Receiver(int aPort_number);
     ~Receiver() override;

     virtual void Start();

     // Stop the thread at the next possible time Run() checks the running_ flag.
     void Stop() {
          rgb_matrix::MutexLock l(&mutex_);
          running_ = false;
     }

     // Implement this and run while running() returns true.
     void Run() override;

     bool isRunning() {return running();}    // sets MutexLock internally

     bool isPendingMessage() {
          rgb_matrix::MutexLock l(&mutex_);
          return !message_queue.empty();
     }

     RawMessage popPendingMessage() {
          rgb_matrix::MutexLock l(&mutex_);
          const RawMessage pendingMessage = message_queue.front();
          message_queue.pop_front();
          return pendingMessage;
     }

     void setClearOnUnrecognizedMessage(bool doClear) {
          rgb_matrix::MutexLock l(&mutex_);
          clearDisplayOnUnrecognizedMessage = doClear;
     }
     [[nodiscard]] bool isClearOnUnrecognizedMessage() {
          rgb_matrix::MutexLock l(&mutex_);
          return clearDisplayOnUnrecognizedMessage;
     }

     bool isNoKnownConnections() {
          rgb_matrix::MutexLock l(&mutex_);
          return !running_ || newsockfd < 0; // avoid calling running() or isRunning() to avoid double-lock deadlock
     }
     std::string getLocalAddresses();

     static std::string nonprintableToHexadecimal(const char* str);

protected:
     static constexpr char PROTOCOL_END_OF_LINE = '\x0d';

     inline bool running() {
          rgb_matrix::MutexLock l(&mutex_);
          return running_;
     }

     void queueReceivedMessage(const RawMessage& aMessage) {
          rgb_matrix::MutexLock l(&mutex_);
          message_queue.push_back(aMessage);
     }

private:
     rgb_matrix::Mutex mutex_;
     bool running_;                          // use MutexLock to allow thread-safe read&write
     std::deque<RawMessage> message_queue;   // use MutexLock to allow thread-safe read&write

     int sockfd;
     int newsockfd;                          // use MutexLock to allow thread-safe read&write
     socklen_t clilen;
     char socket_buffer[PROTOCOL_MESSAGE_MAX_LENGTH+1];     // include room for end-of-string null
     struct sockaddr_in serv_addr, cli_addr;
     int port_number;

     bool clearDisplayOnUnrecognizedMessage; // use MutexLock to allow thread-safe read&write

     void setupSocket();
     void checkAndAcceptConnection();
     void checkAndAppendData(std::string& unprocessed_buffer);
     bool extractLineToQueue(std::string& unprocessed_buffer);
     void parseLineToQueue(const char* single_line_buffer);
     bool parseAlgeLineToQueue(const char* single_line_buffer);
};



#endif //RECEIVER_H
