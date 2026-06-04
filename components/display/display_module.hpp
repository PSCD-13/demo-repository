#ifndef DISPLAY_HPP
#define DISPLAY_HPP


#include "sensor_record.hpp"

#define GRAPHX 10
#define GRAPHY 10
#define GRAPHWIDTH 300
#define GRAPHHEIGHT 120

#define TIMEX 10
#define TIMEY 220

#define HEARTX 10
#define HEARTY 140

#define TEMPX 10
#define TEMPY 160

#define FALLX 10
#define FALLY 200

namespace pscd::display
{
    class display_module
    {
    public:
        void refreshDisplay();
        void updateData(pscd::model::sensor_record newVal);
        void setupDisplay();

    private:
        void renderGraph();
        void renderTime();
        void renderHeart();
        void renderTemp();
        void renderFall();

        pscd::model::sensor_record values;
        int xValue = 0;


    };
}

#endif