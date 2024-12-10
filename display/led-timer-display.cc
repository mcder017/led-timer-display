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
#include <csignal>
#include <string>

#include <unistd.h>  // for io on linux, also option parsing; sleep

#include <cstdio>

#include <ios>

#include "TextChangeOrder.h"
#include "Displayer.h"
#include "Receiver.h"
#include "MessageFormatter.h"

volatile bool interrupt_received = false;
static void InterruptHandler(int signo) {
  interrupt_received = true;
  // not safe to call printf in signal handler
}

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

  rgb_matrix::Color fg_color = TextChangeOrder::getDefaultForegroundColor();
  rgb_matrix::Color bg_color = TextChangeOrder::getDefaultBackgroundColor();

  std::string bdf_font_file_name; // empty means "use default"
  std::string line;               // default to empty string displayed
  int x_orig = 0;
  int y_orig = 0;

  int letter_spacing = 0;
  float speed = 7.0f;
  bool set_horizontal_scroll = true;
  bool set_single_scroll = true;

  int port_number = Receiver::TCP_PORT_DEFAULT;
  int opt;
  while ((opt = getopt(argc, argv, "x:y:f:C:B:t:s:p:v:i:Q")) != -1) {  // ':' suffix indicates required argument
    switch (opt) {
    case 's': speed = atof(optarg); break;
    case 'x': x_orig = atoi(optarg); break;
    case 'y': y_orig = atoi(optarg); break;
    case 'f': bdf_font_file_name = optarg; break;
    case 't': letter_spacing = atoi(optarg); break;
    case 'v': set_horizontal_scroll = atoi(optarg) != 0; break;
    case 'i': set_single_scroll = atoi(optarg) != 0; break;
    case 'C':
      if (!parseColor(&fg_color, optarg)) {
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

      bdf_font_file_name = "";  // use default 10x20 font, loaded below
      fg_color.setColor(255,0,0);  // red
      letter_spacing = -1;
      y_orig = -2;

      set_horizontal_scroll = true;
      set_single_scroll = true;

      speed = 0;
      break;
    default:
      return usage(argv[0]);
    }
  }

  // check for any initial string to display, limited by whitespace being single spaces
  for (int i = optind; i < argc; ++i) {
    line.append(argv[i]).append(" ");
  }

  // Load font. If using a file rather than the default, it needs to be a filename with a bdf bitmap font.
  rgb_matrix::Font* fontPtr = nullptr;
  if (bdf_font_file_name.empty()) {
    // read built-in default font
    fontPtr = SpacedFont::getDefaultFontPtr();
    if (fontPtr == nullptr) {
      fprintf(stderr, "Couldn't load default font\n");
      return 1;
    }
  }
  else {
    fontPtr = new rgb_matrix::Font();
    if (!fontPtr->LoadFont(bdf_font_file_name.c_str())) {
      fprintf(stderr, "Couldn't load font '%s'\n", bdf_font_file_name.c_str());
      return 1;
    }
  }

  Displayer myDisplayer(matrix_options, runtime_opt);
  myDisplayer.setXOrigin(x_orig);
  myDisplayer.setYOrigin(y_orig);

  Receiver myReceiver(port_number);
  myReceiver.Start();

  MessageFormatter myFormatter(myDisplayer, fontPtr, letter_spacing, fg_color, bg_color, speed,
                              set_horizontal_scroll, set_single_scroll);

  // initial display of text (we are awake, but perhaps not yet connected)
  myFormatter.handleMessage(Receiver::RawMessage(Receiver::Protocol::SIMPLE_TEXT, line));

  signal(SIGTERM, InterruptHandler);
  signal(SIGINT, InterruptHandler);
  if (isatty(STDIN_FILENO)) {
    // Only give a message if we are interactive. If connected via pipe, be quiet
    printf("Press CTRL-C for exit.\n");
  }

  while (!interrupt_received) {
    // if new valid message,  decide what to display
    if (myReceiver.isPendingMessage()) {
      const Receiver::RawMessage message = myReceiver.popPendingMessage();
      myFormatter.handleMessage(message);
    }

    myDisplayer.iota();

    // when no messages can be received, and there is nothing to scroll, delay quite a while before looping
    if (!myReceiver.isRunning() && !myDisplayer.getChangeOrder().isScrolling() && !myDisplayer.isChangeOrderDone()) {
      do_pause();
    }
    else {
      // short sleep
      usleep(15 * 1000);
    }
  }
  fprintf(stderr,"Interrupt received\n");

  myReceiver.Stop();

  fprintf(stderr,"Exiting\n");
  return 0;
}
