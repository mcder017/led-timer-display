//
// Created by WMcD on 12/8/2024.
//

#include "Receiver.h"

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>  // inet_ntoa
#include <sys/types.h>
#include <ifaddrs.h>

#include <cstdio>
#include <ios>
#include <unistd.h>  // for io on linux, also option parsing; sleep
#include <strings.h>    // bzero
#include <string.h>     // strlen
#include <csignal>

#include "TextChangeOrder.h"

static auto LED_ERROR_MESSAGE_SOCKET = "DISP(S)";
static auto LED_ERROR_MESSAGE_BIND = "DISP(B)";
static auto LED_ERROR_MESSAGE_LISTEN = "DISP(L)";
static auto LED_ERROR_MESSAGE_SOCKET_OPTIONS = "DISP(O)";
static auto LED_ERROR_MESSAGE_NONBLOCKING = "DISP(NB)";
static auto LED_ERROR_MESSAGE_POLL = "DISP(P)";
static auto LED_ERROR_MESSAGE_ACCEPT = "DISP(A)";
static auto LED_ERROR_MESSAGE_FAIL_EVENT = "DISP(F)";

static auto CLEAR_DISPLAY_ON_UNRECOGNIZED_MESSAGE = true; 

int Receiver::preferredCommandFormatTemplateIndex = 0; // default to first template, if any

Receiver::Receiver(int aPort_number)  : port_number(aPort_number), listen_for_clients_sockfd(-1), 
                                        closingErrorMessage(""), 
                                        running_(false), pending_active_at_next_message(true), 
                                        num_socket_descriptors(0),
                                        active_display_sockfd(-1), pending_active_display_name("")                                        
                                         {
    bzero((struct pollfd *) &socket_descriptors, sizeof(socket_descriptors));                                        

    // descriptor_support_data defaults to empty content, not initialized here
}

Receiver::Receiver() : Receiver(TCP_PORT_DEFAULT) {}    // forward to other constructor

Receiver::~Receiver() {
    lockedStop();
}

void Receiver::Start() {
    {
      rgb_matrix::MutexLock l(&mutex_is_running);
      running_ = true;
    }
    // avoid core 3 (prefer core 0,1,2) so not on core with RGBMatrix
    Thread::Start(0,(1<<2) | (1<<1) | (1<<0));
}

std::string Receiver::getLocalAddresses() {
    std::string accum_addresses;    // start empty

    struct ifaddrs * ifAddrStruct=nullptr;
    struct ifaddrs * ifa=nullptr;
    void * tmpAddrPtr=nullptr;

    getifaddrs(&ifAddrStruct);

    for (ifa = ifAddrStruct; ifa != nullptr; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) {
            continue;
        }
        if (ifa->ifa_addr->sa_family == AF_INET) { // check it is IP4
            // is a valid IP4 Address
            tmpAddrPtr=&((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
            char addressBuffer[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, tmpAddrPtr, addressBuffer, INET_ADDRSTRLEN);
            if (isatty(STDIN_FILENO)) {
                // Only give a message if we are interactive. If connected via pipe, be quiet
                printf("%s IP4 Address %s\n", ifa->ifa_name, addressBuffer);
            }
            accum_addresses += addressBuffer;
            accum_addresses += "   ";
        } else if (ifa->ifa_addr->sa_family == AF_INET6) { // check it is IP6
            // is a valid IP6 Address
            tmpAddrPtr=&((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr;
            char addressBuffer[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6, tmpAddrPtr, addressBuffer, INET6_ADDRSTRLEN);
            if (isatty(STDIN_FILENO)) {
                // Only give a message if we are interactive. If connected via pipe, be quiet
                printf("%s IP6 Address %s\n", ifa->ifa_name, addressBuffer);
            }
            accum_addresses += addressBuffer;
            accum_addresses += "   ";
        }
    }
    if (ifAddrStruct!=nullptr) freeifaddrs(ifAddrStruct);
    return accum_addresses;
}

void Receiver::lockedSetupInitialSocket() {    
    rgb_matrix::MutexLock l(&mutex_descriptors);
    
    if (isatty(STDIN_FILENO)) {
        // Only give a message if we are interactive. If connected via pipe, be quiet
        printf("Setting up listening-for-clients socket...\n");
    }

    //TODO support UDP port messaging, e.g. from SplitSecondTiming software

    // create stream socket to receive incoming connections
    listen_for_clients_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_for_clients_sockfd < 0) {
        fprintf(stderr, "socket() failed\n");
        closingErrorMessage = LED_ERROR_MESSAGE_SOCKET;
        lockedStop(); // open sockets will be closed in Run()
        return;
    }
    // set socket and port to be reusable
    const int enable = 1;
    if (setsockopt(listen_for_clients_sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0 ||
        setsockopt(listen_for_clients_sockfd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(int)) < 0) {
        fprintf(stderr, "setsockopt() failed\n");
        closingErrorMessage = LED_ERROR_MESSAGE_SOCKET_OPTIONS;
        lockedStop(); // open sockets will be closed in Run()
        return;
    }
    // set socket to be non-blocking.  All of the sockets for the incoming connections will also be non-blocking since they will inherit that state from the listening socket.
    if (ioctl(listen_for_clients_sockfd, FIONBIO, &enable) < 0) {
        fprintf(stderr, "ioctl() failed\n");
        closingErrorMessage = LED_ERROR_MESSAGE_NONBLOCKING;
        lockedStop(); // open sockets will be closed in Run()
        return;
    }
    // bind the socket to the port number
    struct sockaddr_in serv_addr;
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port_number);
    if (bind(listen_for_clients_sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        fprintf(stderr, "bind(port %d) failed, errno=%d\n", port_number, errno);
        closingErrorMessage = LED_ERROR_MESSAGE_BIND;
        lockedStop(); // open sockets will be closed in Run()
        return;
    }
    // set the listen backlog size
    const int MAX_PENDING_CONNECTION = 10;
    if (listen(listen_for_clients_sockfd, MAX_PENDING_CONNECTION) < 0) {  // mark socket as passive (listener)
        fprintf(stderr, "listen(port %d, max %d) failed, errno=%d\n", port_number, MAX_PENDING_CONNECTION, errno);
        closingErrorMessage = LED_ERROR_MESSAGE_LISTEN;
        lockedStop(); // open sockets will be closed in Run()
        return;
    }
    // (re-)initialize the listening structure
    bzero((struct pollfd *) &socket_descriptors, sizeof(socket_descriptors));
    num_socket_descriptors = 0;
    // add the initial listening socket into the listening structure
    addMonitoring(listen_for_clients_sockfd);

    if (isatty(STDIN_FILENO)) {
        // Only give a message if we are interactive. If connected via pipe, be quiet
        printf("Listening for clients on port %d...\n", port_number);
    }

}

void Receiver::addMonitoring(int new_descriptor) {
    if (num_socket_descriptors >= MAX_OPEN_SOCKETS) {
        fprintf(stderr, "Too many open sockets to add another (%d)\n", num_socket_descriptors);
        lockedStop(); // open sockets will be closed in Run()
        return;
    }
    socket_descriptors[num_socket_descriptors].fd = listen_for_clients_sockfd;
    socket_descriptors[num_socket_descriptors].events = POLLIN;
    num_socket_descriptors++;
}

void Receiver::checkAndAcceptConnection() {
    std::string uniqueNameExtension = "*";

    struct sockaddr_in cli_addr;
    socklen_t clilen = sizeof(cli_addr);
   
    int new_socket_descriptor = -1;
    do {    // accept all pending connections
        bzero((char *) &cli_addr, sizeof(cli_addr));    // might be unnecessary
        new_socket_descriptor = accept(listen_for_clients_sockfd, (struct sockaddr *) &cli_addr, &clilen);

        if (new_socket_descriptor < 0) {
            if (errno != EWOULDBLOCK) {
                fprintf(stderr, "accept() failed, errno=%d\n", errno);
                closingErrorMessage = LED_ERROR_MESSAGE_ACCEPT;
                lockedStop(); // open sockets will be closed in Run()
            }
            // no more connections to accept, ready to exit loop
        }
        else {  // new connection ready 
            // append new socket descriptor to array
            socket_descriptors[num_socket_descriptors].fd = new_socket_descriptor;
            socket_descriptors[num_socket_descriptors].events = POLLIN;

            descriptor_support_data[num_socket_descriptors] = DescriptorInfo();  // overwrite to start from default constructor (belt and suspenders)
            descriptor_support_data[num_socket_descriptors].tcp_unprocessed = "";  // empty buffer to accumulate unprocessed messages separated by newlines
            descriptor_support_data[num_socket_descriptors].inactive_message_queue.clear();  // empty queue
            descriptor_support_data[num_socket_descriptors].source_name_unique = (cli_addr.sin_family == AF_INET ? inet_ntoa(cli_addr.sin_addr) : "(non-IPV4)");
            descriptor_support_data[num_socket_descriptors].pending_writes.clear(); // empty queue of messages to be sent to this source
            descriptor_support_data[num_socket_descriptors].do_display_report = false; // default to no report requested

            // ensure source address name is unique
            bool found_duplicate;
            do {
                found_duplicate = false;
                for (int i=0; i < num_socket_descriptors; i++) {    // upper bound has NOT yet been incremented
                    if (i != num_socket_descriptors && descriptor_support_data[i].source_name_unique == descriptor_support_data[num_socket_descriptors].source_name_unique) {
                        found_duplicate = true;

                        // add unique name extension to the end of the address name
                        descriptor_support_data[num_socket_descriptors].source_name_unique.append(uniqueNameExtension);
                        break;  // found duplicate, so break out of for loop, and then repeat do loop
                    }
                }
            } while (found_duplicate);

            num_socket_descriptors++;

            if (isatty(STDIN_FILENO)) {
                // Only give a message if we are interactive. If connected via pipe, be quiet
                printf("Connected to: %s\n",(cli_addr.sin_family == AF_INET ? inet_ntoa(cli_addr.sin_addr) : "(non-IPV4)"));
                printf("Now %d clients connected.\n", num_socket_descriptors-1);  // -1 as port listener is not a client
            }
        }

    } while (new_socket_descriptor >= 0);
    updateIsAnyReportingRequested();
}

bool Receiver::checkAndAppendData(int source_descriptor, std::string& unprocessed_buffer) {
    char socket_buffer[PROTOCOL_MESSAGE_MAX_LENGTH+1];     // include room for end-of-string null

    // keep reading data until none available on this source
    do {
        const int result_flag = recv(source_descriptor, socket_buffer, PROTOCOL_MESSAGE_MAX_LENGTH, MSG_DONTWAIT);

        if (result_flag > 0) {        
            // result_flag is now known to be the number of bytes read
            socket_buffer[result_flag] = '\0';  // ensure end-of-string safely added (buffer has one extra size element)

            if (isatty(STDIN_FILENO)) {
                // Only give a message if we are interactive. If connected via pipe, be quiet
                printf("%s%s Rcvd(len=%d)\n", (pending_active_at_next_message ? "(source pending) " : ""), (source_descriptor==active_display_sockfd ? "Active source: " : "Inactive: "), result_flag);
            }

            // accumulate. caller can handle partial message, or more than one protocol message, in buffer string
            unprocessed_buffer.append(socket_buffer, result_flag);
        }
        else if (result_flag == 0) {   // client indicates end of connection
            if (isatty(STDIN_FILENO)) {
                // Only give a message if we are interactive. If connected via pipe, be quiet
                printf("Client signalled they are disconnecting gracefully\n");
            }
            
            return false;  // signal to close connection
        }
        else {  // result_flag < 0
            if (errno == EWOULDBLOCK) {
                return true;  // no more data to process, but connection still open
            }

            // error detected
            if (isatty(STDIN_FILENO)) {
                // Only give a message if we are interactive. If connected via pipe, be quiet
                fprintf(stderr, "recv error %d, preparing to close connection\n", result_flag);
            }
            
            return false;  // signal to close connection
        }
    } while (true);
}

bool Receiver::extractLineToQueue(DescriptorInfo& aDescriptorRef) {
    std::string::size_type eol_pos = 0;
    eol_pos = aDescriptorRef.tcp_unprocessed.find_first_of(PROTOCOL_END_OF_LINE);
    const bool foundLine = eol_pos != std::string::npos;
    if (foundLine) {
        // found a line
        if (eol_pos >= PROTOCOL_MESSAGE_MAX_LENGTH) {
            fprintf(stderr, "Line too long(%lu > %d) in buffer:%s\n",
                static_cast<unsigned long>(eol_pos), PROTOCOL_MESSAGE_MAX_LENGTH,
                nonprintableToHexadecimal(aDescriptorRef.tcp_unprocessed.c_str()).c_str());
            eol_pos = PROTOCOL_MESSAGE_MAX_LENGTH - 2;
        }
        const unsigned int char_in_line = eol_pos+1;  // less than TCP_BUFFER_SIZE since eol_pos max TCP_BUFFER_SIZE-2;

        // copy char from beginning of string, incl eol, and then
        // remove the message characters from the accumulation string
        char single_line_buffer[PROTOCOL_MESSAGE_MAX_LENGTH];
        aDescriptorRef.tcp_unprocessed.copy(single_line_buffer, char_in_line, 0);
        aDescriptorRef.tcp_unprocessed.erase(0, char_in_line);
        // place end-of-string character; index is safe since eol_pos max TCP_BUFFER_SIZE-2, above
        single_line_buffer[char_in_line] = '\0';

        if (isatty(STDIN_FILENO)) {
            // Only give a message if we are interactive. If connected via pipe, be quiet
            printf("%s: Extracted line length %3lu (leaving %3lu): %s\n",
                    aDescriptorRef.source_name_unique.c_str(),
                    static_cast<unsigned long>(strlen(single_line_buffer)),
                    static_cast<unsigned long>(strlen(aDescriptorRef.tcp_unprocessed.c_str())),
                    nonprintableToHexadecimal(single_line_buffer).c_str()
                   );
        }

        parseLineToQueue(single_line_buffer, aDescriptorRef.inactive_message_queue);
    }

    return foundLine;
}

void Receiver::parseLineToQueue(const char* single_line_buffer, std::deque<RawMessage>& aQueue) {
    
    if (!parseUPLCCommand(single_line_buffer, aQueue)
        && !parseUPLCFormattedText(single_line_buffer, aQueue)
        && !parseAlgeLineToQueue(single_line_buffer, aQueue)) {    // if parse is true, a message has already been pushed into queue

        bool doClear = CLEAR_DISPLAY_ON_UNRECOGNIZED_MESSAGE;
        fprintf(stderr, "Discarding unrecognized message%s:%s\n",
            doClear ? " (and clear display)" : "",
            nonprintableToHexadecimal(single_line_buffer).c_str());

        if (doClear) {
            aQueue.push_back(RawMessage(SIMPLE_TEXT, ""));
        }
    }
}

bool Receiver::parseUPLCCommand(const char* single_line_buffer, std::deque<RawMessage>& aQueue) {
    if (strlen(single_line_buffer) <= UPLC_COMMAND_PREFIX.length()) {
        return false;  // not a UPLC command
    }
    if (single_line_buffer[strlen(single_line_buffer)-1] != PROTOCOL_END_OF_LINE) {
        return false;  // not a UPLC command
    }

    const std::string msg(single_line_buffer);
    if (msg.substr(0, UPLC_COMMAND_PREFIX.length()) != UPLC_COMMAND_PREFIX) {
        return false;  // not a UPLC command
    }
    const std::string msg_post_prefix_non_eol = msg.substr(UPLC_COMMAND_PREFIX.length(), msg.length()-UPLC_COMMAND_PREFIX.length()-1); // remove prefix and end-of-line character
    if (msg_post_prefix_non_eol.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890 ~!@#$%^&*()_+`-={}[]|:;\"'<>?,./\\") != std::string::npos) {
        return false;  // not a UPLC command
    }

    // appears to be valid, queue for processing
    aQueue.push_back(RawMessage(UPLC_COMMAND, single_line_buffer));

    return true;    
}

bool Receiver::parseUPLCFormattedText(const char* single_line_buffer, std::deque<RawMessage>& aQueue) {
    if (strlen(single_line_buffer) < TextChangeOrder::UPLC_FORMATTED_PREFIX.length()+1) {
        return false;  // not UPLC formatted text
    }

    const std::string msg(single_line_buffer);
    if (msg.substr(0, TextChangeOrder::UPLC_FORMATTED_PREFIX.length()) != TextChangeOrder::UPLC_FORMATTED_PREFIX) {
        return false;  // not a UPLC formatted text
    }
    if (msg.substr(msg.length()-TextChangeOrder::UPLC_FORMATTED_SUFFIX.length(), TextChangeOrder::UPLC_FORMATTED_SUFFIX.length()) != TextChangeOrder::UPLC_FORMATTED_SUFFIX) {
        return false;  // not a UPLC formatted text
    }
    const std::string msg_post_prefix_non_eol = msg.substr(TextChangeOrder::UPLC_FORMATTED_PREFIX.length(), msg.length()-TextChangeOrder::UPLC_FORMATTED_PREFIX.length()-TextChangeOrder::UPLC_FORMATTED_SUFFIX.length()); // remove prefix and end-of-line suffix
    if (msg_post_prefix_non_eol.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890 ~!@#$%^&*()_+`-={}[]|:;\"'<>?,./\\") != std::string::npos) {
        return false;  // not UPLC formatted text
    }

    // appears to be valid, queue for processing
    aQueue.push_back(RawMessage(UPLC_FORMATTED_TEXT, single_line_buffer));

    return true;    
}

bool Receiver::parseAlgeLineToQueue(const char* single_line_buffer, std::deque<RawMessage>& aQueue) {

    const unsigned int char_in_line = strlen(single_line_buffer);
    bool possible_alge_message = true;

    // end of line can either be 0A 0D (lf cr, backwards from most customer cr lf), or just 0D (cr)
    const unsigned int data_chars_excluding_eol = char_in_line < 2
                ? 0 :
                  (single_line_buffer[char_in_line-2] == LINE_FEED ? char_in_line-2 : char_in_line-1);

    if ((data_chars_excluding_eol < 19 || data_chars_excluding_eol > 23)
        || single_line_buffer[char_in_line-1] != PROTOCOL_END_OF_LINE) {
        possible_alge_message = false;
    }
    else {
        // possible ALGE protocol message
        const std::string msg(single_line_buffer);
        const std::string msg_non_eol = msg.substr(0, data_chars_excluding_eol);
        if (msg_non_eol.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890.: \x01\x02\x03") != std::string::npos) {
            possible_alge_message = false;
        }
        else {
            // must have a space in specific locations
            constexpr size_t KNOWN_SPACE_POS1 = 5;  // protocol index 6 is string index 5
            constexpr size_t KNOWN_SPACE_POS2 = 6;  // protocol index 7 is string index 6
            if (single_line_buffer[KNOWN_SPACE_POS1] != ' ' || single_line_buffer[KNOWN_SPACE_POS2] != ' ') {
                possible_alge_message = false;
            }

            // the hex 01,02,03 are only allowed at one location
            constexpr size_t SPEED_ID_POS = 7;    // protocol index 8 is string index 7
            size_t hexPos = msg_non_eol.find_first_of("\x01\x02\x03");
            if (hexPos != std::string::npos) {
                if (hexPos != SPEED_ID_POS) {
                    possible_alge_message = false;
                }
                else if (msg_non_eol.find_first_of("\x01\x02\x03", SPEED_ID_POS+1) != std::string::npos) {
                    // those characters only allowed in the one location
                    possible_alge_message = false;
                }
            }

            // dot can be either in one of two time-related positions,
            // or in one of two fixed positions as indicator this is a "running" message
            size_t dotPos = msg_non_eol.find_last_of('.');
            if (dotPos != std::string::npos) {
                constexpr size_t RUNNING_FLAG_POS1 = 3; // protocol index 4 is string index 3
                constexpr size_t RUNNING_FLAG_POS2 = 4; // protocol index 5 is string index 4

                constexpr size_t RUNNING_FLAG_POS3 = 16; // protocol index 17 is string index 16
                constexpr size_t RUNNING_FLAG_POS4 = 17; // protocol index 18 is string index 17

                if (dotPos != RUNNING_FLAG_POS1
                    && dotPos != RUNNING_FLAG_POS2
                    && dotPos != RUNNING_FLAG_POS3
                    && dotPos != RUNNING_FLAG_POS4) {

                    possible_alge_message = false;
                }
            }
        }
    }

    if (possible_alge_message) {
        // if appears to be valid, queue for processing by other classes
        aQueue.push_back(RawMessage(ALGE_DLINE, single_line_buffer));
    }

    return possible_alge_message;
}

void Receiver::doubleLockedChangeActiveDisplay(std::string target_client_name) {

    rgb_matrix::MutexLock l1(&mutex_msg_queue);
    rgb_matrix::MutexLock l2(&mutex_descriptors);
    
    if (target_client_name.length() > 0) {

        int old_active_index = -1;
        if (active_display_sockfd >= 0) {
            for (int i = 0; i < num_socket_descriptors; i++) {
                if (socket_descriptors[i].fd == active_display_sockfd) {
                    old_active_index = i;
                    break;
                }
            }
        }

        int new_active_index = -1;
        for (int i = 0; i < num_socket_descriptors; i++) {
            if (descriptor_support_data[i].source_name_unique == target_client_name) {
                new_active_index = i;
                break;
            }
        }

        if (new_active_index < 0) {
            if (isatty(STDIN_FILENO)) {
                // Only give a message if we are interactive. If connected via pipe, be quiet
                printf("Changing active display source requested but descriptor no longer found, disregarding: %s\n", target_client_name.c_str());
            }                    
        }
        else {
            if (isatty(STDIN_FILENO)) {
                // Only give a message if we are interactive. If connected via pipe, be quiet
                printf("Changing active display source to %s, internal array index %d to %d\n", target_client_name.c_str(), old_active_index, new_active_index);
            }                                

            // move any active queue to old source inactive status
            if (old_active_index >= 0) {    // avoid segmentation fault... there might not be a previous active index
                if (active_message_queue.size() > 0) {
                    if (isatty(STDIN_FILENO)) {
                        // Only give a message if we are interactive. If connected via pipe, be quiet
                        printf("De-queueing %ld old active messages...\n", active_message_queue.size());
                    }                    

                    while (active_message_queue.size() > 0) {
                        descriptor_support_data[old_active_index].inactive_message_queue.push_back(active_message_queue.front());
                        active_message_queue.pop_front();
                    }
                }
                else {
                    // no pending messages, so store the last (currently displayed) message
                    if (isatty(STDIN_FILENO)) {
                        // Only give a message if we are interactive. If connected via pipe, be quiet
                        printf("No messages pending for old source, storing last message...\n");
                    }
                    descriptor_support_data[old_active_index].inactive_message_queue.push_back(active_client_last_message);
                }
            }

            // whenever we change source, we (at least momentarily) clear the display
            // for example so that downstream code (Formatter) does not discard messages as duplicates
            active_message_queue.push_back(RawMessage(SIMPLE_TEXT, ""));  // clear display

            // move any new source inactive queue to active status
            if (descriptor_support_data[new_active_index].inactive_message_queue.size() > 0) {
                if (isatty(STDIN_FILENO)) {
                    // Only give a message if we are interactive. If connected via pipe, be quiet
                    printf("Queueing %ld new active messages...\n", descriptor_support_data[new_active_index].inactive_message_queue.size());
                }                    

                while (descriptor_support_data[new_active_index].inactive_message_queue.size() > 0) {
                    active_message_queue.push_back(descriptor_support_data[new_active_index].inactive_message_queue.front());
                    active_client_last_message = descriptor_support_data[new_active_index].inactive_message_queue.front();  // store last message for this source
                    descriptor_support_data[new_active_index].inactive_message_queue.pop_front();
                }
            }

            // update socket reference
            active_display_sockfd = socket_descriptors[new_active_index].fd;  // set active display to this source
        }
    }
    else {
        if (isatty(STDIN_FILENO)) {
            // Only give a message if we are interactive. If connected via pipe, be quiet
            printf("Changing active source requested but id is empty; disregarding.\n");
        }                    
    }
}

void Receiver::lockedAppendMessageActiveQueue(const RawMessage& aMessage) {
    rgb_matrix::MutexLock l(&mutex_msg_queue);
    active_message_queue.push_back(aMessage);
    if (isatty(STDIN_FILENO)) {
         // Only give a message if we are interactive. If connected via pipe, be quiet
         if (active_message_queue.size() > 1) {
              printf("Active queue now %ld\n", active_message_queue.size());
         }
    }
}

void Receiver::internalReportDisplayed(const RawMessage& aMessage) {
    std::string report_message = UPLC_ECHO_PREFIX + aMessage.data;  
    if (report_message.at(report_message.length()-1) != PROTOCOL_END_OF_LINE) {
        report_message += PROTOCOL_END_OF_LINE;  // add end-of-line character to message if needed (SIMPLE_TEXT protocol in particular)
    }

    for (int i = 0; i < num_socket_descriptors; i++) {
         if (socket_descriptors[i].fd == listen_for_clients_sockfd) {
              continue;  // skip port listener
         }
         if (descriptor_support_data[i].do_display_report) {          
            descriptor_support_data[i].pending_writes.push_back(report_message);  
         }
    }
}

void Receiver::updateIsAnyReportingRequested() {
    int report_count = 0;

    // check if any of the descriptors are requesting a report
    for (int i = 0; i < num_socket_descriptors; i++) {
        if (socket_descriptors[i].fd == listen_for_clients_sockfd) {
            continue;  // skip port listener
        }
        else if (descriptor_support_data[i].do_display_report) {          
            report_count++;
            //break;
        }
    }

    rgb_matrix::MutexLock l(&mutex_report_flag);
    is_any_reporting_requested = report_count != 0;
    if (isatty(STDIN_FILENO)) {
        // Only give a message if we are interactive. If connected via pipe, be quiet
        printf("Reporting set for %d clients\n", report_count);        
   }
}

void Receiver::Run() {
    signal(SIGPIPE, SIG_IGN);  // ignore SIGPIPE signal (if write to a stream whose reading end closed), so that we can handle closed connections gracefully

    const short FLAG_POLLIN = POLLIN;
    const short FLAG_SINGLE_CLOSE = POLLPRI | POLLRDHUP | POLLHUP;  // flags for which we will close single connection
    const short FLAG_DO_STOP = ~(FLAG_POLLIN | FLAG_SINGLE_CLOSE);  // any other flag treated as error for which we will stop the receiver completely

    lockedSetupInitialSocket(); // may ALSO lock running internally

    while (lockedTestRunning()) {
        // check if requested to change active display (and its queue)
        if (pending_active_display_name.length() > 0) {
            doubleLockedChangeActiveDisplay(pending_active_display_name);          // locks msg_queue AND descriptors
            pending_active_display_name = "";  // clear pending display source
        }

        // check for pending connections and data
        int result;
        {   // encapsulate lock
            rgb_matrix::MutexLock l(&mutex_descriptors);
            result = poll(socket_descriptors, num_socket_descriptors, 0); // no wait
        }        

        // handle all pending connections, data, and errors
        if (result < 0) {
            fprintf(stderr, "poll() failed, errno=%d\n", errno);
            closingErrorMessage = LED_ERROR_MESSAGE_POLL;
            lockedStop(); // open sockets will be closed at end of Run()            
        }
        else if (result == 0) {
            // no data to process
            // short sleep
            usleep(15 * 1000);
        }
        else if (result > 0) {
            {   // encapsulate lock on descriptors
                rgb_matrix::MutexLock l(&mutex_descriptors);

                bool needs_compress = false;   // flag to compress the array content

                // one or more descriptors are readable.  traverse array and handle all requests
                const int snapshot_num_descriptors = num_socket_descriptors;  // store current size for search, as entries may be appended during loop
                for (int i = 0; result != 0 && i < snapshot_num_descriptors; i++) {
                    if (socket_descriptors[i].revents == 0) {
                        continue;   // no events on this descriptor
                    }

                    if ((socket_descriptors[i].revents & FLAG_POLLIN) != 0) {                    
                        if (socket_descriptors[i].fd == listen_for_clients_sockfd) {
                            // new connection to accept
                            checkAndAcceptConnection(); // appends to array
                        }
                        else {
                            // data on existing connection
                            const bool reading_ok = checkAndAppendData(socket_descriptors[i].fd, descriptor_support_data[i].tcp_unprocessed);
                            if (reading_ok) {
                                queueCompletedLines(descriptor_support_data[i]);

                                if (pending_active_at_next_message 
                                    && descriptor_support_data[i].inactive_message_queue.size() > 0
                                    && (descriptor_support_data[i].inactive_message_queue.front().protocol != UPLC_COMMAND
                                        || descriptor_support_data[i].inactive_message_queue.back().protocol != UPLC_COMMAND)) {

                                    if (isatty(STDIN_FILENO)) {
                                        // Only give a message if we are interactive. If connected via pipe, be quiet
                                        printf("Assigning active display by first displayable message, internal index %d\n", i);
                                    }                    
                                    active_display_sockfd = socket_descriptors[i].fd;  // set active display to this source
                                    pending_active_at_next_message = false;  // only set active display once, at first message; not automatically at every disconnect of active display
                                }

                                // trim if inactive queue, or move inactive entries to active queue
                                const bool isActiveDisplayBuffer = (socket_descriptors[i].fd == active_display_sockfd);
                                lockedProcessQueue(descriptor_support_data[i], isActiveDisplayBuffer); 
                            }
                            else {  
                                // received signal to close connection
                                const bool isActiveDisplay = socket_descriptors[i].fd == active_display_sockfd;
                                const bool isMainListen = socket_descriptors[i].fd == listen_for_clients_sockfd;

                                if (isatty(STDIN_FILENO)) {
                                    // Only give a message if we are interactive. If connected via pipe, be quiet
                                    printf("Closing single connection gracefully, index %d, %s, %s\n", i, (isActiveDisplay ? "active display" : "not active display"), (isMainListen ? "port listener" : "not port listener"));
                                }                    

                                closeSingleSocket(socket_descriptors[i].fd);
                                needs_compress = true;
                            }
                        }
                        result--;
                    }
                    else {  // non-zero and not POLLIN
                        // close single connection

                        const bool isActiveDisplay = socket_descriptors[i].fd == active_display_sockfd;
                        const bool isMainListen = socket_descriptors[i].fd == listen_for_clients_sockfd;
                    
                        if ((socket_descriptors[i].revents & FLAG_SINGLE_CLOSE) != 0) {                    
                            if (isatty(STDIN_FILENO)) {
                                // Only give a message if we are interactive. If connected via pipe, be quiet
                                printf("Closing single connection gracefully, index %d, %s, %s\n", i, (isActiveDisplay ? "active display" : "not active display"), (isMainListen ? "port listener" : "not port listener"));
                            }                    
                        }
                        if ((socket_descriptors[i].revents & FLAG_DO_STOP) != 0) {
                            fprintf(stderr, "Unexpected poll() event %d, force-closing single connection, index %d, %s, %s\n", socket_descriptors[i].revents, i, (isActiveDisplay ? "active display" : "not active display"), (isMainListen ? "port listener" : "not port listener"));
                        }
                    
                        closeSingleSocket(socket_descriptors[i].fd);
                        needs_compress = true;

                        result--;
                    }
                }

                if (needs_compress) {
                    compressSockets();  // remove closed sockets from array
                    needs_compress = false;
                }
            }
        }

        {   // encapsulate lock on descriptors
            // note that writes can be due to reporting OR due to specific commands (so we search for writes even if all reporting is off)

            rgb_matrix::MutexLock l(&mutex_descriptors);

            // now look for any socket writes that have been requested on remaining connections, and send them
            processWrites();
        }
    }
    // TODO main loop could be wrapped in try/catch

    if (closingErrorMessage != "") {
        // if error message to display, queue it
        lockedAppendMessageActiveQueue(RawMessage(SIMPLE_TEXT, closingErrorMessage));
        closingErrorMessage = "";   
    }

    {   // encapsulate lock
        // close sockets
        rgb_matrix::MutexLock l(&mutex_descriptors);   // lock before changing value, in case being read externally
        closeAllSockets();
    }

    if (isatty(STDIN_FILENO)) {
        // Only give a message if we are interactive. If connected via pipe, be quiet
        printf("Sockets closed, ending Receiver.\n");
    }
}

void Receiver::processWrites() {
    for (int wIndex = 0; wIndex < num_socket_descriptors; wIndex++) {
        if (socket_descriptors[wIndex].fd == listen_for_clients_sockfd) {
            continue;  // skip the listening socket
        }

        // attempt to send all pending writes to this socket, but discard if any errors occur
        while (descriptor_support_data[wIndex].pending_writes.size() > 0) {
            const int result_flag = send(socket_descriptors[wIndex].fd, descriptor_support_data[wIndex].pending_writes.front().c_str(), descriptor_support_data[wIndex].pending_writes.front().length(), MSG_DONTWAIT);
            if (result_flag < 0) {
                fprintf(stderr, "send() failed for %s, errno=%d\n", descriptor_support_data[wIndex].source_name_unique.c_str(), errno);
                descriptor_support_data[wIndex].pending_writes.clear();  // clear the pending write buffer
            }
            else {
                if (isatty(STDIN_FILENO)) {
                    // Only give a message if we are interactive. If connected via pipe, be quiet
                    printf("Sent to %s: %s\n", 
                        descriptor_support_data[wIndex].source_name_unique.c_str(), 
                        nonprintableToHexadecimal(descriptor_support_data[wIndex].pending_writes.front().c_str()).c_str());
                }    
                descriptor_support_data[wIndex].pending_writes.pop_front();                
            }
        }
    }
}

void Receiver::lockedProcessQueue(DescriptorInfo& aDescriptorRef, bool isActiveSource) {
    if (!isActiveSource) {
        // process any command messages now, and erase them from the inactive queue
        for (auto iter = aDescriptorRef.inactive_message_queue.begin(); iter != aDescriptorRef.inactive_message_queue.end() ; /*NOTE: no incrementation of the iterator here*/) {
            if ((*iter).protocol == UPLC_COMMAND) {
                if (isatty(STDIN_FILENO)) {
                    // Only give a message if we are interactive. If connected via pipe, be quiet
                    printf("Handling command from client (not active display)\n");
                }

                // handle UPLC_COMMAND message here.  do not add to active queue for display
                handleUPLCCommand((*iter).data, aDescriptorRef);

                iter = aDescriptorRef.inactive_message_queue.erase(iter); // erase returns the next iterator
            }
            else {
                ++iter; // otherwise increment it manually
            }
        }

        // for sources that are not being displayed,
        // shrink queue to only retain most recent displayable message
        // which is useful when the active client is switched to this source
        while (aDescriptorRef.inactive_message_queue.size() > 1) {    
            aDescriptorRef.inactive_message_queue.pop_front();
        }
    }
    else {
        // move any queued messages for this client to active queue, intercepting any UPLC_COMMAND messages to be handled here
        while (aDescriptorRef.inactive_message_queue.size() > 0) {
            if (aDescriptorRef.inactive_message_queue.front().protocol == UPLC_COMMAND) {
                if (isatty(STDIN_FILENO)) {
                    // Only give a message if we are interactive. If connected via pipe, be quiet
                    printf("Handling command from client (active display)\n");
                }

                // handle UPLC_COMMAND message here.  do not add to active queue for display
                handleUPLCCommand(aDescriptorRef.inactive_message_queue.front().data, aDescriptorRef);
            }
            else {
                // copy to active queue
                lockedAppendMessageActiveQueue(aDescriptorRef.inactive_message_queue.front());

                // always keep copy of last displayable (non-command) message from the active client
                // for use in storing when the active client is switched, and later switched back
                active_client_last_message = aDescriptorRef.inactive_message_queue.front();
            }
            // remove from inactive queue
            aDescriptorRef.inactive_message_queue.pop_front();
        }
    }
}

void Receiver::handleUPLCCommand(const std::string& message_string, DescriptorInfo& aDescriptorRef) {
    if (isatty(STDIN_FILENO)) {
        // Only give a message if we are interactive. If connected via pipe, be quiet
        printf("Received UPLC command: %s\n", nonprintableToHexadecimal(message_string.c_str()).c_str());
    }                    

    if (message_string.length() < UPLC_COMMAND_PREFIX.length()+1) {
        fprintf(stderr, "UPLC command requested but prefix %s not found:%s\n",
            UPLC_COMMAND_PREFIX.c_str(), nonprintableToHexadecimal(message_string.c_str()).c_str());
        return;  // not a UPLC command
    }

    switch(message_string.at(UPLC_COMMAND_PREFIX.length())) { 
        case UPLC_COMMAND_SET_ACTIVE_CLIENT:
            internalSetActiveClient(message_string.substr(UPLC_COMMAND_PREFIX.length()+1, message_string.length()-UPLC_COMMAND_PREFIX.length()-1-1));  // +1 to skip command char, -1 to skip end-of-line char
            break;

        case UPLC_COMMAND_SHOW_CLIENTS:
            showClients();
            break;

        case UPLC_COMMAND_CLEAR_FOR_CURRENT_CLIENT:
            {   // scope for local variables
                RawMessage clearMessage(SIMPLE_TEXT, "");

                // copy to active queue
                lockedAppendMessageActiveQueue(clearMessage);

                // always keep copy of last displayable (non-command) message from the active client
                // for use in storing when the active client is switched, and later switched back
                active_client_last_message = clearMessage;
            }
            break;

        case UPLC_COMMAND_TRANSMIT_CLIENTS:
            transmitClients(aDescriptorRef);
            break;

        case UPLC_COMMAND_ECHO_MESSAGES:
            // set flags here.  Then in led-timer-display, call back to Receiver to new method with (simple text, or formatted) message when order created
            if (message_string.length() > UPLC_COMMAND_PREFIX.length()+1) {
                const std::string echo_message = message_string.substr(UPLC_COMMAND_PREFIX.length()+1, 1);  // +1 to skip command char
                aDescriptorRef.do_display_report = echo_message.at(0) == '1';

                if (aDescriptorRef.do_display_report) {
                    // ensure initial report coming                
                    std::string signup_message = UPLC_ECHO_PREFIX + reported_displayed_last_message.data;
                    if (signup_message.at(signup_message.length()-1) != PROTOCOL_END_OF_LINE) {
                        signup_message += PROTOCOL_END_OF_LINE;  // add end-of-line character to message if needed (SIMPLE_TEXT protocol in particular)
                    }
                    aDescriptorRef.pending_writes.push_back(signup_message);
                }

                updateIsAnyReportingRequested();

                if (isatty(STDIN_FILENO)) {
                    // Only give a message if we are interactive. If connected via pipe, be quiet
                    printf("Display echo for %s set: %s\n", aDescriptorRef.source_name_unique.c_str(), aDescriptorRef.do_display_report ? "on" : "off");
                }                    
            }
            else {
                fprintf(stderr, "UPLC command requested echo but no enable/disable value found:%s\n",
                    nonprintableToHexadecimal(message_string.c_str()).c_str());
            }
            break;

        default:
            fprintf(stderr, "UPLC command requested but command char %c not recognized:%s\n",
                message_string.at(UPLC_COMMAND_PREFIX.length()), nonprintableToHexadecimal(message_string.c_str()).c_str());
            break;
    }
}

void Receiver::showClients() {
    for (int i=0; i < num_socket_descriptors; i++) {
        if (socket_descriptors[i].fd == listen_for_clients_sockfd) { 
            continue;  // skip port listener
        }

        TextChangeOrder clientDescription(TextChangeOrder::getRegisteredTemplate(preferredCommandFormatTemplateIndex));
        clientDescription.setString(descriptor_support_data[i].source_name_unique);

        if (active_display_sockfd >= 0 && socket_descriptors[i].fd == active_display_sockfd) {
            const std::string prefixActive = "* ";
            clientDescription.setString(prefixActive + clientDescription.getText());
        }

        RawMessage clientInfoMessage(UPLC_FORMATTED_TEXT, clientDescription.toUPLCFormattedMessage());
        lockedAppendMessageActiveQueue(clientInfoMessage);
    }
}

void Receiver::transmitClients(DescriptorInfo& aDescriptorRef) {
    char clientCountBuffer[15];
    sprintf(clientCountBuffer, "%02d", num_socket_descriptors-1);  // -1 as port listener is not a client
    std::string response = UPLC_TXMT_PREFIX + clientCountBuffer;

    for (int i=0; i < num_socket_descriptors; i++) {
        if (socket_descriptors[i].fd == listen_for_clients_sockfd) { 
            continue;  // skip port listener
        }

        if (active_display_sockfd >= 0 && socket_descriptors[i].fd == active_display_sockfd) {
            response += UPLC_TXMT_ACTIVE_CLIENT_PREFIX;
        }
        else {
            response += UPLC_TXMT_INACTIVE_CLIENT_PREFIX;
        }

        response += descriptor_support_data[i].source_name_unique;
    }
    response += PROTOCOL_END_OF_LINE;
    aDescriptorRef.pending_writes.push_back(response);  // queue for sending to this client
}

void Receiver::compressSockets() {
    const int initial_descriptors = num_socket_descriptors;
    for (int i = 0; i < num_socket_descriptors; i++) {
      if (socket_descriptors[i].fd == -1)
      {
        for (int j = i; j < num_socket_descriptors-1; j++) {
            socket_descriptors[j].fd = socket_descriptors[j+1].fd;
            descriptor_support_data[j] = descriptor_support_data[j+1];
        }
        i--;
        num_socket_descriptors--;
      }
    }
    updateIsAnyReportingRequested();

    if (isatty(STDIN_FILENO)) {
        // Only give a message if we are interactive. If connected via pipe, be quiet
        printf("Compressed array from %d, now %d clients connected.\n", initial_descriptors-1, num_socket_descriptors-1);  // -1 as port listener is not a client
    }
}

void Receiver::closeSingleSocket(int aDescriptor) {

    const bool isActiveDisplay = aDescriptor == active_display_sockfd;
    const bool isMainListen = aDescriptor == listen_for_clients_sockfd;

    // remove descriptor from socket descriptor array
    for (int i = 0; i < num_socket_descriptors; i++) {
        if (socket_descriptors[i].fd == aDescriptor) {
            // found the socket to close
            socket_descriptors[i].fd = -1;  // mark as closed
            break;
        }
    }

    close(aDescriptor);
    aDescriptor = -1;  // mark as closed
    
    if (isMainListen) {
        listen_for_clients_sockfd = -1;  // clear listening socket
        
        // this unexpected event forces closure of Receiver
        fprintf(stderr, "Closure of port listener forcing stop of Receiver\n");
        closingErrorMessage = LED_ERROR_MESSAGE_FAIL_EVENT;
        lockedStop();
    }
    else {
        if (isActiveDisplay) {
            if (isatty(STDIN_FILENO)) {
                // Only give a message if we are interactive. If connected via pipe, be quiet
                printf("Closed active display client, now none being displayed.\n");
            }

            active_display_sockfd = -1;  // clear active display socket
        }

        if (isatty(STDIN_FILENO)) {
            // Only give a message if we are interactive. If connected via pipe, be quiet
            printf("Closed single client, array not yet compressed.\n");
        }    
    }
}

Receiver::ClientSummary Receiver::getClientSummary() {
    rgb_matrix::MutexLock l(&mutex_descriptors);

    ClientSummary summary;
    summary.active_client_name = "";
    
    for (int i=0; i < num_socket_descriptors; i++) {
        if (socket_descriptors[i].fd == listen_for_clients_sockfd) {
            continue;  // skip port listener
        }

        summary.client_names.push_back(descriptor_support_data[i].source_name_unique);

        if (active_display_sockfd >= 0 && socket_descriptors[i].fd == active_display_sockfd) {
            summary.active_client_name = descriptor_support_data[i].source_name_unique;
        }
    }
    return summary;
}

void Receiver::internalSetActiveClient(std::string aClientName) {
    pending_active_display_name = aClientName;  

    pending_active_at_next_message = false;     // given active command, ensure not set by arbitrary first message
    // actual update will occur in Run() thread            
}

void Receiver::closeAllSockets() {
    if (isatty(STDIN_FILENO)) {
        // Only give a message if we are interactive. If connected via pipe, be quiet
        printf("Closing %d sockets, including port listener.\n", num_socket_descriptors);
    }
    for (int i = 0; i < num_socket_descriptors; i++) {
        if (socket_descriptors[i].fd >= 0) {
            close(socket_descriptors[i].fd);
            socket_descriptors[i].fd = -1;
        }
    }
    listen_for_clients_sockfd = -1;
    active_display_sockfd = -1;
    num_socket_descriptors = 0;
}

void Receiver::queueCompletedLines(DescriptorInfo& aDescriptorRef) {
    // look for end-of-protocol message char anywhere in the buffer, then pull and parse lines
    while (extractLineToQueue(aDescriptorRef)) {
        // keep extracting lines until none remain finished in the buffer
    }    
}

/**
 * Prints a string to the screen, with non-printable characters displayed in hexadecimal form.
 *
 * @param str: The string to be printed.
 */
std::string Receiver::nonprintableToHexadecimal(const char* str) {
    std::string editedString;   // empty to start

    // Iterate through each character in the string.
    for (int i = 0; str[i] != '\0'; i++) {
        // Check if the character is printable.
        if (isprint(str[i])) {
            editedString += str[i];  // Print the character as is.
        } else {
            constexpr int MAX_CONVERSION_LENGTH = 5;
            char formattedBuffer[MAX_CONVERSION_LENGTH];
            // Print the character in uppercase hexadecimal form.
            sprintf(formattedBuffer, "\\x%02X", (unsigned char) str[i]);
            editedString += formattedBuffer;
        }
    }
    return editedString;
}

void Receiver::setPreferredCommandFormatTemplate(int templateIndex) {
    preferredCommandFormatTemplateIndex = templateIndex;
}