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
    : allowIdleMarkers(true),
      isIdle(false),
      isDisconnected(false),
      markedDisconnected(false),

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
        if (offscreen_canvas == nullptr) {
          displayerOK = false;
          fprintf(stderr, "Error creating offscreen_canvas\n");
        }
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
      offscreen_canvas->SetPWMBits(targetPWMBits);
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

  // depending on colors and brightness, use fewer pwm bits (for faster refresh)
  updatePWMBits();

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

  if (currChangeOrder.isScrolling()) {  // velocity not zero
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

      y = currChangeOrder.getYOrigin();
    }
    else {
      // scrolling vertically
      x = currChangeOrder.getXOrigin();

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
    x = currChangeOrder.getXOrigin();
    y = currChangeOrder.getYOrigin();
  }
  setChangeDone(false);
  isIdle = false;  // reset idle timer, regardless of whether message is blank or not (so idle markers can be re-added if appropriate)
}

inline void Displayer::setChangeDone(bool isChangeDone) {
  currChangeOrderDone = isChangeDone;
  last_change_time = std::time(nullptr);

  if (currChangeOrderDone && isatty(STDIN_FILENO)) {
    // Only give a message if we are interactive. If connected via pipe, be quiet
    printf("Displayed:%s\n",currChangeOrder.getText());
  }
}

void Displayer::dotCorners(const rgb_matrix::Color &dotColor, rgb_matrix::Canvas *aCanvas) {
  //last_change_time = std::time(nullptr);  // dotting corners with markers does NOT count as "no longer idle"

  aCanvas->SetPixel(0,0, dotColor.r, dotColor.g, dotColor.b);
  aCanvas->SetPixel(0,offscreen_canvas->height()-1, dotColor.r, dotColor.g, dotColor.b);
  aCanvas->SetPixel(offscreen_canvas->width()-1,0, dotColor.r, dotColor.g, dotColor.b);
  aCanvas->SetPixel(offscreen_canvas->width()-1,offscreen_canvas->height()-1, dotColor.r, dotColor.g, dotColor.b);

}

void Displayer::iota() {
  if (!displayerOK) return;

  const  rgb_matrix::Color MARK_DISCONNECTED_COLOR(0,255,0); // use an extreme color to avoid messing up pwmbits
  const  rgb_matrix::Color UNMARK_DISCONNECTED_COLOR(0,0,0); // since we can't query other dot colors, just go black
  const  rgb_matrix::Color MARK_IDLE_COLOR(255,0,0); // use an extreme color to avoid messing up pwmbits
  constexpr time_t SECONDS_BLANK_TO_DECLARE_IDLE = 5;

  if (!currChangeOrderDone || isContinuousScroll()) {
    // restart idle timer unless an "empty" message is in progress (still scrolling) on the display over multiple iota calls
    if (!currChangeOrder.orderDoneHasEmptyDisplay()) {  // non-empty message still in progress, so keep resetting the idle timer
      isIdle = false;
    }

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

    // if asked, overlay "disconnected" marker dots on whatever is displayed
    // regardless, update flag indicating whether the dots are applied
    if (isDisconnected) {
      dotCorners(MARK_DISCONNECTED_COLOR, offscreen_canvas);
    }
    markedDisconnected = isDisconnected;

    // Swap the offscreen_canvas with canvas on vsync, avoids flickering
    offscreen_canvas = canvas->SwapOnVSync(offscreen_canvas);

    // compute next position and/or done status
    if (currChangeOrder.isScrolling()) {
      if (currChangeOrder.getVelocityIsHorizontal()) {
        x += scroll_direction;  // unit magnitude
      }
      else {
        y += scroll_direction;  // unit magnitude
      }


      // handle scrolling
      switch (currChangeOrder.getVelocityScrollType()) {
        case TextChangeOrder::CONTINUOUS:
          // handle wrapping
          if (currChangeOrder.getVelocityIsHorizontal()) {
            // wrap while scrolling horizontal
            if ((scroll_direction < 0 && x + length < 0) ||
               (scroll_direction > 0 && x > canvas->width())) {
              x = currChangeOrder.getXOrigin() + ((scroll_direction > 0) ? -length : canvas->width());
              if (!currChangeOrderDone) setChangeDone();  // completed at least one cycle of scrolling
            }
          }
          else {
            // wrap while scrolling vertical
            if ((scroll_direction < 0 && y + currFont.baseline() < 0) ||
               (scroll_direction > 0 && y > canvas->height())) {
              y = currChangeOrder.getYOrigin() + ((scroll_direction > 0) ? -currFont.height() : canvas->height());
              if (!currChangeOrderDone) setChangeDone();  // completed at least one cycle of scrolling
            }
          }
          break;

        case TextChangeOrder::SINGLE_ON:
          if (currChangeOrder.getVelocityIsHorizontal()) {
            // stop horizontal scroll when reach origin position
            if ((scroll_direction < 0 && x < currChangeOrder.getXOrigin()) ||
                (scroll_direction > 0 && x > currChangeOrder.getXOrigin())) {
              x = currChangeOrder.getXOrigin();
              setChangeDone();
            }
          }
          else {
            // stop vertical scroll when reach origin position
            if ((scroll_direction < 0 && y < currChangeOrder.getYOrigin()) ||
                (scroll_direction > 0 && y > currChangeOrder.getYOrigin())) {
              y = currChangeOrder.getYOrigin();
              setChangeDone();
            }
          }
          break;

        case TextChangeOrder::SINGLE_ONOFF:
          if (currChangeOrder.getVelocityIsHorizontal()) {
            // stop horizontal scroll when exit far side
            if ((scroll_direction < 0 && x < -length) ||
                (scroll_direction > 0 && x > canvas->width())) {
              x = canvas->width()+1;  // off screen
              setChangeDone();
            }
          }
          else {
            // stop vertical scroll when exit top or bottom
            if ((scroll_direction < 0 && y < -currFont.height()) ||
                (scroll_direction > 0 && y > canvas->height())) {
              y = canvas->height()+1; // off screen
              setChangeDone();
            }
          }
        break;

        default:
          //no action
          break;
      }
    }
    else {
      // Text appeared.  Done.
      setChangeDone();
    }
  }

  if (currChangeOrderDone) {  // no active change order (although continuous scrolling may be ongoing)
    // if requested, and idled with blank display for length of time, mark dots on corners
    if (allowIdleMarkers
        && !isIdle
        && currChangeOrder.orderDoneHasEmptyDisplay()
        && std::time(nullptr) - last_change_time >= SECONDS_BLANK_TO_DECLARE_IDLE) {

      isIdle = true;
      dotCorners(MARK_IDLE_COLOR, canvas);  // directly change display to show dots
      if (isatty(STDIN_FILENO)) {
        // Only give a message if we are interactive. If connected via pipe, be quiet
        printf("Idle marked\n");
      }

    }

    // if change in whether disconnected dots should be visible, update display and internal flag indicating presence of dots
    if (isDisconnected != markedDisconnected) {
      // apply new dot color over the top of whatever was displayed
      const rgb_matrix::Color applyColor = isDisconnected ? MARK_DISCONNECTED_COLOR : UNMARK_DISCONNECTED_COLOR;
      dotCorners(applyColor, canvas);  // directly change display to show new dot color
      markedDisconnected = isDisconnected;
      if (isatty(STDIN_FILENO)) {
        // Only give a message if we are interactive. If connected via pipe, be quiet
        printf("Known disconnected marked\n");
      }
    }
  }
}

Displayer::~Displayer() {
  // Finished. Shut down the RGB matrix.
  canvas->Clear();
  delete canvas;
}


