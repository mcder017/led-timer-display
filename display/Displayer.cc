//
// Created by WMcD on 12/7/2024.
//

#include "Displayer.h"

#include "led-matrix.h"
#include "graphics.h"

#include <time.h>    // for monitoring clock for steady scrolling
#include <math.h>    // for fabs

#define EXTREME_COLORS_PWM_BITS 1

using namespace rgb_matrix;

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


Displayer::Displayer(RGBMatrix::Options& aMatrix_options, rgb_matrix::RuntimeOptions& aRuntime_opt)
    : x_origin(0),
      y_origin(0),

      currChangeOrder(),
      currChangeOrderDone(true),
      x(0),
      y(0),
      scroll_direction(0),
      delay_speed_usec(0)
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

// TODO initially, and if unused for "a while", display dots in the four corners to clarify display is still on (if flag)

void Displayer::startChangeOrder(const TextChangeOrder aChangeOrder) {
  currChangeOrder = aChangeOrder;

  // reset scroll timing
  next_frame.tv_sec = 0;
  next_frame.tv_nsec = 0;

  scroll_direction = (currChangeOrder.getVelocity() >= 0) ? -1 : 1;
  const float speed = fabs(currChangeOrder.getVelocity());

  delay_speed_usec = (currChangeOrder.isScrolling() || currChangeOrder.getSpacedFont().fontPtr == nullptr)
                         ? 0
                         : 1000000 / speed / currChangeOrder.getSpacedFont().fontPtr->CharacterWidth('W');

  x = (currChangeOrder.getVelocityIsHorizontal() && scroll_direction < 0) ? canvas->width() : x_origin;
  y = (!currChangeOrder.getVelocityIsHorizontal() && scroll_direction < 0) ? canvas->height() : y_origin;

  currChangeOrderDone = false;
}

void Displayer::iota() {
  if (!displayerOK) return;

  if (!currChangeOrderDone) {

    // clear offline canvas
    offscreen_canvas->Fill(currChangeOrder.getForegroundColor().r,
                           currChangeOrder.getForegroundColor().g,
                           currChangeOrder.getForegroundColor().b);

    // draw text onto offline canvas.
    // length = holds how many pixels our text takes up
    const rgb_matrix::Font& currFont = *currChangeOrder.getSpacedFont().fontPtr;
    const int currLetterSpacing = currChangeOrder.getSpacedFont().letterSpacing;

    const int length = rgb_matrix::DrawText(offscreen_canvas, currFont,
                                  x, y + currFont.baseline(),
                                  currChangeOrder.getForegroundColor(),
                                  nullptr,
                                  currChangeOrder.getText(), currLetterSpacing);

    // Make sure render-time delays are not influencing scroll-time
    if (currChangeOrder.isScrolling()) {
      if (next_frame.tv_sec == 0 && next_frame.tv_nsec == 0) {
        // First time. Start timer, but don't wait.
        clock_gettime(CLOCK_MONOTONIC, &next_frame);
      } else {
        add_micros(&next_frame, delay_speed_usec);  // TBD could make either Receiver or Displayer a process so slow scroll speed doesn't affect Receiver
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_frame, NULL);
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
          y = y_origin + ((scroll_direction > 0) ? -currFont.baseline() : canvas->height());
        }
      }

      // handle single scroll (non-continuous scrolling)
      if (!currChangeOrder.getVelocityIsSingleScroll()) {
        if (currChangeOrder.getVelocityIsHorizontal()) {
          // stop horizontal scroll when reach origin position
          if ((scroll_direction < 0 && x <= x_origin) ||
              (scroll_direction > 0 && x >= x_origin)) {
            x = x_origin;
            currChangeOrderDone = true;
          }
        }
        else {
          // stop vertical scroll when reach origin position
          if ((scroll_direction < 0 && y <= y_origin) ||
              (scroll_direction > 0 && y >= y_origin)) {
            y = y_origin;
            currChangeOrderDone = true;
          }
        }
      }
    }
    else {
      // Text appeared.  Done.
      currChangeOrderDone = true;
    }
  }
}

Displayer::~Displayer() {
  // Finished. Shut down the RGB matrix.
  canvas->Clear();
  delete canvas;
}


