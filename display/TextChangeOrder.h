//
// Created by WMcD on 12/7/2024.
//

#ifndef TEXTCHANGEORDER_H
#define TEXTCHANGEORDER_H

#include <string>
#include <vector>
#include "graphics.h"

struct SpacedFont {
    SpacedFont() : fontPtr(getDefaultFontPtr()), letterSpacing(getDefaultLetterSpacing()) {}
    SpacedFont(rgb_matrix::Font* aFontPtr, int aLetterSpacing) : fontPtr(aFontPtr == nullptr ? getDefaultFontPtr() : aFontPtr), letterSpacing(aLetterSpacing) {}
    SpacedFont(const SpacedFont& aSpacedFont) = default;

    rgb_matrix::Font* fontPtr;
    int letterSpacing;

    inline bool equals(const SpacedFont& aSpacedFont) const {
        return (fontPtr == aSpacedFont.fontPtr && letterSpacing == aSpacedFont.letterSpacing);
    }

    static rgb_matrix::Font* getDefaultFontPtr();
    static int getDefaultLetterSpacing();
    static SpacedFont getDefaultSpacedFont() {return SpacedFont();}   // default font with default spacing
    
    static inline SpacedFont getRegisteredSpacedFont(int registeredIndex) {
        if (registeredIndex < 0 || registeredIndex >= (int)registeredSpacedFonts.size()) {
            return getDefaultSpacedFont();
        }
        return registeredSpacedFonts[registeredIndex];
    }
    static inline int registerFont(SpacedFont aSpacedFont) {    // returns new index, for reference
        registeredSpacedFonts.push_back(aSpacedFont);
        return registeredSpacedFonts.size() - 1;   // index of new font
    }
    static inline int getNumRegisteredFonts() {
        return registeredSpacedFonts.size();
    }    

    private:
    static rgb_matrix::Font* defaultFontPtr;
    static std::vector<SpacedFont> registeredSpacedFonts;
};

class TextChangeOrder {
    public:
    inline static const std::string UPLC_FORMATTED_PREFIX = "~+/";   // start of UPLC formatted text protocol
    inline static const std::string UPLC_FORMATTED_SUFFIX = "\0D";   // end of line for UPLC formatted text protocol

    enum ScrollType {   // when velocity is not zero...

            CONTINUOUS,    // 0: scroll forever. start off one side, end off the other side, restart
            SINGLE_ON,     // 1: start off one side, end when at origin position on screen
            SINGLE_ONOFF   // 2: start off one side, end off the other side
    };

    TextChangeOrder();  // empty text
    explicit TextChangeOrder(const char* aText);   // default font, spacing,  colors, speed
    explicit TextChangeOrder(std::string  aString);   // default font, spacing, colors, speed
    TextChangeOrder(SpacedFont aSpacedFont, const char* aText);   // default colors
    TextChangeOrder(const TextChangeOrder& aTextChangeOrder) = default;

    TextChangeOrder& setSpacedFont(const SpacedFont& aFont) {spacedFont = aFont; return *this;}
    [[nodiscard]] SpacedFont getSpacedFont() const {return spacedFont;}

    TextChangeOrder& setForegroundColor(const rgb_matrix::Color aColor) {foregroundColor = aColor; return *this;}
    [[nodiscard]] rgb_matrix::Color getForegroundColor() const {return foregroundColor;}

    TextChangeOrder& setBackgroundColor(const rgb_matrix::Color aColor) {backgroundColor = aColor; return *this;}
    [[nodiscard]] rgb_matrix::Color getBackgroundColor() const {return backgroundColor;}

    TextChangeOrder& setText(const char* aText) {text.assign(aText); return *this;}
    TextChangeOrder& setString(const std::string& aString) {text = aString; return *this;}
    [[nodiscard]] const char* getText() const { return text.c_str(); }
    std::string getString() const {return text;}
    [[nodiscard]] bool orderDoneHasEmptyDisplay() const;    // is display empty when the order is marked Done

    // negative horizontal is to the left, negative vertical is up
    TextChangeOrder&  setVelocity(float aVelocity) {velocity = aVelocity; return *this;}
    [[nodiscard]] float getVelocity() const { return velocity; }
    [[nodiscard]] bool isScrolling() const;

    TextChangeOrder&  setVelocityIsHorizontal(bool aVelocityIsHorizontal) {velocityIsHorizontal = aVelocityIsHorizontal; return *this;}
    [[nodiscard]] bool getVelocityIsHorizontal() const { return velocityIsHorizontal; }

    // scroll type is not sufficient to get scrolling... it is only applied if the velocity is non-zero
    TextChangeOrder&  setVelocityScrollType(ScrollType aVelocityScrollType) {velocityScrollType = aVelocityScrollType; return *this;}
    [[nodiscard]] ScrollType getVelocityScrollType() const { return velocityScrollType; }

    TextChangeOrder& setXOrigin(const int aX_origin) {x_origin = aX_origin; return *this;}
    [[nodiscard]] int getXOrigin() const {return x_origin;}

    TextChangeOrder& setYOrigin(const int aY_origin) {y_origin = aY_origin; return *this;}
    [[nodiscard]] int getYOrigin() const {return y_origin;}

    std::string toUPLCFormattedMessage() const;  // returns a string with the UPLC protocol format to set this order
    bool fromUPLCFormattedMessage(std::string messageString);  // overwrite this object with attributes from the UPLC protocol format string

    static rgb_matrix::Color getDefaultForegroundColor();
    static rgb_matrix::Color getDefaultBackgroundColor();

    static int getXOriginDefault() {return xOriginDefault;}
    static int getYOriginDefault() {return yOriginDefault;}

    static void setXOriginDefault(int aX_origin) {xOriginDefault = aX_origin;}
    static void setYOriginDefault(int aY_origin) {yOriginDefault = aY_origin;}

    static inline TextChangeOrder getRegisteredTemplate(int registeredIndex) {
        if (registeredIndex < 0 || registeredIndex >= (int)registeredTemplates.size()) {
            return TextChangeOrder();
        }
        return registeredTemplates[registeredIndex];
    }
    static inline int registerTemplate(TextChangeOrder aTemplate) {    // returns new index, for reference
        registeredTemplates.push_back(aTemplate);
        return registeredTemplates.size() - 1;   // index of new template
    }
    static inline int getNumRegisteredTemplates() {
        return registeredTemplates.size();
    }    


    private:
    SpacedFont spacedFont;
    rgb_matrix::Color foregroundColor;  // default is an extreme color (255 for some subset of R,G,B)
    rgb_matrix::Color backgroundColor;  // default black
    float velocity;                     // default is 0.0=no motion.  1.0=approx one character width per second
    bool velocityIsHorizontal;          // default is true for horizontal scrolling. Set false for vertical
    ScrollType velocityScrollType;      // default is 2 to scroll across and off screen, once
    int x_origin;                       // default is 0 but can be changed
    int y_origin;                       // default is 0 but can be changed 

    std::string text;

    static int xOriginDefault;
    static int yOriginDefault;
    static std::vector<TextChangeOrder> registeredTemplates;

};



#endif //TEXTCHANGEORDER_H
