//
// Created by WMcD on 12/9/2024.
//

#include "MessageFormatter.h"

#include <algorithm>
#include <string>

static bool NO_VELOCITY_FOR_FIXED_TIMES = true;

MessageFormatter::MessageFormatter(Displayer& aDisplayer, rgb_matrix::Font* aFontPtr, int aLetterSpacing,
                                   rgb_matrix::Color& fgColor, rgb_matrix::Color& bgColor,
                                   float velocity)
      : myDisplayer(aDisplayer), defaultVelocity(velocity) {

    defaultSpacedFont.fontPtr = aFontPtr;
    defaultSpacedFont.letterSpacing = aLetterSpacing;
    defaultForegroundColor = fgColor;
    defaultBackgroundColor = bgColor;
};

void MessageFormatter::handleMessage(Receiver::RawMessage message) {
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

void MessageFormatter::handleAlgeMessage(Receiver::RawMessage message) {
  const size_t BIB_FIELD_LENGTH = 3;
  std::string bibField = message.data.substr(0, BIB_FIELD_LENGTH);  // first three char may be bib number, or blank
  bibField.erase(std::remove_if(bibField.begin(), bibField.end(), ::isspace), bibField.end());  // may now be empty string

  const size_t RUNNING_FLAG_POS = 3;  // 4th char of protocol = string index 3
  const char RUNNING_FLAG_CHAR = '.';
  const bool isRunningTime = message.data.at(RUNNING_FLAG_POS) == RUNNING_FLAG_CHAR;

  const size_t TIME_FIELD_POS = 8;    // 9th char of protocol = string index 8
  const size_t TIME_FIELD_LENGTH = 12;  // hh:mm:ss.zht but leading or trailing part may be whitespace
  std::string timeField = message.data.substr(TIME_FIELD_POS, TIME_FIELD_LENGTH);
  timeField.erase(std::remove_if(timeField.begin(), timeField.end(), ::isspace), timeField.end());

  const size_t RANK_FIELD_POS = 20;  // 21st char of protocol = string index 20
  const size_t RANK_FIELD_LENGTH = 2;
  std::string rankField = message.data.substr(RANK_FIELD_POS, RANK_FIELD_LENGTH);  // may be rank number, or blank
  rankField.erase(std::remove_if(rankField.begin(), rankField.end(), ::isspace), rankField.end());

  // if time field starts with hours that are all zero, remove from string
  if (timeField.rfind("00:", 0) == 0
      && timeField.find_first_of(':',3) != std::string::npos) {  // found a later colon, so first match was hours
    timeField = timeField.substr(3, timeField.length() - 3);
  }

  // if time field starts with two digit hours or two digit minutes, and the first digit is zero, remove leading zero
  if (timeField.at(0) == '0' && timeField.at(2) == ':') {
    timeField = timeField.substr(1, timeField.length() - 1);
  }

  // if time is only seconds, format as m:ss
  if (timeField.find_first_of(':') == std::string::npos) {
    char timeBuffer[TIME_FIELD_LENGTH];
    sprintf(timeBuffer, "0:%02d", std::stoi(timeField));
    timeField = timeBuffer;
  }

  if (isRunningTime) {
    const std::string text = "[ " + timeField + " ]";
    TextChangeOrder newOrder(defaultSpacedFont, text.c_str());
    newOrder.setVelocity(defaultVelocity);
    newOrder.setForegroundColor(defaultForegroundColor);
    newOrder.setBackgroundColor(defaultBackgroundColor);
    myDisplayer.startChangeOrder(newOrder);
  }
  else {
    // combine bib, time, and rank if provided
    const std::string text = (bibField.length() == 0 ? "" : bibField + "=")
                             + timeField
                             + (rankField.length() == 0 ? "" : "(" + rankField + ")");
    TextChangeOrder newOrder(defaultSpacedFont, text.c_str());
    newOrder.setVelocity(NO_VELOCITY_FOR_FIXED_TIMES ? 0 : defaultVelocity);
    newOrder.setForegroundColor(defaultForegroundColor);
    newOrder.setBackgroundColor(defaultBackgroundColor);
    myDisplayer.startChangeOrder(newOrder);
  }
}

void MessageFormatter::handleSimpleTextMessage(Receiver::RawMessage message) {
  // forward the message string directly to the display, using default entrance parameters

  TextChangeOrder newOrder(defaultSpacedFont, message.data.c_str());
  newOrder.setVelocity(defaultVelocity);
  newOrder.setForegroundColor(defaultForegroundColor);
  newOrder.setBackgroundColor(defaultBackgroundColor);
  myDisplayer.startChangeOrder(newOrder);

}
