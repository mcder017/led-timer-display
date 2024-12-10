//
// Created by Wesley on 12/7/2024.
//

#include "TextChangeOrder.h"
#include "bdf-10x20-local.h"

#include "graphics.h"
#include <cmath>    // for fabs
#include <utility>

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

rgb_matrix::Font* SpacedFont::defaultFontPtr = nullptr;

TextChangeOrder::TextChangeOrder()
    :
    spacedFont(),
    foregroundColor(getDefaultForegroundColor()),
    backgroundColor(getDefaultBackgroundColor()),
    velocity(0.0f),
    velocityIsHorizontal(true),
    velocityScrollType(SINGLE_ONOFF),
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
