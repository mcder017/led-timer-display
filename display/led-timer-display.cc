//
// Created by WMcD on 12/4/2024.
//
// ( note that the led-matrix library this depends on is GPL v2)
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation version 2.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://gnu.org/licenses/gpl-2.0.txt>

#include <getopt.h>  // for command line options
#include <signal.h>
#include <string>

#include <unistd.h>  // for option parsing; sleep

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>  // inet_ntoa

#include <sstream>  // stringstream
#include <ios>
#include <led-matrix-c.h>

#include "TextChangeOrder.h"
#include "Displayer.h"


volatile bool interrupt_received = false;
static void InterruptHandler(int signo) {
  interrupt_received = true;
}

static const int TCP_PORT_DEFAULT = 21967;
static const char *LED_ERROR_MESSAGE_SOCKET = "P-ERR-S";
static const char *LED_ERROR_MESSAGE_BIND = "P-ERR-B";
static const char *LED_ERROR_MESSAGE_LISTEN = "P-ERR-L";
static const char *LED_ERROR_MESSAGE_ACCEPT = "P-ERR-A";
static const char *LED_ERROR_MESSAGE_SOCKET_OPTIONS = "P-ERR-O";

static int usage(const char *progname) {
  fprintf(stderr, "usage: %s [options] [<text>| -i <filename>]\n", progname);
  fprintf(stderr, "Takes text and scrolls it with speed -s\n");
  fprintf(stderr, "Options:\n");
  fprintf(stderr,
          "\t-f <font-file>    : Path to *.bdf-font to be used.\n"
          "\t-s <speed>        : Approximate letters per second. \n"
          "\t                    Positive: scroll right to left; Negative: scroll left to right\n"
          "\t                    (Zero for no scrolling)\n"
          "\t-l <loop-count>   : Number of loops through the text. "
          "-1 for endless (default)\n"
          "\t-b <on-time>,<off-time>  : Blink while scrolling. Keep "
          "on and off for these amount of scrolled pixels.\n"
          "\t-x <x-origin>     : Shift X-Origin of displaying text (Default: 0)\n"
          "\t-y <y-origin>     : Shift Y-Origin of displaying text (Default: 0)\n"
          "\t-t <track-spacing>: Spacing pixels between letters (Default: 0)\n"
          "\n"
          "\t-C <r,g,b>        : Text Color. Default 255,255,255 (white)\n"
          "\t-B <r,g,b>        : Background-Color. Default 0,0,0 (black)\n"
          "\t-O <r,g,b>        : Outline-Color, e.g. to increase contrast.\n"
          );
  fprintf(stderr, "\nGeneral LED matrix options:\n");
  rgb_matrix::PrintMatrixFlags(stderr);
  fprintf(stderr, "\nTCP configuration:\n");
  fprintf(stderr,
          "\t-p <portnumber>   : TCP port number (default 21967)\n"
          );
  fprintf(stderr, "\nOne-step configurations:\n");
  fprintf(stderr,
          "\t-Q                : Quick configuration with\n"
          "\t                    row 16, cols 32, chain 3, parallel 1,\n"
          "\t                    GPIO slowdown 2, GPIO map adafruit-hat-pwm,\n"
          "\t                    font 10x20, text red (255,0,0), y-offset -2, track spacing -1\n"
          "\t                    speed 0\n"
          );
  return 1;
}

static bool parseColor(rgb_matrix::Color *c, const char *str) {
  return sscanf(str, "%hhu,%hhu,%hhu", &c->r, &c->g, &c->b) == 3;
}

static void do_pause() {
  sleep(3);
}

/**
 * Prints a string to the screen, with non-printable characters displayed in hexadecimal form.
 *
 * @param str: The string to be printed.
 */
static void printStringWithHexadecimal(char* str) {
  // Iterate through each character in the string.
  for (int i = 0; str[i] != '\0'; i++) {
    // Check if the character is printable.
    if (isprint(str[i])) {
      printf("%c", str[i]);  // Print the character as is.
    } else {
      printf("\\x%02x", (unsigned char) str[i]);  // Print the character in hexadecimal form.
    }
  }
}

static void replaceNonPrintableCharacters(char *str, char repl_char) {
  // Iterate through each character in the string.
  for (int i = 0; str[i] != '\0'; i++) {
    // Check if the character is printable.
    if (!isprint(str[i])) {
      str[i] = repl_char;
    }
  }
}



int main(int argc, char *argv[]) {
  rgb_matrix::RGBMatrix::Options matrix_options;
  rgb_matrix::RuntimeOptions runtime_opt;
  // If started with 'sudo': make sure to drop privileges to same user
  // we started with, which is the most expected (and allows us to read
  // files as that user).
  runtime_opt.drop_priv_user = getenv("SUDO_UID");
  runtime_opt.drop_priv_group = getenv("SUDO_GID");
  if (!rgb_matrix::ParseOptionsFromFlags(&argc, &argv,
                                         &matrix_options, &runtime_opt)) {
    return usage(argv[0]);
  }

  rgb_matrix::Color color(TextChangeOrder::getDefaultForegroundColor());
  rgb_matrix::Color bg_color(TextChangeOrder::getDefaultBackgroundColor());

  const char *bdf_font_file = NULL;
  const char *EMPTY_STRING = "";
  std::string line(EMPTY_STRING);      // default empty string displayed
  int x_orig = 0;
  int y_orig = 0;

  int letter_spacing = 0;
  float speed = 7.0f;

  int sockfd = -1, newsockfd = -1;  // initialize sockets as not valid
  socklen_t clilen;
  const uint32_t TCP_BUFFER_SIZE = 96;
  char socket_buffer[TCP_BUFFER_SIZE];
  struct sockaddr_in serv_addr, cli_addr;
  int port_number = TCP_PORT_DEFAULT;

  int opt;
  while ((opt = getopt(argc, argv, "x:y:f:C:B:t:s:p:Q")) != -1) {  // ':' suffix indicates required argument
    switch (opt) {
    case 's': speed = atof(optarg); break;
    case 'x': x_orig = atoi(optarg); break;
    case 'y': y_orig = atoi(optarg); break;
    case 'f': bdf_font_file = strdup(optarg); break;
    case 't': letter_spacing = atoi(optarg); break;
    case 'C':
      if (!parseColor(&color, optarg)) {
        fprintf(stderr, "Invalid color spec: %s\n", optarg);
        return usage(argv[0]);
      }
      break;
    case 'B':
      if (!parseColor(&bg_color, optarg)) {
        fprintf(stderr, "Invalid background color spec: %s\n", optarg);
        return usage(argv[0]);
      }
      break;
    case 'p': port_number = atoi(optarg); break;
    case 'Q':
      matrix_options.rows = 16;
      matrix_options.cols = 32;
      matrix_options.chain_length = 3;
      matrix_options.parallel = 1;

      runtime_opt.gpio_slowdown = 2;
      matrix_options.hardware_mapping = "adafruit-hat-pwm";

      bdf_font_file = NULL;  // use default 10x20 font, loaded below
      color.setColor(255,0,0);  // red
      letter_spacing = -1;
      y_orig = -2;

      speed = (float)0;
      break;
    default:
      return usage(argv[0]);
    }
  }

  // check for any initial string to display, limited by whitespace being single spaces
  for (int i = optind; i < argc; ++i) {
    line.append(argv[i]).append(" ");
  }

  /*
  if (line.empty()) {
    fprintf(stderr, "Add the text you want to print on the command-line or -i for input file.\n");
    return usage(argv[0]);
  }
  */

  /*
   * Load font. If using a file rather than the default, it needs to be a filename with a bdf bitmap font.
   */
  rgb_matrix::Font* fontPtr = nullptr;
  if (bdf_font_file == nullptr) {
    // read built-in default font
    fontPtr = SpacedFont::getDefaultFontPtr();
    if (fontPtr == nullptr) {
      fprintf(stderr, "Couldn't load default font\n");
      return 1;
    }
  }
  else if (!fontPtr->LoadFont(bdf_font_file)) {
    fprintf(stderr, "Couldn't load font '%s'\n", bdf_font_file);
    return 1;
  }

  Displayer displayer(matrix_options, runtime_opt);

  if (isatty(STDIN_FILENO)) {
    // Only give a message if we are interactive. If connected via pipe, be quiet
    printf("Setting up socket...\n");
  }


  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  bool socket_ok = !(sockfd < 0);
  if (!socket_ok) {
    fprintf(stderr, "socket() failed\n");
    line = LED_ERROR_MESSAGE_SOCKET;
  }
  const int enable = 1;
  if (socket_ok &&
      (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0 ||
       setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(int)) < 0)) {
    socket_ok = false;
    fprintf(stderr, "setsockopt() failed\n");
    line = LED_ERROR_MESSAGE_SOCKET_OPTIONS;
  }

  bzero((char *) &serv_addr, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = INADDR_ANY;
  serv_addr.sin_port = htons(port_number);
  if (socket_ok &&
      bind(sockfd, (struct sockaddr *) &serv_addr,
              sizeof(serv_addr)) < 0) {
    socket_ok = false;
    fprintf(stderr, "bind(port %d) failed, errno=%d\n", port_number, errno);
    line = LED_ERROR_MESSAGE_BIND;
  }
  const int MAX_PENDING_CONNECTION = 5;
  if (socket_ok && listen(sockfd, MAX_PENDING_CONNECTION) < 0) {  // mark socket as passive (listener)
    socket_ok = false;
    fprintf(stderr, "listen(port %d, max %d) failed, errno=%d\n", port_number, MAX_PENDING_CONNECTION, errno);
    line = LED_ERROR_MESSAGE_LISTEN;
  }

  if (socket_ok && isatty(STDIN_FILENO)) {
    // Only give a message if we are interactive. If connected via pipe, be quiet
    printf("Listening on port %d...\n", port_number);
  }

  // initial display of text (we are awake, but not yet connected)
  displayer.startChangeOrder(TextChangeOrder(line));
  displayer.iota();

  if (socket_ok) {
    clilen = sizeof(cli_addr);
    newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
    if (newsockfd < 0) {
      socket_ok = false;
      fprintf(stderr, "accept() failed\n");
      line = LED_ERROR_MESSAGE_ACCEPT;
    }
    if (socket_ok && isatty(STDIN_FILENO)) {
      // Only give a message if we are interactive. If connected via pipe, be quiet
      printf("Connected to:%s\n",(cli_addr.sin_family == AF_INET ? inet_ntoa(cli_addr.sin_addr) : "(non-IPV4)"));
    }
  }
  bzero(socket_buffer,TCP_BUFFER_SIZE);

  signal(SIGTERM, InterruptHandler);
  signal(SIGINT, InterruptHandler);
  if (isatty(STDIN_FILENO)) {
    // Only give a message if we are interactive. If connected via pipe, be quiet
    printf("CTRL-C for exit.\n");
  }

  const char PROTOCOL_END_OF_LINE = '\x0d';
  std::string tcp_unprocessed(EMPTY_STRING);  // buffer to accumulate unprocessed messages separated by newlines

  while (!interrupt_received) {
    int num_read = (socket_ok ? recv(newsockfd, socket_buffer, TCP_BUFFER_SIZE-1, MSG_DONTWAIT) : 0);
    if (num_read > 0) { // TBD: replace with logic if have new data to display (orig code monitored file for changes)

      socket_buffer[num_read] = '\0';  // ensure end-of-string

      if (isatty(STDIN_FILENO)) {
        // Only give a message if we are interactive. If connected via pipe, be quiet
        printf("Rcvd(len=%d):",num_read);
        printStringWithHexadecimal(socket_buffer);
        printf("\n");
      }

      // accumulate, so we can handle more than one protocol message in tcp buffer, or partial message in buffer
      tcp_unprocessed.append(socket_buffer, num_read);
    }

    // if end-of-protocol message char is anywhere in the buffer, pull first line
    std::string::size_type eol_pos = tcp_unprocessed.find_first_of(PROTOCOL_END_OF_LINE);
    const bool found_line = eol_pos != std::string::npos;
    if (found_line) {  // found the character
      if (eol_pos >= TCP_BUFFER_SIZE-1) {
        fprintf(stderr, "Line too long(%lu) in buffer:%s\n", (unsigned long)eol_pos, tcp_unprocessed.c_str());
        eol_pos = TCP_BUFFER_SIZE - 2;
      }
      const int char_in_line = eol_pos+1;  // less than TCP_BUFFER_SIZE since eol_pos max TCP_BUFFER_SIZE-2;

      char single_line_buffer[TCP_BUFFER_SIZE];
      //bzero(single_line_buffer, TCP_BUFFER_SIZE);  // ensure end-of-string character will be present
      tcp_unprocessed.copy(single_line_buffer, char_in_line, 0);  // copy char from beginning of string, incl eol
      single_line_buffer[char_in_line] = '\0';  // less than TCP_BUFFER_SIZE since eol_pos max TCP_BUFFER_SIZE-2;
      tcp_unprocessed.erase(0, char_in_line);                     // remove the characters from the string

      if (isatty(STDIN_FILENO)) {
        // Only give a message if we are interactive. If connected via pipe, be quiet
        printf("Extracted line length %3lu, leaving %3lu unprocessed.\n",
               (unsigned long)strlen(single_line_buffer),
               (unsigned long)strlen(tcp_unprocessed.c_str()));
      }

      // TBD parse message and decide what to display
      char msg_buffer[TCP_BUFFER_SIZE];
      bzero(msg_buffer, TCP_BUFFER_SIZE);

      if (strlen(single_line_buffer) >= 24) {  // possible ALGE protocol message
        // TBD format nicely, such as with 00:01 for first second in running time message
        strcpy(msg_buffer,single_line_buffer+12);  // start message at possible 1 minute character
        msg_buffer[8] = '\0';  // end message at possible thousandth-of-second place
      }
      else {
        strcpy(msg_buffer,single_line_buffer);
      }
      msg_buffer[11] = '\0';  // ensure message not longer
      replaceNonPrintableCharacters(msg_buffer, '.');  // replace any non-printable characters
      line = msg_buffer;  // copy into variable used to display to LED matrix
      if (isatty(STDIN_FILENO)) {
        // Only give a message if we are interactive. If connected via pipe, be quiet
        printf("Displaying:%s\n",line.c_str());
      }

    }

    if (!socket_ok
      || found_line) {
      displayer.startChangeOrder(TextChangeOrder(line));
    }

    displayer.iota();

    // when no messages to wait for and nothing to scroll, delay quite a while before looping
    if (!socket_ok && !displayer.getChangeOrder().isScrolling() && !displayer.isChangeOrderDone()) do_pause();
  }

  // close sockets
  // (main loop could be wrapped in try/catch)
  if (newsockfd >= 0) close(newsockfd);
  if (sockfd >= 0) close(sockfd);

  return 0;
}
