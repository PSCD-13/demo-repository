#ifndef DISPLAY_HPP
#define DISPLAY_HPP

#include "sensor_record.hpp"

#define GRAPHX 10
#define GRAPHY 10
#define GRAPHWIDTH 280
#define GRAPHHEIGHT 140
#define GRAPHMIN 40
#define GRAPHMAX 180

#define DATAX 170

#define HEARTX 10
#define HEARTY 160

#define TEMPX 10
#define TEMPY 180

#define WORKX 10
#define WORKY 220
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
        void renderHeart();
        void renderTemp();
        void renderWorkout();
        bool renderEmergency();
        void reset();

        pscd::model::sensor_record values;
        int xValue = 0;
    };

}

#endif