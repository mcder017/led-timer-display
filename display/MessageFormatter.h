//
// Created by WMcD on 12/9/2024.
//

#ifndef MESSAGEFORMATTER_H
#define MESSAGEFORMATTER_H

#include "displayer.h"
#include "receiver.h"

class MessageFormatter {
public:
  MessageFormatter(Displayer& aDisplayer) : myDisplayer(aDisplayer) {};

  void handleMessage(Receiver::RawMessage message);

private:
  Displayer& myDisplayer;

  void handleAlgeMessage(Receiver::RawMessage message);
  void handleInternalErrMessage(Receiver::RawMessage message);
};



#endif //MESSAGEFORMATTER_H
