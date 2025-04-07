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

  bool handleMessage(const Receiver::RawMessage& message);  // returns true if message forwarded for display (versus disregarded)

  static std::string trimWhitespace(const std::string& str,
                                    const std::string& whitespace = " \t");

private:
  Displayer& myDisplayer;
  TextChangeOrder defaultOrderFormat;

  bool observedAlgeEventTypeChar;  // state information: true if have most recently seen intermediate location specifications and hence we'll ignore messages without them as copies
  int nextAlgeIntermediateLocationID;  // state information: next intermediate location to display if multiple messages received

  bool handleAlgeMessage(const Receiver::RawMessage& message);
  bool handleSimpleTextMessage(const Receiver::RawMessage& message);
  bool handleUPLCFormattedMessage(const Receiver::RawMessage& message);
  TextChangeOrder buildDefaultChangeOrder(const char* text) const;
};


#endif //MESSAGEFORMATTER_H
