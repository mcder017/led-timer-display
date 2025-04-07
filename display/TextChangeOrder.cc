//
// Created by Wesley on 12/7/2024.
//

#include "TextChangeOrder.h"
#include "bdf-10x20-local.h"

#include "graphics.h"
#include <cmath>    // for fabs
#include <utility>
#include <stdexcept> // for std::exception

rgb_matrix::Font* SpacedFont::getDefaultFontPtr() {
    if (defaultFontPtr == nullptr) {
        // read built-in default font
        defaultFontPtr = new rgb_matrix::Font();
        if (!defaultFontPtr->ReadFont(BDF_10X20_STRING)) {
            fprintf(stderr, "Couldn't read default built-in 10x20 font\n");
            // we proceed on with a default empty or partially constructed font, and no other error signalling
        }
    }
    return SpacedFont::defaultFontPtr;
}

int SpacedFont::getDefaultLetterSpacing() {
    constexpr int DEFAULT_SPACING = -1;    // associated with default font
    return DEFAULT_SPACING;
}

// static variable initialization
rgb_matrix::Font* SpacedFont::defaultFontPtr = nullptr;
std::vector<SpacedFont> SpacedFont::registeredSpacedFonts;
int TextChangeOrder::xOriginDefault = 0;
int TextChangeOrder::yOriginDefault = 0;
std::vector<TextChangeOrder> TextChangeOrder::registeredTemplates;

TextChangeOrder::TextChangeOrder()
    :
    spacedFont(),
    foregroundColor(getDefaultForegroundColor()),
    backgroundColor(getDefaultBackgroundColor()),
    velocity(0.0f),
    velocityIsHorizontal(true),
    velocityScrollType(SINGLE_ONOFF),
    x_origin(xOriginDefault),
    y_origin(yOriginDefault),
    text()
{}


TextChangeOrder::TextChangeOrder(const char* aText)
    :
    spacedFont(),
    foregroundColor(getDefaultForegroundColor()),
    backgroundColor(getDefaultBackgroundColor()),
    velocity(0.0f),
    velocityIsHorizontal(true),
    velocityScrollType(SINGLE_ONOFF),
    x_origin(xOriginDefault),
    y_origin(yOriginDefault),
    text(aText)
    {}

TextChangeOrder::TextChangeOrder(std::string  aString)
    :
    spacedFont(),
    foregroundColor(getDefaultForegroundColor()),
    backgroundColor(getDefaultBackgroundColor()),
    velocity(0.0f),
    velocityIsHorizontal(true),
    velocityScrollType(SINGLE_ONOFF),
    x_origin(xOriginDefault),
    y_origin(yOriginDefault),
    text(std::move(aString))
{}

TextChangeOrder::TextChangeOrder(SpacedFont aSpacedFont, const char* aText)
    :
    spacedFont(aSpacedFont),
    foregroundColor(getDefaultForegroundColor()),
    backgroundColor(getDefaultBackgroundColor()),
    velocity(0.0f),
    velocityIsHorizontal(true),
    velocityScrollType(SINGLE_ONOFF),
    x_origin(xOriginDefault),
    y_origin(yOriginDefault),
    text(aText)
{}

rgb_matrix::Color TextChangeOrder::getDefaultForegroundColor() {
    return {255, 0, 0};    // red
}

rgb_matrix::Color TextChangeOrder::getDefaultBackgroundColor() {
    return {0, 0, 0};    // black
}

bool TextChangeOrder::isScrolling() const {
  const float eps = 0.0001f;
  return fabs((double)velocity) > eps;
}

bool TextChangeOrder::orderDoneHasEmptyDisplay() const {
    return text.empty()
            || (isScrolling() && velocityScrollType == SINGLE_ONOFF);  // scrolling (velocity not zero), but this scroll type ends with empty display
}

std::string TextChangeOrder::toUPLCFormattedMessage() const {
    std::string result = UPLC_FORMATTED_PREFIX;

    // look for registered font key
    // support max of 10 fonts, 0-9
    for (int i = 0; i < 10 && i < SpacedFont::getNumRegisteredFonts(); ++i) {
        if (spacedFont.equals(SpacedFont::getRegisteredSpacedFont(i))) {
            result += "!" + std::to_string(i);  // font prefix and index
            break;
        }
    }

    // add foreground and background colors
    {
        constexpr int MAX_COLOR_CONVERSION_LENGTH = 20;
        char formattedColorBuffer[MAX_COLOR_CONVERSION_LENGTH];
        sprintf(formattedColorBuffer, "F%02x%02x%02xB%02x%02x%02x", 
                foregroundColor.r, foregroundColor.g, foregroundColor.b,
                backgroundColor.r, backgroundColor.g, backgroundColor.b
            );
        result += formattedColorBuffer;
    }

    // add velocity
    {
        constexpr int MAX_VELOCITY_CONVERSION_LENGTH = 20;
        char formattedVelocityBuffer[MAX_VELOCITY_CONVERSION_LENGTH];
        sprintf(formattedVelocityBuffer, "V%+05.1f", velocity);
        result += formattedVelocityBuffer;
    }

    // add scrolling direction and type
    result += "D" + std::to_string((int)velocityIsHorizontal);
    result += "S" + std::to_string((int)velocityScrollType);

    // add text
    result += "=" + text;

    result += UPLC_FORMATTED_SUFFIX;
    return result;
}

bool TextChangeOrder::fromUPLCFormattedMessage(std::string messageString) {
    if (messageString.substr(0, UPLC_FORMATTED_PREFIX.length()) != UPLC_FORMATTED_PREFIX
        || messageString.substr(messageString.length()-UPLC_FORMATTED_SUFFIX.length(), UPLC_FORMATTED_SUFFIX.length()) != UPLC_FORMATTED_SUFFIX) {
        // no action, format not recognized
        fprintf(stderr, "At conversion, UPLC formatted prefix %s or suffix newline not found:%s\n",
                UPLC_FORMATTED_PREFIX.c_str(), messageString.c_str());   
        return false;  // not a UPLC formatted message
    }

    int charIndex = UPLC_FORMATTED_PREFIX.length();
    while ((int)messageString.length() > charIndex) {
        const char c = messageString.at(charIndex);
        switch (c) {
            case '!': {  // font prefix
                charIndex++;

                try {
                    // support max of 10 fonts, 0-9
                    const int fontIndex = std::stoi(messageString.substr(charIndex, 1));
                    if (fontIndex >= 0 && fontIndex < SpacedFont::getNumRegisteredFonts()) {
                        spacedFont = SpacedFont::getRegisteredSpacedFont(fontIndex);
                    }
                    else {
                        fprintf(stderr, "At conversion, UPLC formatted font index %d not found:%s\n",
                                fontIndex, messageString.c_str());   
                    }
                }
                catch (const std::exception& e) {
                    // stoi failed.  keep current font   
                    return false;
                }
                charIndex++;  // skip over the font index
                break;
            }
            case 'F': {  // foreground color
                charIndex++;
                int fr,fg,fb;
                if (sscanf(messageString.substr(charIndex, 2).c_str(), "%x", &fr) != 1
                    || sscanf(messageString.substr(charIndex+2, 2).c_str(), "%x", &fg) != 1
                    || sscanf(messageString.substr(charIndex+4, 2).c_str(), "%x", &fb) != 1) {
                    foregroundColor = getDefaultForegroundColor();  // reset to default if error
                    fprintf(stderr, "At conversion, UPLC formatted foreground color %s not found:%s\n",
                            messageString.substr(charIndex, 6).c_str(), messageString.c_str());
                    return false;
                }
                else {
                    foregroundColor = rgb_matrix::Color((uint8_t)fr,(uint8_t)fg,(uint8_t)fb);  // set color
                }
                charIndex += 6;  // skip over the color codes
                break;
            }
            case 'B': {  // background color
                charIndex++;
                int br,bg,bb;
                if (sscanf(messageString.substr(charIndex, 2).c_str(), "%x", &br) != 1
                    || sscanf(messageString.substr(charIndex+2, 2).c_str(), "%x", &bg) != 1
                    || sscanf(messageString.substr(charIndex+4, 2).c_str(), "%x", &bb) != 1) {
                    backgroundColor = getDefaultBackgroundColor();  // reset to default if error
                    fprintf(stderr, "At conversion, UPLC formatted background color %s not found:%s\n",
                            messageString.substr(charIndex, 6).c_str(), messageString.c_str());
                    return false;
                }
                else {
                    backgroundColor = rgb_matrix::Color((uint8_t)br,(uint8_t)bg,(uint8_t)bb);  // set color
                }
                charIndex += 6;  // skip over the color codes
                break;
            }
            case 'V': {  // velocity
                charIndex++;
                if (sscanf(messageString.substr(charIndex, 5).c_str(), "%f", &velocity) != 1) {
                    velocity = 0.0f;  // reset to default if error
                    fprintf(stderr, "At conversion, UPLC formatted velocity %s not found:%s\n",
                            messageString.substr(charIndex, 5).c_str(), messageString.c_str());
                    return false;
                }
                charIndex += 5;  // skip over the velocity data
                break;
            }
            case 'D': {  // horizontal scrolling
                charIndex++;
                if (sscanf(messageString.substr(charIndex, 1).c_str(), "%d", (int*)&velocityIsHorizontal) != 1) {
                    velocityIsHorizontal = true;  // reset to default if error
                    fprintf(stderr, "At conversion, UPLC formatted horizontal scroll %s not found:%s\n",
                            messageString.substr(charIndex, 1).c_str(), messageString.c_str());
                    return false;
                }
                charIndex++;
                break;
            }
            case 'S': {  // scroll type
                charIndex++;
                if (sscanf(messageString.substr(charIndex, 1).c_str(), "%d", (int*)&velocityScrollType) != 1) {
                    velocityScrollType = SINGLE_ONOFF;  // reset to default if error
                    fprintf(stderr, "At conversion, UPLC formatted scroll type %s not found:%s\n",
                            messageString.substr(charIndex, 1).c_str(), messageString.c_str());
                    return false;
                }
                charIndex++;
                break;
            }
            case '=': {  // text string
                charIndex++;
                text = messageString.substr(charIndex, messageString.length()-charIndex-UPLC_FORMATTED_SUFFIX.length());   // remainder of string, except for suffix, is the message text
                charIndex = messageString.length();
                return true;   // done processing all characters
            }
            default:
                fprintf(stderr, "At conversion, UPLC formatted with unknown format code %c:%s\n",
                        c, messageString.c_str());   
                //charIndex++;  // skip over the unknown format code, keep searching (may see multiple errors)
                return false;
        }

    }
    return true;  // done processing all characters
}