//
// Created by WMcD on 12/9/2024.
//

#include "MessageFormatter.h"

#include <algorithm>
#include <string>

static bool NO_VELOCITY_FOR_FIXED_TIMES = true;

MessageFormatter::MessageFormatter(Displayer& aDisplayer, rgb_matrix::Font* aFontPtr, int aLetterSpacing,
                                   rgb_matrix::Color& fgColor, rgb_matrix::Color& bgColor,
                                   float velocity, bool scroll_horizontal, TextChangeOrder::ScrollType scroll_type)
      : myDisplayer(aDisplayer), defaultVelocity(velocity), default_horizontal(scroll_horizontal), default_scroll_type(scroll_type) {

    defaultSpacedFont.fontPtr = aFontPtr;
    defaultSpacedFont.letterSpacing = aLetterSpacing;
    defaultForegroundColor = fgColor;
    defaultBackgroundColor = bgColor;
};

void MessageFormatter::handleMessage(const Receiver::RawMessage& message) {
  switch (message.protocol) {
    case Receiver::Protocol::ALGE_DLINE:
      handleAlgeMessage(message);
      break;

    case Receiver::Protocol::SIMPLE_TEXT:
      handleSimpleTextMessage(message);
      break;

    default:
      fprintf(stderr, "Unknown message passed for formatting(%d):%s\n", message.protocol, message.data.c_str());
  };
}

void MessageFormatter::handleAlgeMessage(const Receiver::RawMessage& message) {
  // message data includes eol, and may be all whitespace
  if (message.data.length() < 20) {
    fprintf(stderr, "Message too short\n");
    return;
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
  // handle both "usual" and "board ID" case
  // in board ID case, the event flag field isn't used but no fractions of a second are recorded.  So look for separator, with non-blank seconds to ensure data present
  // in non-board ID case, look for event flag field
  const bool isStillRunningTime =
    isBoardIdentifier ? (message.data.at(FRAC_SECONDS_SEP_POS_WITH_BOARD_ID-1) != ' '
                          && message.data.at(FRAC_SECONDS_SEP_POS_WITH_BOARD_ID) != '.')
                      : eventTypeChar == RUNNING_FLAG_CHAR;

  const bool isIntermediateOne = eventTypeChar == 'A';
  const bool isIntermediateTwo = eventTypeChar == 'B'; // 'B' is provided for 2nd or later intermediate times
  const bool isRunTime = eventTypeChar == 'C' || eventTypeChar == 'K';  // 'C' is TDC4000, 'K' is Comet Stopwatch (with next char identifying source Comet)
  const bool isTotalTime = eventTypeChar == 'D' || (!isIntermediateOne && !isIntermediateTwo && !isStillRunningTime);

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

  // assemble the message to display

  if (isStillRunningTime) {
    const std::string text = "[ " + timeField + " ]";
    myDisplayer.startChangeOrder(buildDefaultChangeOrder(text.c_str()));
  }
  else if (isTotalTime) {
    // combine bib, time, and rank if provided
    const std::string text = //(bibField.empty() ? "" : bibField + "=") +
                             timeField
                             + (rankField.empty() ? "" : "(" + rankField + ")");
    TextChangeOrder newOrder = buildDefaultChangeOrder(text.c_str());
    if (NO_VELOCITY_FOR_FIXED_TIMES) newOrder.setVelocity(0);  // override velocity
    myDisplayer.startChangeOrder(newOrder);
  }
  else if (isRunTime) {
    // combine bib, time, and rank if provided
    const std::string text = //(bibField.empty() ? "" : bibField + "=") +
                             timeField
                             + (rankField.empty() ? "" : "/ " + rankField);
    TextChangeOrder newOrder = buildDefaultChangeOrder(text.c_str());
    if (NO_VELOCITY_FOR_FIXED_TIMES) newOrder.setVelocity(0);  // override velocity
    myDisplayer.startChangeOrder(newOrder);
  }
  else {
    // intermediate time, or else
    // unsure why didn't filter as total time, but do a similar display
    // combine bib, time, and rank if provided
    const std::string text = //(bibField.empty() ? "" : bibField + "=") +
                             timeField
                             + (rankField.empty() ? "" : "[" + rankField + "]");
    TextChangeOrder newOrder = buildDefaultChangeOrder(text.c_str());
    if (NO_VELOCITY_FOR_FIXED_TIMES) newOrder.setVelocity(0);  // override velocity
    myDisplayer.startChangeOrder(newOrder);
  }
}

void MessageFormatter::handleSimpleTextMessage(const Receiver::RawMessage& message) {
  // forward the message string directly to the display, using default entrance parameters

  myDisplayer.startChangeOrder(buildDefaultChangeOrder(message.data.c_str()));

}

TextChangeOrder MessageFormatter::buildDefaultChangeOrder(const char* text) const {
  TextChangeOrder newOrder(defaultSpacedFont, text);
  newOrder.setVelocity(defaultVelocity);
  newOrder.setForegroundColor(defaultForegroundColor);
  newOrder.setBackgroundColor(defaultBackgroundColor);
  newOrder.setVelocityIsHorizontal(default_horizontal);
  newOrder.setVelocityScrollType(default_scroll_type);
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
