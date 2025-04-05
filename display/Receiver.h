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

     struct ClientSummary {
          int num_clients;
          int active_client_index; // if negative, no active client.  otherwise, zero-based index of currently active client

          ClientSummary(int aClientCount, int aActiveIndex) : num_clients(aClientCount), active_client_index(aActiveIndex) {}
     }

     Receiver();    // use default port
     explicit Receiver(int aPort_number);
     ~Receiver() override;

     virtual void Start();

     // Stop the thread at the next possible time Run() checks the running_ flag.
     void Stop() {
          rgb_matrix::MutexLock l(&mutex_is_running);
          running_ = false;
     }

     // Implement this and run while running() returns true.
     void Run() override;

     bool isRunning() {return lockedTestRunning();}    // sets MutexLock internally

     bool isPendingMessage() {
          rgb_matrix::MutexLock l(&mutex_msg_queue);
          return !active_message_queue.empty();
     }

     RawMessage popPendingMessage() {
          rgb_matrix::MutexLock l(&mutex_msg_queue);
          const RawMessage pendingMessage = active_message_queue.front();
          active_message_queue.pop_front();
          return pendingMessage;
     }

     bool isNoKnownConnections() {
          rgb_matrix::MutexLock l(&mutex_descriptors);
          return num_socket_descriptors < 2;  // first socket is port listener
     }

     ClientSummary getClientSummary() {
          rgb_matrix::MutexLock l(&mutex_descriptors);
          ClientSummary summary(num_socket_descriptors-1, -1);
          if (active_display_sockfd >= 0) {
               for (int i=0; i < num_socket_descriptors; i++) {
                    if (socket_descriptors[i].fd == active_display_sockfd) {
                         summary.active_client_index = i - 1; // -1 as first socket is port listener
                         break;
                    }
               }
          }
          return summary;
     }

     bool setActiveClient(int aClientIndex) {
          rgb_matrix::MutexLock l(&mutex_descriptors);
          if (aClientIndex < 0 || aClientIndex >= num_socket_descriptors-1) {
               return false; // invalid index. note that clients could have change since call to getClientSummary, so caller should just check again
          }
          active_display_sockfd = socket_descriptors[aClientIndex+1].fd; // +1 as first socket is port listener
          return true;
     }

     std::string getLocalAddresses();

     static std::string nonprintableToHexadecimal(const char* str);

protected:
     static constexpr char PROTOCOL_END_OF_LINE = '\x0d';

     inline bool lockedTestRunning() {
          rgb_matrix::MutexLock l(&mutex_is_running);
          return running_;
     }

     inline void lockedActiveQueueReceivedMessage(const RawMessage& aMessage) {
          rgb_matrix::MutexLock l(&mutex_msg_queue);
          active_message_queue.push_back(aMessage);
     }

     inline void lockedStop() {
          Stop();   // public method, locks internally
     }

private:
     const int MAX_OPEN_SOCKETS = 20;

     // no lock needed, only used by this object's Run thread
     int port_number;
     int listen_for_clients_sockfd;          // entry in the socket_descriptors array for listening for new clients
     std::string tcp_unprocessed[MAX_OPEN_SOCKETS];  // empty buffers to accumulate unprocessed messages separated by newlines
     std::deque<RawMessage> inactive_message_queue[MAX_OPEN_SOCKETS]; // queue of messages received from each socket
     bool pending_active_at_next_message;  // if true, first message received will determine the active client
     std::string closingErrorMessage;   // if not empty, displayable error message to queue when stopping thread

     // If multiple locks, must ensure can not have deadlock between threads waiting for resources.
     // One way to do that is to ensure that only the Run thread can have multiple locks at once,
     // AND organized such that the Run method thread can not try to re-lock something it has already locked.
     // Public and protected methods (which can be called from other threads) can only use one lock and must release it by the end of the call.
     //
     // Otherwise, simpler to lock entire object.
     rgb_matrix::Mutex mutex_msg_queue;
     rgb_matrix::Mutex mutex_is_running;
     rgb_matrix::Mutex mutex_descriptors;

     // use MutexLock is_running to allow thread-safe read&write on this group
     bool running_;                          

     // use MutexLock msg_queue to allow thread-safe read&write on this group
     std::deque<RawMessage> active_message_queue;     

     // use MutexLock descriptors to allow thread-safe read&write on this group
     struct pollfd socket_descriptors[MAX_OPEN_SOCKETS];    
     int num_socket_descriptors;                            
     int active_display_sockfd;              // entry in the socket_descriptors array for source being displayed on the LED board

     // locks descriptors internally
     void lockedSetupInitialSocket();  // locks descriptors; may also lock running

     // locks msq_queue internally
     lockedProcessQueue(std::deque<RawMessage>& aQueue, bool isActiveSource);

     // before calling, use MutexLock descriptors to allow thread-safe read&write on this group
     void addMonitoring(int new_descriptor);
     void checkAndAcceptConnection();
     bool checkAndAppendData(int source_descriptor, std::string& unprocessed_buffer); // returns true if data found.  if not, client is disconnecting, or error.
     void closeAllSockets();
     void closeSingleSocket(int aDescriptor);     // may also lock running
     void compressSockets();

     // no lock needed, only used by this object's Run thread
     bool extractLineToQueue(std::string& aBuffer, std::deque<RawMessage>& aQueue);     
     void parseLineToQueue(const char* single_line_buffer, std::deque<RawMessage>& aQueue);
     bool parseAlgeLineToQueue(const char* single_line_buffer, std::deque<RawMessage>& aQueue);
     void queueCompletedLines(std::string& aBuffer, std::deque<RawMessage>& aQueue);
};



#endif //RECEIVER_H
