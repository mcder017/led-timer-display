//
// Created by WMcD on 12/7/2024.
//

#ifndef TEXTCHANGEORDER_H
#define TEXTCHANGEORDER_H

#include <string>
#include "graphics.h"

struct SpacedFont {
    SpacedFont() : fontPtr(getDefaultFontPtr()), letterSpacing(getDefaultLetterSpacing()) {}
    SpacedFont(rgb_matrix::Font* aFontPtr, int aLetterSpacing) : fontPtr(aFontPtr), letterSpacing(aLetterSpacing) {}
    SpacedFont(const SpacedFont& aSpacedFont) = default;

    rgb_matrix::Font* fontPtr;
    int letterSpacing;

    static rgb_matrix::Font* getDefaultFontPtr();
    static int getDefaultLetterSpacing();

    private:
    static rgb_matrix::Font* defaultFontPtr;

};

class TextChangeOrder {
    public:
    enum ScrollType {   // when velocity is not zero...

            CONTINUOUS,    // 0: scroll forever. start off one side, end off the other side, restart
            SINGLE_ON,     // 1: start off one side, end when at origin position on screen
            SINGLE_ONOFF   // 2: start off one side, end off the other side
    };

    TextChangeOrder();  // empty text
    explicit TextChangeOrder(const char* aText);   // default font, spacing,  colors, speed
    explicit TextChangeOrder(std::string  aString);   // default font, spacing, colors, speed
    TextChangeOrder(SpacedFont aSpacedFont, const char* aText);   // default colors

    void setSpacedFont(const SpacedFont& aFont) {spacedFont = aFont;}
    [[nodiscard]] SpacedFont getSpacedFont() const {return spacedFont;}

    void setForegroundColor(const rgb_matrix::Color aColor) {foregroundColor = aColor;}
    [[nodiscard]] rgb_matrix::Color getForegroundColor() const {return foregroundColor;}

    void setBackgroundColor(const rgb_matrix::Color aColor) {backgroundColor = aColor;}
    [[nodiscard]] rgb_matrix::Color getBackgroundColor() const {return backgroundColor;}

    void setText(const char* aText) {text.assign(aText);}
    void setString(const std::string& aString) {text = aString;}
    [[nodiscard]] const char* getText() const { return text.c_str(); }
    std::string getString() const {return text;}
    [[nodiscard]] bool orderDoneHasEmptyDisplay() const {return text.empty() || velocityScrollType == SINGLE_ONOFF;}

    void setVelocity(float aVelocity) {velocity = aVelocity;}
    [[nodiscard]] float getVelocity() const { return velocity; }
    [[nodiscard]] bool isScrolling() const;

    void setVelocityIsHorizontal(bool aVelocityIsHorizontal) {velocityIsHorizontal = aVelocityIsHorizontal;}
    [[nodiscard]] bool getVelocityIsHorizontal() const { return velocityIsHorizontal; }


    void setVelocityScrollType(ScrollType aVelocityScrollType) {velocityScrollType = aVelocityScrollType;}
    [[nodiscard]] ScrollType getVelocityScrollType() const { return velocityScrollType; }

    static rgb_matrix::Color getDefaultForegroundColor();
    static rgb_matrix::Color getDefaultBackgroundColor();

    private:
    SpacedFont spacedFont;
    rgb_matrix::Color foregroundColor;  // default is an extreme color (255 for some subset of R,G,B)
    rgb_matrix::Color backgroundColor;  // default black
    float velocity;                     // default is 0.0=no motion.  1.0=approx one character width per second (left=positive)
    bool velocityIsHorizontal;          // default is true for horizontal scrolling. Set false for vertical
    ScrollType velocityScrollType;      // default is 2 to scroll across and off screen, once

    std::string text;

};



#endif //TEXTCHANGEORDER_H
