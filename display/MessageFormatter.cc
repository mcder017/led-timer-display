//
// Created by WMcD on 12/9/2024.
//

#include "MessageFormatter.h"

#include <algorithm>
#include <string>
#include <unistd.h>  // for io on linux, specifically for STDIN_FILENO

static bool NO_VELOCITY_FOR_FIXED_TIMES = true;

MessageFormatter::MessageFormatter(Displayer& aDisplayer, TextChangeOrder aOrderFormat)
      : myDisplayer(aDisplayer), defaultOrderFormat(aOrderFormat),
        observedAlgeEventTypeChar(false), nextAlgeIntermediateLocationID(0) {

    // no further initialization needed
};

bool MessageFormatter::handleMessage(const Receiver::RawMessage& message) {
  switch (message.protocol) {
    case Receiver::Protocol::ALGE_DLINE:
      return handleAlgeMessage(message);
      break;

    case Receiver::Protocol::SIMPLE_TEXT:
      observedAlgeEventTypeChar = false;  // reset state variable
      nextAlgeIntermediateLocationID = 0;  // reset state variable
      return handleSimpleTextMessage(message);
      break;

    case Receiver::Protocol::UPLC_FORMATTED_TEXT:
      observedAlgeEventTypeChar = false;  // reset state variable
      nextAlgeIntermediateLocationID = 0;  // reset state variable
      return handleUPLCFormattedMessage(message);
      break;

    case Receiver::Protocol::UPLC_COMMAND:
      // these are not meant for display.  quietly decline
      fprintf(stderr, "UPLC Control message unexpectedly passed for formatting(%d):%s\n", message.protocol, message.data.c_str());
      break;

    default:
      fprintf(stderr, "Unknown message passed for formatting(%d):%s\n", message.protocol, message.data.c_str());
  };
  return false;
}

bool MessageFormatter::handleUPLCFormattedMessage(const Receiver::RawMessage& message) {
  TextChangeOrder newOrder(defaultOrderFormat);  // copy the default order format
  if (!newOrder.fromUPLCFormattedMessage(message.data)) {  // conversion failed
    fprintf(stderr, "UPLC format conversion failed\n");
    return false;
  }
  myDisplayer.startChangeOrder(newOrder);  // start the new order
  return true;
}

bool MessageFormatter::handleAlgeMessage(const Receiver::RawMessage& message) {
  // message data includes eol, and may be all whitespace
  if (message.data.length() < 20) {
    fprintf(stderr, "Message too short\n");
    return false;
  }

  // parse fields from the message

  const size_t BOARD_IDENTIFIER_POS = 0;  // 1st char of protocol = string index 0
  const std::string BOARD_CHAR_STRING = "ABCDEFGHIJ";
  const bool isBoardIdentifier = BOARD_CHAR_STRING.find(message.data.at(BOARD_IDENTIFIER_POS)) != std::string::npos;
  const int field_pos_shift = isBoardIdentifier ? 1 : 0; // certain protocol messages have time field at col 10 (string location 9)

  constexpr size_t BIB_FIELD_POS = 0;     // 1st char of protocol = string index 0; offset higher if Board ID
  constexpr size_t BIB_FIELD_LENGTH = 3;
  std::string bibField = message.data.substr(BIB_FIELD_POS+field_pos_shift, BIB_FIELD_LENGTH);  // first three char may be bib number, or blank
  bibField.erase(std::remove_if(bibField.begin(), bibField.end(), ::isspace), bibField.end());  // may now be empty string

  const size_t TIME_FIELD_POS = 8;    // 9th char of protocol = string index 8; offset higher if Board ID
  const size_t TIME_FIELD_LENGTH = 12;  // hh:mm:ss.zht but leading or trailing part may be whitespace
  std::string timeField = message.data.substr(TIME_FIELD_POS+field_pos_shift, TIME_FIELD_LENGTH);
  timeField = trimWhitespace(timeField);

  constexpr size_t RANK_FIELD_POS = 20;  // 21st char of protocol = string index 20; offset higher if Board ID
  constexpr size_t RANK_FIELD_LENGTH = 2;
  // may be rank number, or blank
  std::string rankField = message.data.length() > (RANK_FIELD_POS+field_pos_shift) ? message.data.substr(RANK_FIELD_POS+field_pos_shift, RANK_FIELD_LENGTH) : "";
  rankField = trimWhitespace(rankField);

  // event type char (in TDC 4000 events) only potentially present if no board ID char
  constexpr size_t EVENT_TYPE_POS = 3;  // 4th char of protocol = string index 3
  char eventTypeChar = isBoardIdentifier ? ' ' : message.data.at(EVENT_TYPE_POS);

  //constexpr size_t RUNNING_FLAG_POS_NO_BOARD_ID = 3;    // 4th char of protocol = string index 3
  constexpr size_t FRAC_SECONDS_SEP_POS_WITH_BOARD_ID = 17; // 18th char of protocol = string index 17
  constexpr char RUNNING_FLAG_CHAR = '.';

  const bool isBlankMessage = trimWhitespace(isBoardIdentifier ? message.data.substr(BOARD_IDENTIFIER_POS+1) : message.data).length() == 0;
  // handle both "usual" and "board ID" cases
  // in board ID case, the event flag field isn't used but no fractions of a second are recorded.  So look for separator, with non-blank seconds to ensure data present
  // in non-board ID case, look for event flag field
  const bool isStillRunningTime = 
    isBoardIdentifier ? (message.data.at(FRAC_SECONDS_SEP_POS_WITH_BOARD_ID-1) != ' '
                          && message.data.at(FRAC_SECONDS_SEP_POS_WITH_BOARD_ID) != '.')
                      : eventTypeChar == RUNNING_FLAG_CHAR;

  const bool isIntermediateOne = eventTypeChar == 'A';
  const bool isIntermediateTwoPlus = eventTypeChar == 'B'; // 'B' is provided for 2nd or later intermediate times
  const bool isRunTime = eventTypeChar == 'C' || eventTypeChar == 'K';  // 'C' is TDC4000, 'K' is Comet Stopwatch (with next char identifying source Comet)
  const bool isTotalTimeOrUnknown = eventTypeChar == 'D' || (!isIntermediateOne && !isIntermediateTwoPlus && !isRunTime && !isStillRunningTime); // expansive definition, includes message that is all-blank (after possible board id char) that effectively clears display

  // === UPDATE STATE VARIABLE ===
  // RTPro sends multiple ALGE protocol messages (to all boards!) if more than one board is defined, 
  // with first having a useful extra info character and later copies not having that char but have a board ID inserted at start.
  //
  // Since the protocol only indicates "first intermediate" and then "second-or-later intermediate", 
  // and since the RTPro stops sending any intermediate time snapshots if any sequential intermediate time point (1,2,3,...) is skipped during a run,
  // we maintain a state variable to track what intermediate we are on, across messages received.
  // AND rather than look for "our" board ID, we use the receipt of the full message meant for the "first" board to distinguish if we are on a new intermediate.
  if (!observedAlgeEventTypeChar) {
    if (!isBoardIdentifier && eventTypeChar != ' ') {
      observedAlgeEventTypeChar = true;
    }
  }
  else {
    // allow for possibility that (perhaps due to RTPro configuration change or disconnect/reconnect) we will not see intermediate locations in upcoming messages
    // and we do not want to throw away all future messages

    if (isBoardIdentifier && message.data.at(BOARD_IDENTIFIER_POS) == lastBoardIDChar) {   // seeing consecutive same board identifiers; are we no longer seeing event types?
      // reset flag, do not treat as duplicate message.
      observedAlgeEventTypeChar = false;  
    }
  }
  lastBoardIDChar = isBoardIdentifier ? message.data.at(BOARD_IDENTIFIER_POS) : ' ';  // save the last board ID char seen (if any)

  if (observedAlgeEventTypeChar) {
    if (isIntermediateOne) {
      // reset the intermediate location ID
      nextAlgeIntermediateLocationID = 1;
    }
    else if (isIntermediateTwoPlus && !isBoardIdentifier) { // new intermediate point (e.g. speed point),  not a board-ID-variation copy of a message already received
      // increment the intermediate location ID
      // (relies on the behavior that the RTPro will not send a message with a new intermediate location ID if the previous one was skipped, 
      //  so each run this state variable will reset to value 1, above)
      nextAlgeIntermediateLocationID++;
    }
    else if (!isBoardIdentifier && (isRunTime || isTotalTimeOrUnknown) && eventTypeChar != ' ') {
      // in an abundance of caution, reset the intermediate location ID if end of run known due to specific event type code flags
      nextAlgeIntermediateLocationID = 1;
    }
  }
  // ======

  // format the individual fields

  // while bib has a leading zero that is not the only character, remove the zero
  while (bibField.length() > 1 && bibField.at(0) == '0') {
    bibField.erase(bibField.begin());
  }

  // if time field starts with hours that are all zero, remove from string
  if (timeField.length() > 3
      && timeField.rfind("00:", 0) == 0
      && timeField.find_first_of(':',3) != std::string::npos) {  // found a later colon, so first match was hours
    timeField = timeField.substr(3, timeField.length() - 3);
  }

  // if time field starts with two digit hours or two digit minutes, and the first digit is zero, remove leading zero
  if (timeField.length() > 2
      && timeField.at(0) == '0'
      && timeField.at(2) == ':') {
    timeField = timeField.substr(1, timeField.length() - 1);
  }

  // if time is only seconds (and possibly fractions of second), remove whitespace and format as m:ss or m:ss.zht
  if (!timeField.empty()
      && timeField.find_first_of(':') == std::string::npos
      && timeField.find_first_of("01234567890") != std::string::npos) {
    char timeBuffer[TIME_FIELD_LENGTH];
    const size_t dotPos = timeField.find_first_of('.');
    if (dotPos != std::string::npos) {
      // has fractions of a second
      sprintf(timeBuffer, "0:%02d%s",
                std::stoi(timeField.substr(0, dotPos)),
                timeField.substr(dotPos).c_str());
    }
    else {
      // only seconds
      sprintf(timeBuffer, "0:%02d", std::stoi(timeField));
    }
    timeField = timeBuffer;
  }

  // The RTPro sends all board messages to all boards IDs,
  // not just individual messages to each board's IP.  The first message (with no board ID)
  // contains extra information (such as intermediate location) of which we are now making active use.
  //
  // Therefore, we are now discarding (ignoring) any messages with a board ID.  This avoids having
  // useful display (like the split location) disappearing instantly when the 2nd message (with board ID but no detail data) arrives.
  //
  // The hedge on this approach is that if we are only seeing messages with a board ID, then we don't ignore.
  if (isBoardIdentifier && observedAlgeEventTypeChar) {
    if (isatty(STDIN_FILENO)) {
      printf("Ignoring dupl msg\n");
    }
    return false;
  }

  // assemble the message to display
  if (isBlankMessage) {
    TextChangeOrder newOrder = buildDefaultChangeOrder(" ");  // clear display
    myDisplayer.startChangeOrder(newOrder);
  }
  else if (isIntermediateOne || isIntermediateTwoPlus) {  
    // intermediate time
    // RTPro does not have run lap split, just total split, available to display.  
    // (Run lap split is available to computer communication)
    const std::string text = //(bibField.empty() ? "" : bibField + "=") +
                             timeField
                             //+ (rankField.empty() ? "" : "[" + rankField + "]")
                             + " S"+std::to_string(nextAlgeIntermediateLocationID);
    TextChangeOrder newOrder = buildDefaultChangeOrder(text.c_str());
    if (NO_VELOCITY_FOR_FIXED_TIMES) newOrder.setVelocity(0);  // override velocity

    myDisplayer.startChangeOrder(newOrder);
  }
  else if (isStillRunningTime) {
    const std::string text = "[ " + timeField + " ]";
    myDisplayer.startChangeOrder(buildDefaultChangeOrder(text.c_str()));
}
  else if (isTotalTimeOrUnknown) {
    // combine bib, time, and rank if provided
    const std::string text = //(bibField.empty() ? "" : bibField + "=") +
                             timeField
                             + (rankField.empty() ? "" : "(" + rankField + ")");
    TextChangeOrder newOrder = buildDefaultChangeOrder(text.c_str());
    if (NO_VELOCITY_FOR_FIXED_TIMES) newOrder.setVelocity(0);  // override velocity

    myDisplayer.startChangeOrder(newOrder);
  }
  else if (isRunTime) { // if run2 or later, RTPro sends total time, then run time, then total time again (with delays in between).
    // combine bib, time, and rank if provided
    const std::string text = //(bibField.empty() ? "" : bibField + "=") +
                             timeField
                             + (rankField.empty() ? " Rn" : "(" + rankField + ")");
    TextChangeOrder newOrder = buildDefaultChangeOrder(text.c_str());
    if (NO_VELOCITY_FOR_FIXED_TIMES) newOrder.setVelocity(0);  // override velocity
    myDisplayer.startChangeOrder(newOrder);
  }
  else {
    // unsure why didn't filter as total time, but do a similar display
    // combine bib, time, and rank if provided
    const std::string text = //(bibField.empty() ? "" : bibField + "=") +
                             timeField
                             + (rankField.empty() ? "" : "[" + rankField + "]");
    TextChangeOrder newOrder = buildDefaultChangeOrder(text.c_str());
    if (NO_VELOCITY_FOR_FIXED_TIMES) newOrder.setVelocity(0);  // override velocity
    myDisplayer.startChangeOrder(newOrder);
  }
  return true;
}

bool MessageFormatter::handleSimpleTextMessage(const Receiver::RawMessage& message) {
  // forward the message string directly to the display, using default entrance parameters

  myDisplayer.startChangeOrder(buildDefaultChangeOrder(message.data.c_str()));
  return true;
}

TextChangeOrder MessageFormatter::buildDefaultChangeOrder(const char* text) const {
  TextChangeOrder newOrder(defaultOrderFormat);
  newOrder.setText(text);
  return newOrder;
}

std::string MessageFormatter::trimWhitespace(const std::string& str,
                                    const std::string& whitespace) {
  const auto strBegin = str.find_first_not_of(whitespace);
  if (strBegin == std::string::npos)
    return ""; // no content

  const auto strEnd = str.find_last_not_of(whitespace);
  const auto strRange = strEnd - strBegin + 1;

  return str.substr(strBegin, strRange);
}
