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

static auto LED_ERROR_MESSAGE_SOCKET = "P-ERR-S";
static auto LED_ERROR_MESSAGE_BIND = "P-ERR-B";
static auto LED_ERROR_MESSAGE_LISTEN = "P-ERR-L";
static auto LED_ERROR_MESSAGE_SOCKET_OPTIONS = "P-ERR-O";

Receiver::Receiver(int aPort_number)  : running_(false), sockfd(-1), newsockfd(-1), clilen(0),
                                        port_number(aPort_number), clearDisplayOnUnrecognizedMessage(true) {
    bzero(socket_buffer,PROTOCOL_MESSAGE_MAX_LENGTH);
    bzero((char *) &serv_addr, sizeof(serv_addr));
    bzero((char *) &cli_addr, sizeof(cli_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port_number);

}

Receiver::Receiver() : Receiver(TCP_PORT_DEFAULT) {}    // forward to other constructor

Receiver::~Receiver() {
    Stop();


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

void Receiver::setupSocket() {
    if (isatty(STDIN_FILENO)) {
        // Only give a message if we are interactive. If connected via pipe, be quiet
        printf("Setting up socket...\n");
    }

    //TODO support later attempts to start TCP port with e.g. RTPro, in case cable plugged in after power turned on
    //TODO support UDP port messaging, e.g. from SplitSecondTiming software

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        fprintf(stderr, "socket() failed\n");
        queueReceivedMessage(RawMessage(SIMPLE_TEXT, LED_ERROR_MESSAGE_SOCKET));
        Stop();
        return;
    }
    const int enable = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0 ||
        setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(int)) < 0) {
        fprintf(stderr, "setsockopt() failed\n");
        queueReceivedMessage(RawMessage(SIMPLE_TEXT, LED_ERROR_MESSAGE_SOCKET_OPTIONS));
        Stop();
        return;
    }

    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        fprintf(stderr, "bind(port %d) failed, errno=%d\n", port_number, errno);
        queueReceivedMessage(RawMessage(SIMPLE_TEXT, LED_ERROR_MESSAGE_BIND));
        Stop();
        return;
    }

    const int MAX_PENDING_CONNECTION = 5;
    if (listen(sockfd, MAX_PENDING_CONNECTION) < 0) {  // mark socket as passive (listener)
        fprintf(stderr, "listen(port %d, max %d) failed, errno=%d\n", port_number, MAX_PENDING_CONNECTION, errno);
        queueReceivedMessage(RawMessage(SIMPLE_TEXT, LED_ERROR_MESSAGE_LISTEN));
        Stop();
        return;
    }

    if (isatty(STDIN_FILENO)) {
        // Only give a message if we are interactive. If connected via pipe, be quiet
        printf("Listening on port %d...\n", port_number);
    }

}

void Receiver::checkAndAcceptConnection() {
    if (newsockfd < 0) {
        // size 1 "set" of socket descriptors
        struct pollfd fds[1];
        memset(fds, 0 , sizeof(fds));
        fds[0].fd = sockfd;
        fds[0].events = POLLIN;
        const int timeout_poll = 100; // milliseconds

        if (poll(fds, 1, timeout_poll) > 0) {
            clilen = sizeof(cli_addr);
            newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
            if (newsockfd >= 0 && isatty(STDIN_FILENO)) {
                // Only give a message if we are interactive. If connected via pipe, be quiet
                printf("Connected to:%s\n",(cli_addr.sin_family == AF_INET ? inet_ntoa(cli_addr.sin_addr) : "(non-IPV4)"));
            }
        }
    }
}

void Receiver::checkAndAppendData(std::string& unprocessed_buffer) {
    // look, but do not wait if no data ready
    const int num_read = recv(newsockfd, socket_buffer, PROTOCOL_MESSAGE_MAX_LENGTH, MSG_DONTWAIT);
    if (num_read > 0) {

        socket_buffer[num_read] = '\0';  // ensure end-of-string safely added (buffer has one extra size element)

        if (isatty(STDIN_FILENO)) {
            // Only give a message if we are interactive. If connected via pipe, be quiet
            printf("Rcvd(len=%d)\n",num_read);
        }

        // accumulate, so we can handle more than one protocol message in tcp buffer, or partial message in buffer
        unprocessed_buffer.append(socket_buffer, num_read);
    }
    else if (num_read == 0) {   // client indicates end of connection
        close(newsockfd);
        newsockfd = -1;
        if (isatty(STDIN_FILENO)) {
            // Only give a message if we are interactive. If connected via pipe, be quiet
            printf("Connection closed by client\n");
        }
    }
}

bool Receiver::extractLineToQueue(std::string& unprocessed_buffer) {
    std::string::size_type eol_pos = 0;
    eol_pos = unprocessed_buffer.find_first_of(PROTOCOL_END_OF_LINE);
    const bool foundLine = eol_pos != std::string::npos;
    if (foundLine) {
        // found a line
        if (eol_pos >= PROTOCOL_MESSAGE_MAX_LENGTH) {
            fprintf(stderr, "Line too long(%lu > %d) in buffer:%s\n",
                static_cast<unsigned long>(eol_pos), PROTOCOL_MESSAGE_MAX_LENGTH,
                nonprintableToHexadecimal(unprocessed_buffer.c_str()).c_str());
            eol_pos = PROTOCOL_MESSAGE_MAX_LENGTH - 2;
        }
        const unsigned int char_in_line = eol_pos+1;  // less than TCP_BUFFER_SIZE since eol_pos max TCP_BUFFER_SIZE-2;

        // copy char from beginning of string, incl eol, and then
        // remove the message characters from the accumulation string
        char single_line_buffer[PROTOCOL_MESSAGE_MAX_LENGTH];
        unprocessed_buffer.copy(single_line_buffer, char_in_line, 0);
        unprocessed_buffer.erase(0, char_in_line);
        // place end-of-string character; index is safe since eol_pos max TCP_BUFFER_SIZE-2, above
        single_line_buffer[char_in_line] = '\0';

        if (isatty(STDIN_FILENO)) {
            // Only give a message if we are interactive. If connected via pipe, be quiet
            printf("Extracted line length %3lu, leaving %3lu unprocessed: %s\n",
                   static_cast<unsigned long>(strlen(single_line_buffer)),
                   static_cast<unsigned long>(strlen(unprocessed_buffer.c_str())),
                   nonprintableToHexadecimal(single_line_buffer).c_str()
                   );
        }

        parseLineToQueue(single_line_buffer);
    }

    return foundLine;
}

void Receiver::parseLineToQueue(const char* single_line_buffer) {
    //const bool alge_line =
        parseAlgeLineToQueue(single_line_buffer);    // if true, line has been pushed into queue
}

bool Receiver::parseAlgeLineToQueue(const char* single_line_buffer) {
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
        fprintf(stderr, "Discarding unrecognized message%s:%s\n",
            clearDisplayOnUnrecognizedMessage ? " (and clear display)" : "",
            nonprintableToHexadecimal(single_line_buffer).c_str());

        if (clearDisplayOnUnrecognizedMessage) queueReceivedMessage(RawMessage(SIMPLE_TEXT, ""));
    }
    else {
        // if appears to be valid, queue for processing by other classes
        queueReceivedMessage(RawMessage(ALGE_DLINE, single_line_buffer));
    }

    return possible_alge_message;
}

void Receiver::Run() {
    setupSocket();

    std::string tcp_unprocessed;  // empty buffer to accumulate unprocessed messages separated by newlines

    while (running()) { // handles lock within the call
        checkAndAcceptConnection();

        if (newsockfd >= 0) {     // connection is open
            checkAndAppendData(tcp_unprocessed);

            // look for end-of-protocol message char anywhere in the buffer, then pull and parse lines
            while (extractLineToQueue(tcp_unprocessed)) {
                // keep extracting lines
            }
        }
        // short sleep
        usleep(15 * 1000);
    }

    // close sockets
    // (main loop could be wrapped in try/catch)
    if (newsockfd >= 0) {close(newsockfd); newsockfd = -1;}
    if (sockfd >= 0) {close(sockfd); sockfd = -1;}

    if (isatty(STDIN_FILENO)) {
        // Only give a message if we are interactive. If connected via pipe, be quiet
        printf("Sockets closed.\n");
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

