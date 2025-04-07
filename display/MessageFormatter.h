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
  MessageFormatter(Displayer& aDisplayer, TextChangeOrder aOrderFormat);

  void handleMessage(const Receiver::RawMessage& message);

  static std::string trimWhitespace(const std::string& str,
                                    const std::string& whitespace = " \t");

private:
  Displayer& myDisplayer;
  TextChangeOrder defaultOrderFormat;

  bool observedAlgeEventTypeChar;  // state information: true if have most recently seen intermediate location specifications and hence we'll ignore messages without them as copies
  int nextAlgeIntermediateLocationID;  // state information: next intermediate location to display if multiple messages received

  void handleAlgeMessage(const Receiver::RawMessage& message);
  void handleSimpleTextMessage(const Receiver::RawMessage& message);
  void handleUPLCFormattedMessage(const Receiver::RawMessage& message);
  TextChangeOrder buildDefaultChangeOrder(const char* text) const;
};


#endif //MESSAGEFORMATTER_H
