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

    void startChangeOrder(const TextChangeOrder& aChangeOrder);
    [[nodiscard]] const TextChangeOrder& getChangeOrder() const {return currChangeOrder;}
    [[nodiscard]] bool isChangeOrderDone() const { return currChangeOrderDone; }    // true if done or continuous scroll completed at least once
    [[nodiscard]] bool isContinuousScroll() const {return currChangeOrder.isScrolling && currChangeOrder.getVelocityScrollType() == TextChangeOrder::CONTINUOUS;}

    void iota();    // continue working on any previously assigned task, then return (non-blocking)

    void setAllowIdleMarkers(bool isAllow) {allowIdleMarkers = isAllow;}
    [[nodiscard]] int getAllowIdleMarkers() const {return allowIdleMarkers;}
    [[nodiscard]] int getMarkedIdle() const {return isIdle;}

    void setMarkDisconnected(bool aIsDisconnected) {isDisconnected = aIsDisconnected;}
    [[nodiscard]] int getMarkDisconnected() const {return isDisconnected;}

    private:
    bool displayerOK;   // if false, every method should presume other attributes are suspect (e.g. canvas NULL)
    bool allowIdleMarkers;  // mark dots on display when display has been blank for several seconds
    bool isIdle;            // true if "idle" timeout has occurred and idle markers are allowed
    bool isDisconnected;    // mark dots (different color) on display to report no messaging connection
    bool markedDisconnected;    // true if "disconnect" dots have been marked

    uint8_t defaultPWMBits;
    rgb_matrix::RGBMatrix *canvas;
    rgb_matrix::FrameCanvas *offscreen_canvas;

    TextChangeOrder currChangeOrder;
    bool currChangeOrderDone;

    // current parameters of display, relevant when velocity is not zero
    struct timespec next_frame;
    int x;
    int y;
    int scroll_direction;
    int delay_speed_usec;

    std::time_t last_change_time;   // seconds since epoch (C++17)

    [[nodiscard]] bool isExtremeColors() const;
    void updatePWMBits();
    void dotCorners(const rgb_matrix::Color &, rgb_matrix::Canvas *aCanvas);
    void setChangeDone() {setChangeDone(true);}
    void setChangeDone(bool isChangeDone);
};


#endif //DISPLAYER_H
