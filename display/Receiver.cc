//
// Created by WMcD on 12/8/2024.
//

#include "Receiver.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>  // inet_ntoa
#include <poll.h>
#include <sys/types.h>
#include <ifaddrs.h>

#include <cstdio>
#include <ios>
#include <unistd.h>  // for io on linux, also option parsing; sleep
#include <strings.h>    // bzero
#include <string.h>     // strlen

static auto LED_ERROR_MESSAGE_SOCKET = "DISP(S)";
static auto LED_ERROR_MESSAGE_BIND = "DISP(B)";
static auto LED_ERROR_MESSAGE_LISTEN = "DISP(L)";
static auto LED_ERROR_MESSAGE_SOCKET_OPTIONS = "DISP(O)";
static auto LED_ERROR_MESSAGE_NONBLOCKING = "DISP(NB)";
static auto LED_ERROR_MESSAGE_POLL = "DISP(P)";
static auto LED_ERROR_MESSAGE_ACCEPT = "DISP(A)";
static auto LED_ERROR_MESSAGE_FAIL_EVENT = "DISP(F)";

static auto CLEAR_DISPLAY_ON_UNRECOGNIZED_MESSAGE = true; 

Receiver::Receiver(int aPort_number)  : running_(false), listen_for_clients_sockfd(-1), active_display_sockfd(-1), num_socket_descriptors(0), clilen(0),
                                        port_number(aPort_number), pending_active_at_next_message(true),
                                        closingErrorMessage("") {
    bzero((char *) &cli_addr, sizeof(cli_addr));
    bzero((struct pollfd *) &socket_descriptors, sizeof(socket_descriptors));                                        

}

Receiver::Receiver() : Receiver(TCP_PORT_DEFAULT) {}    // forward to other constructor

Receiver::~Receiver() {
    lockedStop();


}

void Receiver::Start() {
    {
      rgb_matrix::MutexLock l(&mutex_);
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
            num_socket_descriptors++;

            if (isatty(STDIN_FILENO)) {
                // Only give a message if we are interactive. If connected via pipe, be quiet
                printf("Connected to: %s\n",(cli_addr.sin_family == AF_INET ? inet_ntoa(cli_addr.sin_addr) : "(non-IPV4)"));
                printf("Now %d clients connected.\n", num_socket_descriptors-1);  // -1 as port listener is not a client
            }
        }

    } while (new_socket_descriptor >= 0);
}

bool Receiver::checkAndAppendData(int source_descriptor, std::string& unprocessed_buffer) {
    char socket_buffer[PROTOCOL_MESSAGE_MAX_LENGTH+1];     // include room for end-of-string null

    // keep reading data until none available on this source
    do {
        const int result_flag = recv(source_descriptor, socket_buffer, PROTOCOL_MESSAGE_MAX_LENGTH, MSG_DONTWAIT);

        if (result_flag > 0) {        
            socket_buffer[num_read] = '\0';  // ensure end-of-string safely added (buffer has one extra size element)

            if (isatty(STDIN_FILENO)) {
                // Only give a message if we are interactive. If connected via pipe, be quiet
                printf("%s,Rcvd(len=%d)\n", (source_descriptor==active_display_sockfd ? "Active source: " : "Inactive: "), num_read);
            }

            // accumulate. caller can handle partial message, or more than one protocol message, in buffer string
            unprocessed_buffer.append(socket_buffer, num_read);
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

bool Receiver::extractLineToQueue(std::string& aBuffer, std::deque<RawMessage>& aQueue) {
    std::string::size_type eol_pos = 0;
    eol_pos = aBuffer.find_first_of(PROTOCOL_END_OF_LINE);
    const bool foundLine = eol_pos != std::string::npos;
    if (foundLine) {
        // found a line
        if (eol_pos >= PROTOCOL_MESSAGE_MAX_LENGTH) {
            fprintf(stderr, "Line too long(%lu > %d) in buffer:%s\n",
                static_cast<unsigned long>(eol_pos), PROTOCOL_MESSAGE_MAX_LENGTH,
                nonprintableToHexadecimal(aBuffer.c_str()).c_str());
            eol_pos = PROTOCOL_MESSAGE_MAX_LENGTH - 2;
        }
        const unsigned int char_in_line = eol_pos+1;  // less than TCP_BUFFER_SIZE since eol_pos max TCP_BUFFER_SIZE-2;

        // copy char from beginning of string, incl eol, and then
        // remove the message characters from the accumulation string
        char single_line_buffer[PROTOCOL_MESSAGE_MAX_LENGTH];
        aBuffer.copy(single_line_buffer, char_in_line, 0);
        aBuffer.erase(0, char_in_line);
        // place end-of-string character; index is safe since eol_pos max TCP_BUFFER_SIZE-2, above
        single_line_buffer[char_in_line] = '\0';

        if (isatty(STDIN_FILENO)) {
            // Only give a message if we are interactive. If connected via pipe, be quiet
            printf("Extracted line length %3lu, leaving %3lu unprocessed: %s\n",
                   static_cast<unsigned long>(strlen(single_line_buffer)),
                   static_cast<unsigned long>(strlen(aBuffer.c_str())),
                   nonprintableToHexadecimal(single_line_buffer).c_str()
                   );
        }

        parseLineToQueue(single_line_buffer, aQueue);
    }

    return foundLine;
}

void Receiver::parseLineToQueue(const char* single_line_buffer, std::deque<RawMessage>& aQueue) {
    //const bool alge_line =
        parseAlgeLineToQueue(single_line_buffer, aQueue);    // if true, line has been pushed into queue
}

bool Receiver::parseAlgeLineToQueue(const char* single_line_buffer, std::deque<RawMessage>& aQueue) {

    const unsigned int char_in_line = strlen(single_line_buffer);
    bool possible_alge_message = true;

    // end of line can either be 0A 0D, or just 0D
    const char CARRIAGE_RETURN = '\x0A';
    const unsigned int data_chars_excluding_eol = char_in_line < 2
                ? 0 :
                  (single_line_buffer[char_in_line-2] == CARRIAGE_RETURN ? char_in_line-2 : char_in_line-1);

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

    if (!possible_alge_message) {
        bool doClear = CLEAR_DISPLAY_ON_UNRECOGNIZED_MESSAGE;
        fprintf(stderr, "Discarding unrecognized message%s:%s\n",
            doClear ? " (and clear display)" : "",
            nonprintableToHexadecimal(single_line_buffer).c_str());

        if (doClear) {
            aQueue.push_back(RawMessage(SIMPLE_TEXT, ""));
        }
    }
    else {
        // if appears to be valid, queue for processing by other classes
        aQueue.push_back(RawMessage(ALGE_DLINE, single_line_buffer));
    }

    return possible_alge_message;
}

void Receiver::Run() {
    const short FLAG_POLLIN = POLLIN;
    const short FLAG_SINGLE_CLOSE = POLLPRI | POLLRDHUP | POLLHUP;  // flags for which we will close single connection
    const short FLAG_DO_STOP = ~(FLAG_POLLIN | FLAG_SINGLE_CLOSE);  // any other flag treated as error for which we will stop the receiver completely

    lockedSetupInitialSocket(); // may ALSO lock running internally

    while (lockedTestRunning()) {
        // check for pending connections and data
        int result;
        {   // encapsulate lock
            rgb_matrix::MutexLock l(&mutex_descriptors);
            result = poll(socket_descriptors, num_socket_descriptors, 0); // no wait
        }        

        // handle all pending connections, data, and errors
        if (result < 0) {
            fprintf(stderr, "poll() failed, errno=%d\n", errno);
            closingErrorMessage = LED_ERROR_POLL;
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

                    if (socket_descriptors[i].revents & FLAG_POLLIN != 0) {                    
                        if (socket_descriptors[i].fd == listen_for_clients_sockfd) {
                            // new connection to accept
                            checkAndAcceptConnection(); // appends to array
                        }
                        else {
                            // data on existing connection
                            const bool reading_ok = checkAndAppendData(socket_descriptors[i].fd, tcp_unprocessed[i]);
                            if (reading_ok) {
                                queueCompletedLines(tcp_unprocessed[i], inactive_message_queue[i]);

                                if (pending_active_at_next_message && inactive_message_queue[i].size() > 0) {
                                    active_display_sockfd = socket_descriptors[i].fd;  // set active display to this source
                                }

                                // trim if inactive queue, or move inactive entries to active queue
                                const bool isActiveDisplayBuffer = (socket_descriptors[i].fd == active_display_sockfd);
                                lockedProcessQueue(inactive_message_queue[i], isActiveDisplayBuffer); 
                            }
                            else {  
                                // received signal to close connection
                                const bool isActiveDisplay = socket_descriptors[i].fd == active_display_sockfd;
                                const bool isMainListen = socket_descriptors[i].fd == listen_for_clients_sockfd;

                                if (isatty(STDIN_FILENO)) {
                                    // Only give a message if we are interactive. If connected via pipe, be quiet
                                    printf("Closing single connection gracefully, index %d, %s, %s\n", i, , (isActiveDisplay ? "active display" : "not active display"), (isMainListen ? "port listener" : "not port listener"));
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
                    
                        if (socket_descriptors[i].revents & FLAG_SINGLE_CLOSE != 0) {                    
                            if (isatty(STDIN_FILENO)) {
                                // Only give a message if we are interactive. If connected via pipe, be quiet
                                printf("Closing single connection gracefully, index %d, %s, %s\n", i, , (isActiveDisplay ? "active display" : "not active display"), (isMainListen ? "port listener" : "not port listener"));
                            }                    
                        }
                        if (socket_descriptors[i].revents & FLAG_DO_STOP != 0) {
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
    }
    // TODO main loop could be wrapped in try/catch

    if (closingErrorMessage != "") {
        // if error message to display, queue it
        lockedActiveQueueReceivedMessage(RawMessage(SIMPLE_TEXT, closingErrorMessage));
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

void Receiver::lockedProcessQueue(std::deque<RawMessage>& aQueue, bool isActiveSource) {
    if (!isActiveSource) {
        while (aQueue.size() > 1) {    
            // for sources that are not being displayed,
            // shrink queue to only retain most recent message
            aQueue.pop_front();
        }
    }
    else {
        // move any inactive queued messages to active queue
        while (aQueue.size() > 0) {
            lockedActiveQueueReceivedMessage(aQueue.front());
            aQueue.pop_front();
        }
    }
}

void Receiver::compressSockets() {
    for (i = 0; i < num_socket_descriptors; i++) {
      if (socket_descriptors[i].fd == -1)
      {
        for (j = i; j < num_socket_descriptors-1; j++) {
            socket_descriptors[j].fd = socket_descriptors[j+1].fd;
        }
        i--;
        num_socket_descriptors--;
      }
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
            printf("Closed single client, now %d connected.\n", num_socket_descriptors-1);  // -1 as port listener is not a client
        }    
    }
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

void Receiver::queueCompletedLines(std::string& aBuffer, std::deque<RawMessage>& aQueue) {
    // look for end-of-protocol message char anywhere in the buffer, then pull and parse lines
    while (extractLineToQueue(aBuffer, aQueue)) {
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

