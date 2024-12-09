//
// Created by WMcD on 12/9/2024.
//

#ifndef MESSAGEFORMATTER_H
#define MESSAGEFORMATTER_H

#include "Displayer.h"
#include "Receiver.h"

class MessageFormatter {
public:
  MessageFormatter(Displayer& aDisplayer, rgb_matrix::Font& aFont, const int aLetterSpacing)
      : myDisplayer(aDisplayer), defaultFont(aFont), defaultLetterSpacing(aLetterSpacing) {};

  void handleMessage(Receiver::RawMessage message);

private:
  Displayer& myDisplayer;
  rgb_matrix::Font& defaultFont;
  int defaultLetterSpacing;

  void handleAlgeMessage(Receiver::RawMessage message);
  void handleInternalErrMessage(Receiver::RawMessage message);
};



#endif //MESSAGEFORMATTER_H
