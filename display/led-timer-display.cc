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

#include "led-matrix.h"
#include "graphics.h"

#include "bdf-10x20-local.h"

//#include <algorithm>
//#include <fstream>
//#include <streambuf>
//#include <string>

#include <getopt.h>  // for command line options
#include <math.h>    // for fabs
#include <signal.h>
//#include <stdint.h>
#include <string.h>
//#include <sys/stat.h>

#include <time.h>    // for monitoring clock for steady scrolling
#include <unistd.h>  // for option parsing; sleep

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>  // inet_ntoa

#include <sstream>  // stringstream
#include <ios>

using namespace rgb_matrix;

volatile bool interrupt_received = false;
static void InterruptHandler(int signo) {
  interrupt_received = true;
}

static const int TCP_PORT_DEFAULT = 21967;
static const char *LED_ERROR_MESSAGE_SOCKET = "P-ERR1";
static const char *LED_ERROR_MESSAGE_BIND = "P-ERR2";
static const char *LED_ERROR_MESSAGE_ACCEPT = "P-ERR3";
static const char *LED_ERROR_MESSAGE_SOCKET_OPTIONS = "P-ERR4";

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

static bool parseColor(Color *c, const char *str) {
  return sscanf(str, "%hhu,%hhu,%hhu", &c->r, &c->g, &c->b) == 3;
}

static bool FullSaturation(const Color &c) {
  return (c.r == 0 || c.r == 255)
    && (c.g == 0 || c.g == 255)
    && (c.b == 0 || c.b == 255);
}

static void add_micros(struct timespec *accumulator, long micros) {
  const long billion = 1000000000;
  const int64_t nanos = (int64_t) micros * 1000;
  accumulator->tv_sec += nanos / billion;
  accumulator->tv_nsec += nanos % billion;
  while (accumulator->tv_nsec > billion) {
    accumulator->tv_nsec -= billion;
    accumulator->tv_sec += 1;
  }
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
  RGBMatrix::Options matrix_options;
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

  Color color(255, 255, 255);
  Color bg_color(0, 0, 0);
  Color outline_color(0,0,0);
  bool with_outline = false;

  const char *bdf_font_file = NULL;
  const char *EMPTY_STRING = "";
  std::string line(EMPTY_STRING);      // default empty string displayed
  bool xorigin_configured = false;
  int x_orig = 0;
  int y_orig = 0;
  int loops_orig = 0;

  int letter_spacing = 0;
  float speed = 7.0f;
  int loops = -1;
  int blink_on = 0;
  int blink_off = 0;

  int sockfd = -1, newsockfd = -1;  // initialize sockets as not valid
  socklen_t clilen;
  const uint32_t TCP_BUFFER_SIZE = 96;
  char socket_buffer[TCP_BUFFER_SIZE];
  struct sockaddr_in serv_addr, cli_addr;
  int port_number = TCP_PORT_DEFAULT;

  int opt;
  while ((opt = getopt(argc, argv, "x:y:f:C:B:O:t:s:l:b:p:Q")) != -1) {  // ':' suffix indicates required argument
    switch (opt) {
    case 's': speed = atof(optarg); break;
    case 'b':
      if (sscanf(optarg, "%d,%d", &blink_on, &blink_off) == 1) {
        blink_off = blink_on;
      }
      fprintf(stderr, "hz: on=%d off=%d\n", blink_on, blink_off);
      break;
    case 'l': loops_orig = atoi(optarg); break;
    case 'x': x_orig = atoi(optarg); xorigin_configured = true; break;
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
    case 'O':
      if (!parseColor(&outline_color, optarg)) {
        fprintf(stderr, "Invalid outline color spec: %s\n", optarg);
        return usage(argv[0]);
      }
      with_outline = true;
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

//  if (line.empty()) {
//    fprintf(stderr, "Add the text you want to print on the command-line or -i for input file.\n");
//    return usage(argv[0]);
//  }

  /*
   * Load font. If using a file rather than the default, it needs to be a filename with a bdf bitmap font.
   */
  rgb_matrix::Font font;
  if (bdf_font_file == NULL) {
    // read built-in default font
    if (!font.ReadFont(BDF_10X20_STRING)) {
      fprintf(stderr, "Couldn't read default built-in 10x20 font\n");
      return 1;
    }
  }
  else if (!font.LoadFont(bdf_font_file)) {
    fprintf(stderr, "Couldn't load font '%s'\n", bdf_font_file);
    return 1;
  }

  /*
   * If we want an outline around the font, we create a new font with
   * the original font as a template that is just an outline font.
   */
  rgb_matrix::Font *outline_font = NULL;
  if (with_outline) {
    outline_font = font.CreateOutlineFont();
  }

  RGBMatrix *canvas = RGBMatrix::CreateFromOptions(matrix_options, runtime_opt);
  if (canvas == NULL)
    return 1;

  const bool all_extreme_colors = (matrix_options.brightness == 100)
    && FullSaturation(color)
    && FullSaturation(bg_color)
    && FullSaturation(outline_color);
  if (all_extreme_colors)
    canvas->SetPWMBits(1);

  // Create a new canvas to be used with led_matrix_swap_on_vsync
  FrameCanvas *offscreen_canvas = canvas->CreateFrameCanvas();

  const int scroll_direction = (speed >= 0) ? -1 : 1;
  speed = fabs(speed);
  int delay_speed_usec = 1000000;
  if (speed > 0) {
    delay_speed_usec = 1000000 / speed / font.CharacterWidth('W');
  }

  if (!xorigin_configured) {
    if (speed == 0) {
      // There would be no scrolling, so text would never appear. Move to front.
      x_orig = with_outline ? 1 : 0;
    } else {
      x_orig = scroll_direction < 0 ? canvas->width() : 0;
    }
  }

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
    fprintf(stderr, "bind(%d) failed\n", port_number);
    line = LED_ERROR_MESSAGE_BIND;
  }
  if (socket_ok && isatty(STDIN_FILENO)) {
    // Only give a message if we are interactive. If connected via pipe, be quiet
    printf("Listening on port %d...\n", port_number);
  }

  int x = x_orig;
  int y = y_orig;
  int length = 0;

  // set up initial text before waiting for connection
  offscreen_canvas->Fill(bg_color.r, bg_color.g, bg_color.b);
  length = rgb_matrix::DrawText(offscreen_canvas, font,  // length = holds how many pixels our text takes up
                                x, y + font.baseline(),
                                color, NULL,
                                line.c_str(), letter_spacing);

  // Swap the offscreen_canvas with canvas on vsync, avoids flickering
  offscreen_canvas = canvas->SwapOnVSync(offscreen_canvas);

  if (socket_ok) {
    const int MAX_PENDING_CONNECTION = 5;
    listen(sockfd, MAX_PENDING_CONNECTION);
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

  struct timespec next_frame = {0, 0};

  signal(SIGTERM, InterruptHandler);
  signal(SIGINT, InterruptHandler);
  if (isatty(STDIN_FILENO)) {
    // Only give a message if we are interactive. If connected via pipe, be quiet
    printf("CTRL-C for exit.\n");
  }

  const char PROTOCOL_END_OF_LINE = '\x0d';
  std::string tcp_unprocessed(EMPTY_STRING);  // buffer to accumulate unprocessed messages separated by newlines

  uint64_t frame_counter = 0;
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

      // reset position and any looping information
      x = x_orig;
      y = y_orig;
      loops = loops_orig;
    }

    if (!socket_ok || found_line || speed > 0) {
      ++frame_counter;
      offscreen_canvas->Fill(bg_color.r, bg_color.g, bg_color.b);
      const bool draw_on_frame = (blink_on <= 0)
        || (frame_counter % (blink_on + blink_off) < (uint64_t)blink_on);

      if (draw_on_frame) {
        if (outline_font) {
          // The outline font, we need to write with a negative (-2) text-spacing,
          // as we want to have the same letter pitch as the regular text that
          // we then write on top.
          rgb_matrix::DrawText(offscreen_canvas, *outline_font,
                               x - 1, y + font.baseline(),
                               outline_color, NULL,
                               line.c_str(), letter_spacing - 2);
        }

        // length = holds how many pixels our text takes up
        length = rgb_matrix::DrawText(offscreen_canvas, font,
                                      x, y + font.baseline(),
                                      color, NULL,
                                      line.c_str(), letter_spacing);
      }

      if (speed > 0) {
        x += scroll_direction;
        if ((scroll_direction < 0 && x + length < 0) ||
            (scroll_direction > 0 && x > canvas->width())) {
          x = x_orig + ((scroll_direction > 0) ? -length : 0);
          if (loops > 0) {
            if (--loops == 0) {
              line = EMPTY_STRING;  // clear the string being displayed, then wait for new text to display
            }
          }
        }

        // Make sure render-time delays are not influencing scroll-time
        if (next_frame.tv_sec == 0 && next_frame.tv_nsec == 0) {
          // First time. Start timer, but don't wait.
          clock_gettime(CLOCK_MONOTONIC, &next_frame);
        } else {
          add_micros(&next_frame, delay_speed_usec);
          clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_frame, NULL);
        }
      }

      // Swap the offscreen_canvas with canvas on vsync, avoids flickering
      offscreen_canvas = canvas->SwapOnVSync(offscreen_canvas);
      if (!socket_ok && speed <= 0) do_pause();  // Nothing to scroll. Delay a few seconds before looping
    }
  }

  // close sockets
  // (main loop could be wrapped in try/catch)
  if (newsockfd >= 0) close(newsockfd);
  if (sockfd >= 0) close(sockfd);

  // Finished. Shut down the RGB matrix.
  canvas->Clear();
  delete canvas;

  return 0;
}
