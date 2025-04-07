//
// Created by WMcD on 12/8/2024.
//

#ifndef RECEIVER_H
#define RECEIVER_H

#include "thread.h"

#include <string>
#include <vector>
#include <deque>
#include <chrono>
#include <utility>
#include <netinet/in.h>
#include <poll.h>

class Receiver : public rgb_matrix::Thread {
public:
     static constexpr int TCP_PORT_DEFAULT = 21967;

     static constexpr uint32_t PROTOCOL_MESSAGE_MAX_LENGTH = 96;   // longest valid protocol message, including end-of-line

     enum Protocol {ALGE_DLINE,    // see "Alge timing manual for D-LINE / D-SAT"
                    SIMPLE_TEXT,  // data is short string to display on board
                    UPLC_COMMAND,   // data is control messages to this LED board
                    UPLC_FORMATTED_TEXT, // data is text with formatting (font, color, scrolling)
                    UNKNOWN};

     struct RawMessage {
          Protocol protocol;
          std::string data;
          std::chrono::time_point<std::chrono::system_clock> timestamp;

          RawMessage() : protocol(UNKNOWN), data(), timestamp(std::chrono::system_clock::now()) {}
          RawMessage(const Protocol p, std::string  s)
               : protocol(p), data(std::move(s)), timestamp(std::chrono::system_clock::now()) {}
          RawMessage(const Protocol p, std::string  s, std::chrono::time_point<std::chrono::system_clock> t)
               : protocol(p), data(std::move(s)), timestamp(t) {}
          RawMessage(const RawMessage& other) = default; // copy constructor
     };

     struct ClientSummary {
          std::vector<std::string> client_names; // list of unique names (based on addresses) of clients currently connected
          std::string active_client_name; // if empty, no active client.  otherwise, an entry from client_names

          ClientSummary() : client_names(), active_client_name() {}
          ClientSummary(std::vector<std::string> aClientNameVector, std::string aActiveName) : client_names(aClientNameVector), active_client_name(aActiveName) {}
          ClientSummary(const ClientSummary& other) = default; // copy constructor
     };

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

     bool isRunning() {return lockedTestRunning();}    // locks mutex_is_running internally

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

     bool isNoActiveSourceOrPending() {
          rgb_matrix::MutexLock l(&mutex_descriptors);
          return (num_socket_descriptors < 2) || (active_display_sockfd < 0 && !pending_active_at_next_message);   // 1st descriptor is port listener
     }

     ClientSummary getClientSummary();                 // locks mutex_descriptors internally

     void setActiveClient(std::string aClientName) {      
          rgb_matrix::MutexLock l(&mutex_descriptors);     
          internalSetActiveClient(aClientName);
     }

     std::string getLocalAddresses();

     void reportDisplayed(RawMessage& aMessage) {
          // public methods must only lock one flag at a time
          {
               rgb_matrix::MutexLock l(&mutex_report_flag);
               if (!is_any_reporting_requested) {
                    return;  // no clients are requesting a report
               }
          }
          {
               rgb_matrix::MutexLock l(&mutex_descriptors);
               internalReportDisplayed(aMessage);  
          }
     }

     bool isAnyReportingRequested() {
          rgb_matrix::MutexLock l(&mutex_report_flag);
          return is_any_reporting_requested;
     }

     static std::string nonprintableToHexadecimal(const char* str);
     static void setPreferredCommandFormatTemplate(int templateIndex);

protected:
     static constexpr char PROTOCOL_END_OF_LINE = '\x0d';
     static constexpr char CARRIAGE_RETURN = '\x0D';
     static constexpr char LINE_FEED = '\x0A';

     inline static const std::string UPLC_COMMAND_PREFIX = "~)'";
     static constexpr char UPLC_COMMAND_SET_ACTIVE_CLIENT = '*';
     static constexpr char UPLC_COMMAND_SHOW_CLIENTS = '!';
     static constexpr char UPLC_COMMAND_TRANSMIT_CLIENTS = '?';
     static constexpr char UPLC_COMMAND_ECHO_MESSAGES = '&';
     static constexpr char UPLC_COMMAND_CLEAR_FOR_CURRENT_CLIENT = '0';

     inline static const std::string UPLC_TXMT_PREFIX = "~~";
     inline static const std::string UPLC_TXMT_INACTIVE_CLIENT_PREFIX = "~~";
     inline static const std::string UPLC_TXMT_ACTIVE_CLIENT_PREFIX = "~~*!";

     inline static const std::string UPLC_ECHO_PREFIX = "=";

     inline bool lockedTestRunning() {
          rgb_matrix::MutexLock l(&mutex_is_running);
          return running_;
     }

     inline void lockedAppendMessageActiveQueue(const RawMessage& aMessage);    // locks mutex_msg_queue internally

     inline void lockedStop() {
          Stop();   // public method, locks internally
     }

private:
     struct DescriptorInfo {
          // no lock needed, only used by this object's Run thread
          std::string tcp_unprocessed;  // empty buffer to accumulate unprocessed messages separated by newlines
          std::deque<RawMessage> inactive_message_queue; // queue of messages received from socket and not yet deleted nor put in active Receiver queue
          std::string source_name_unique;  // address of source, for descriptor selection lookup
          std::deque<std::string> pending_writes; // list of messages (such as command responses) to be sent to this source
          bool do_display_report; // if true, send copy of all displayed messages (at external reports, not when queued messages done internally) to this source

          DescriptorInfo() : tcp_unprocessed(), inactive_message_queue(), source_name_unique() {}
          DescriptorInfo(std::string aSourceAddressName) : tcp_unprocessed(), inactive_message_queue(), source_name_unique(std::move(aSourceAddressName)) {}
          DescriptorInfo(const DescriptorInfo& other) = default; // copy constructor
     };
     static const int MAX_OPEN_SOCKETS = 20;

     // no lock needed, only used by this object's Run thread
     int port_number;
     int listen_for_clients_sockfd;          // entry in the socket_descriptors array for listening for new clients
     std::string closingErrorMessage;   // if not empty, displayable error message to queue when stopping thread
     RawMessage active_client_last_message;  // last message received from active client (if any), used for restoring display later

     // If multiple locks, must ensure can not have deadlock between threads waiting for resources.
     // One way to do that is to ensure that only the Run thread can have multiple locks at once,
     // AND organized such that the Run method thread can not try to re-lock something it has already locked.
     // Public and protected methods (which can be called from other threads) can only use one lock and must release it by the end of the call.
     //
     // Otherwise, simpler to lock entire object.
     rgb_matrix::Mutex mutex_msg_queue;
     rgb_matrix::Mutex mutex_is_running;
     rgb_matrix::Mutex mutex_descriptors;
     rgb_matrix::Mutex mutex_report_flag;

     // use MutexLock on mutex_is_running to allow thread-safe read&write on this group
     bool running_;                          

     // use MutexLock on mutex_msg_queue to allow thread-safe read&write on this group
     std::deque<RawMessage> active_message_queue;

     // use MutexLock on mutex_descriptors to allow thread-safe read&write on this group
     bool pending_active_at_next_message;  // if true, first message received will determine the active client
     struct pollfd socket_descriptors[MAX_OPEN_SOCKETS];    // socket descriptor for each client
     DescriptorInfo descriptor_support_data[MAX_OPEN_SOCKETS];      // other information about socket connection and data for each client
     int num_socket_descriptors;                            
     int active_display_sockfd;                   // entry in the socket_descriptors array for source being displayed on the LED board
     std::string pending_active_display_name;     // requested active display source, but not yet set

     // use MutexLock on mutex_report_flag to allow thread-safe read&write on this group
     bool is_any_reporting_requested; // if true, at least one client wants a copy of all displayed messages (at external reports, not when queued messages done internally)

     // locks on mutex_msg_queue AND on mutex_descriptors internally
     void doubleLockedChangeActiveDisplay(std::string target_client_name);

     // locks on mutex_descriptors internally
     void lockedSetupInitialSocket();  // locks descriptors; may also lock running

     // locks on mutex_msg_queue internally
     void lockedProcessQueue(DescriptorInfo& aDescriptorRef, bool isActiveSource);

     // before calling, use MutexLock on mutex_descriptors to allow thread-safe read&write on this group
     void addMonitoring(int new_descriptor);
     void checkAndAcceptConnection();
     bool checkAndAppendData(int source_descriptor, std::string& unprocessed_buffer); // returns true if data found.  if not, client is disconnecting, or error.
     void closeAllSockets();
     void closeSingleSocket(int aDescriptor);     // may also lock on mutex_running
     void compressSockets();
     void processWrites();
     void internalSetActiveClient(std::string aClientName);    
     void handleUPLCCommand(const std::string& message_string, DescriptorInfo& aDescriptorRef);
     void transmitClients(DescriptorInfo& aDescriptorRef);
     void internalReportDisplayed(RawMessage& aMessage);    
     void updateIsAnyReportingRequested();        // also locks mutex_report_flag internally; call when adding client, removing client, or changing report flag for client
     void showClients();                          // also locks on mutex_msg_queue internally

     // no lock needed, only used by this object's Run thread
     bool extractLineToQueue(DescriptorInfo& aDescriptorRef);     
     void parseLineToQueue(const char* single_line_buffer, std::deque<RawMessage>& aQueue);
     bool parseAlgeLineToQueue(const char* single_line_buffer, std::deque<RawMessage>& aQueue);
     bool parseUPLCCommand(const char* single_line_buffer, std::deque<RawMessage>& aQueue);
     bool parseUPLCFormattedText(const char* single_line_buffer, std::deque<RawMessage>& aQueue);
     void queueCompletedLines(DescriptorInfo& aDescriptorRef);

     // formatting, intended to be set up once during construction
     static int preferredCommandFormatTemplateIndex;
};



#endif //RECEIVER_H
