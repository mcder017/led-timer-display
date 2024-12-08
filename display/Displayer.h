//
// Created by WMcD on 12/7/2024.
//

#ifndef DISPLAYER_H
#define DISPLAYER_H

#include "led-matrix.h"
#include "graphics.h"   // Color
#include "TextChangeOrder.h"

#include <ctime>        // timespec

class Displayer {
    public:
    Displayer(rgb_matrix::RGBMatrix::Options& aMatrix_options, rgb_matrix::RuntimeOptions& aRuntime_opt);
    ~Displayer();

    [[nodiscard]] bool isDisplayerOK() const {return displayerOK;}
    [[nodiscard]] bool isMatrixAttached() const;

    static bool FullSaturation(const rgb_matrix::Color &c);

    void startChangeOrder(TextChangeOrder aChangeOrder);
    const TextChangeOrder& getChangeOrder() const {return currChangeOrder;}
    [[nodiscard]] bool isChangeOrderDone() const { return currChangeOrderDone; }

    void iota();    // continue working on any previously assigned task, then return (non-blocking)

    // shift the upper left position of the destination of text to be drawn
    void setXOrigin(const int xOrig) {x_origin = xOrig;}
    [[nodiscard]] int getXOrigin() const {return x_origin;}
    void setYOrigin(const int yOrig) {y_origin = yOrig;}
    [[nodiscard]] int getYOrigin() const {return y_origin;}

    private:
    bool displayerOK;   // if false, every method should presume other attributes are suspect (e.g. canvas NULL)

    uint8_t defaultPWMBits;
    rgb_matrix::RGBMatrix *canvas;
    rgb_matrix::FrameCanvas *offscreen_canvas;

    int x_origin;
    int y_origin;

    TextChangeOrder currChangeOrder;
    bool currChangeOrderDone;

    // current parameters of display, relevant when velocity is not zero
    struct timespec next_frame;
    int x;
    int y;
    int scroll_direction;
    int delay_speed_usec;

    [[nodiscard]] bool isExtremeColors() const;
    void updatePWMBits();
};


#endif //DISPLAYER_H
