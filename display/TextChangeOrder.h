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

    rgb_matrix::Font* fontPtr;
    int letterSpacing;

    static rgb_matrix::Font* getDefaultFontPtr();
    static int getDefaultLetterSpacing();

    private:
    static rgb_matrix::Font* defaultFontPtr;

};

class TextChangeOrder {
    public:
    TextChangeOrder();  // empty text
    explicit TextChangeOrder(const char* aText);   // default font, spacing,  colors, speed
    explicit TextChangeOrder(const std::string& aString);   // default font, spacing, colors, speed
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

    void setVelocity(float aVelocity) {velocity = aVelocity;}
    [[nodiscard]] float getVelocity() const { return velocity; }
    [[nodiscard]] bool isScrolling() const;

    void setVelocityIsHorizontal(bool aVelocityIsHorizontal) {velocityIsHorizontal = aVelocityIsHorizontal;}
    [[nodiscard]] bool getVelocityIsHorizontal() const { return velocityIsHorizontal; }

    void setVelocityIsSingleScroll(bool avelocityIsSingleScroll) {velocityIsSingleScroll = avelocityIsSingleScroll;}
    [[nodiscard]] bool getVelocityIsSingleScroll() const { return velocityIsSingleScroll; }

    static rgb_matrix::Color getDefaultForegroundColor();
    static rgb_matrix::Color getDefaultBackgroundColor();

    private:
    SpacedFont spacedFont;
    rgb_matrix::Color foregroundColor;  // default is an extreme color (255 for some subset of R,G,B)
    rgb_matrix::Color backgroundColor;  // default black
    float velocity;                     // 0.0=no motion.  1.0=approx one character width per second (left=positive)
    bool velocityIsHorizontal;          // true for horizontal scrolling, false for vertical
    bool velocityIsSingleScroll;        // if true, stop after scrolling on

    std::string text;

};



#endif //TEXTCHANGEORDER_H
