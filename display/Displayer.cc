//
// Created by WMcD on 12/7/2024.
//

#include "Displayer.h"

#include <unistd.h>  // for io on linux, also option parsing; sleep

#include "led-matrix.h"
#include "graphics.h"

#include <ctime>    // for monitoring clock for steady scrolling
#include <cmath>    // for fabs

#define EXTREME_COLORS_PWM_BITS 1

using namespace rgb_matrix;

static void add_micros(struct timespec *accumulator, long micros) {
  const long billion = 1000000000;
  const int64_t nanos = static_cast<int64_t>(micros) * 1000;
  accumulator->tv_sec += nanos / billion;
  accumulator->tv_nsec += static_cast<long>(nanos % billion);
  while (accumulator->tv_nsec > billion) {
    accumulator->tv_nsec -= billion;
    accumulator->tv_sec += 1;
  }
}


Displayer::Displayer(RGBMatrix::Options& aMatrix_options, rgb_matrix::RuntimeOptions& aRuntime_opt)
    : x_origin(0),
      y_origin(0),

      currChangeOrder(),
      currChangeOrderDone(true),
      next_frame(),

      x(0),
      y(0),
      scroll_direction(0),
      delay_speed_usec(0),

      last_change_time(0)
{

    next_frame.tv_sec = 0;
    next_frame.tv_nsec = 0;

    displayerOK = true;

    canvas = RGBMatrix::CreateFromOptions(aMatrix_options, aRuntime_opt);
    if (canvas == nullptr) {
      displayerOK = false;
      fprintf(stderr, "Error creating canvas from options objects\n");
    }
    else {
        // store RGBMatrix's default pwmbits value for future use
        defaultPWMBits = canvas->pwmbits();

        // Create a new canvas to be used with led_matrix_swap_on_vsync
        offscreen_canvas = canvas->CreateFrameCanvas();
    }
}

bool Displayer::isExtremeColors() const {
    if (!displayerOK) return false;  // canvas might not be valid

    return (canvas->brightness() == 100
            && FullSaturation(currChangeOrder.getForegroundColor())
            && FullSaturation(currChangeOrder.getBackgroundColor()));
}

bool Displayer::FullSaturation(const Color &c) {
    return (c.r == 0 || c.r == 255)
      && (c.g == 0 || c.g == 255)
      && (c.b == 0 || c.b == 255);
}

void Displayer::updatePWMBits() {
    if (!displayerOK) return;

    if (isChangeOrderDone()) return;    // no work to do, so no changing the canvas settings

    const uint8_t targetPWMBits = isExtremeColors() ? EXTREME_COLORS_PWM_BITS : defaultPWMBits;
    if (canvas->pwmbits() != targetPWMBits) {
      canvas->SetPWMBits(targetPWMBits);
    }
}

static std::string replaceNonPrintableCharacters(std::string str, const char repl_char) {
  // Iterate through each character in the string.
  for (unsigned i = 0; i < str.length(); i++) {
    // Check if the character is printable.
    if (!isprint(str[i])) {
      if (isatty(STDIN_FILENO)) {
        fprintf(stderr, "Replaced %02X with %c for display", str[i], repl_char);
      }

      str[i] = repl_char;
    }
  }
  return str;
}

void Displayer::startChangeOrder(const TextChangeOrder& aChangeOrder) {
  last_change_time = std::time(nullptr);

  currChangeOrder = aChangeOrder;

  // ensure text can be displayed
  constexpr char UNPRINTABLE_CHAR_REPL = '&';
  currChangeOrder.setString(replaceNonPrintableCharacters(currChangeOrder.getString(), UNPRINTABLE_CHAR_REPL));

  // reset scroll timing
  next_frame.tv_sec = 0;
  next_frame.tv_nsec = 0;

  scroll_direction = (currChangeOrder.getVelocity() <= 0) ? -1 : 1;
  const auto speed = static_cast<float>(fabs(static_cast<double>(currChangeOrder.getVelocity())));

  delay_speed_usec = (!currChangeOrder.isScrolling() || currChangeOrder.getSpacedFont().fontPtr == nullptr)
                         ? 0
                         : static_cast<int>(1000000.0 / speed / currChangeOrder.getSpacedFont().fontPtr->CharacterWidth('W'));

  if (currChangeOrder.isScrolling()) {
    if (currChangeOrder.getVelocityIsHorizontal()) {
      if (scroll_direction > 0) {
        // get width of text
        // not thread safe, since we are currently using the same offscreen_canvas we use in iota
        const int length = rgb_matrix::DrawText(offscreen_canvas, *currChangeOrder.getSpacedFont().fontPtr,
                                    x, y + currChangeOrder.getSpacedFont().fontPtr->baseline(),
                                    currChangeOrder.getForegroundColor(),
                                    nullptr,  // already filled with background color, so use transparency when drawing
                                    currChangeOrder.getText(), currChangeOrder.getSpacedFont().letterSpacing);
        x = -length;
      }
      else {
        x = canvas->width();
      }

      y = y_origin;
    }
    else {
      // scrolling vertically
      x = x_origin;

      if (scroll_direction > 0) {
        y = -currChangeOrder.getSpacedFont().fontPtr->height();
      }
      else {
        y = canvas->height();
      }
    }
    //printf("Scrolling request... startX=%d, startY=%d, vel=%f, dir=%d, %s, %s\n",x,y,currChangeOrder.getVelocity(),scroll_direction,currChangeOrder.getVelocityIsHorizontal() ? "horizontal" : "vertical",currChangeOrder.getVelocityIsSingleScroll() ? "single" : "repeating");// DEBUG
  }
  else {
    x = x_origin;
    y = y_origin;
  }
  setChangeDone(false);
}

inline void Displayer::setChangeDone(bool isChangeDone) {
  currChangeOrderDone = isChangeDone;
  last_change_time = std::time(nullptr);

  if (currChangeOrderDone && isatty(STDIN_FILENO)) {
    // Only give a message if we are interactive. If connected via pipe, be quiet
    printf("Displayed:%s\n",currChangeOrder.getText());
  }
}

void Displayer::dotCorners() {
  last_change_time = std::time(nullptr);

  // clear offline canvas
  offscreen_canvas->Fill(currChangeOrder.getBackgroundColor().r,
                         currChangeOrder.getBackgroundColor().g,
                         currChangeOrder.getBackgroundColor().b);

  rgb_matrix::Color red(255, 0, 0);
  offscreen_canvas->SetPixel(0,0, red.r, red.g, red.b);
  offscreen_canvas->SetPixel(0,offscreen_canvas->height()-1, red.r, red.g, red.b);
  offscreen_canvas->SetPixel(offscreen_canvas->width()-1,0, red.r, red.g, red.b);
  offscreen_canvas->SetPixel(offscreen_canvas->width()-1,offscreen_canvas->height()-1, red.r, red.g, red.b);

  // Swap the offscreen_canvas with canvas on vsync, avoids flickering
  offscreen_canvas = canvas->SwapOnVSync(offscreen_canvas);
}

void Displayer::iota() {
  if (!displayerOK) return;

  if (!currChangeOrderDone) {

    // clear offline canvas
    offscreen_canvas->Fill(currChangeOrder.getBackgroundColor().r,
                           currChangeOrder.getBackgroundColor().g,
                           currChangeOrder.getBackgroundColor().b);

    // draw text onto offline canvas.
    // length = holds how many pixels our text takes up
    const rgb_matrix::Font& currFont = *currChangeOrder.getSpacedFont().fontPtr;
    const int currLetterSpacing = currChangeOrder.getSpacedFont().letterSpacing;

    //printf("Loc(%d,%d)\n",x,y+currFont.baseline());//DEBUG
    const int length = rgb_matrix::DrawText(offscreen_canvas, currFont,
                                  x, y + currFont.baseline(),
                                  currChangeOrder.getForegroundColor(),
                                  nullptr,  // already filled with background color, so use transparency when drawing
                                  currChangeOrder.getText(), currLetterSpacing);

    // Make sure render-time delays are not influencing scroll-time
    if (currChangeOrder.isScrolling()) {
      if (next_frame.tv_sec == 0 && next_frame.tv_nsec == 0) {
        // First time. Start timer, but don't wait.
        clock_gettime(CLOCK_MONOTONIC, &next_frame);
      } else {
        add_micros(&next_frame, delay_speed_usec);
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_frame, nullptr);
      }
    }

    // Swap the offscreen_canvas with canvas on vsync, avoids flickering
    offscreen_canvas = canvas->SwapOnVSync(offscreen_canvas);

    // compute next position and/or done status
    if (currChangeOrder.isScrolling()) {
      if (currChangeOrder.getVelocityIsHorizontal()) {
        x += scroll_direction;
      }
      else {
        y += scroll_direction;
      }

      // handle wrapping
      if (currChangeOrder.getVelocityIsHorizontal()) {
        // wrap while scrolling horizontal
        if ((scroll_direction < 0 && x + length < 0) ||
           (scroll_direction > 0 && x > canvas->width())) {
          x = x_origin + ((scroll_direction > 0) ? -length : canvas->width());
        }
      }
      else {
        // wrap while scrolling vertical
        if ((scroll_direction < 0 && y + currFont.baseline() < 0) ||
           (scroll_direction > 0 && y > canvas->height())) {
          y = y_origin + ((scroll_direction > 0) ? -currFont.height() : canvas->height());
        }
      }

      // handle single scroll (non-continuous scrolling)
      if (currChangeOrder.getVelocityIsSingleScroll()) {
        if (currChangeOrder.getVelocityIsHorizontal()) {
          // stop horizontal scroll when reach origin position
          if ((scroll_direction < 0 && x <= x_origin) ||
              (scroll_direction > 0 && x >= x_origin)) {
            x = x_origin;
            setChangeDone();
          }
        }
        else {
          // stop vertical scroll when reach origin position
          if ((scroll_direction < 0 && y <= y_origin) ||
              (scroll_direction > 0 && y >= y_origin)) {
            y = y_origin;
            setChangeDone();
          }
        }
      }
    }
    else {
      // Text appeared.  Done.
      setChangeDone();
    }
  }
  else {
    // no active change order
    const time_t SECONDS_BLANK_TO_DECLARE_IDLE = 30;
    if (currChangeOrder.getString().empty()
        && std::time(nullptr) - last_change_time >= SECONDS_BLANK_TO_DECLARE_IDLE) {
      dotCorners();
    }
  }
}

Displayer::~Displayer() {
  // Finished. Shut down the RGB matrix.
  canvas->Clear();
  delete canvas;
}


