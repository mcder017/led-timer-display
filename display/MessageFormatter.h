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
                    float velocity);

  void handleMessage(Receiver::RawMessage message);

private:
  Displayer& myDisplayer;
  SpacedFont defaultSpacedFont;
  rgb_matrix::Color defaultForegroundColor;
  rgb_matrix::Color defaultBackgroundColor;
  float defaultVelocity;

  void handleAlgeMessage(Receiver::RawMessage message);
  void handleInternalErrMessage(Receiver::RawMessage message);
};


#endif //MESSAGEFORMATTER_H
