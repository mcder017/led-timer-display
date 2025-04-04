//
// Created by WMcD on 12/9/2024.
//

#ifndef MESSAGEFORMATTER_H
#define MESSAGEFORMATTER_H

#include "Displayer.h"
#include "Receiver.h"
#include "TextChangeOrder.h"

class MessageFormatter {
public:
  MessageFormatter(Displayer& aDisplayer, rgb_matrix::Font* aFontPtr, int aLetterSpacing,
                    rgb_matrix::Color& fgColor, rgb_matrix::Color& bgColor,
                    float velocity, bool scroll_horizontal, TextChangeOrder::ScrollType scroll_type);

  void handleMessage(const Receiver::RawMessage& message);

  static std::string trimWhitespace(const std::string& str,
                                    const std::string& whitespace = " \t");

private:
  Displayer& myDisplayer;
  SpacedFont defaultSpacedFont;
  rgb_matrix::Color defaultForegroundColor;
  rgb_matrix::Color defaultBackgroundColor;
  float defaultVelocity;
  bool default_horizontal;
  TextChangeOrder::ScrollType default_scroll_type;

  bool observedEventTypeChar;  // true if have seen intermediate location specifications and hence we'll ignore messages without them as copies
  int nextIntermediateLocationID;  // next intermediate location to display if multiple messages received

  void handleAlgeMessage(const Receiver::RawMessage& message);
  void handleSimpleTextMessage(const Receiver::RawMessage& message);
  TextChangeOrder buildDefaultChangeOrder(const char* text) const;
};


#endif //MESSAGEFORMATTER_H
