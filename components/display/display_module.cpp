#include "display_module.hpp"

#include <TFT_eSPI.h>
#include <TFT_eWidget.h>

static TFT_eSPI tft = TFT_eSPI();
static GraphWidget graph = GraphWidget(&tft);
static TraceWidget trace = TraceWidget(&graph);

namespace pscd::display
{
    void display_module::refreshDisplay()
    {
        xValue += 1;
        renderGraph();
        renderHeart();
        renderTemp();
        renderFall();
    }

    void display_module::updateData(pscd::model::sensor_record newVal)
    {
        values = newVal;
    }

    void display_module::setupDisplay()
    {
        tft.begin();
        tft.setRotation(1);
        tft.fillScreen(TFT_BLACK);

        tft.setTextSize(2);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setCursor(10, 140);
        tft.print("Value:");

        graph.createGraph(GRAPHWIDTH, GRAPHHEIGHT, TFT_BLACK);

        graph.setGraphScale(0, GRAPHWIDTH, 0, GRAPHHEIGHT);
        graph.setGraphGrid(0, 10, 0, 10, TFT_DARKGREY);
        graph.drawGraph(GRAPHX, GRAPHY);

        trace.startTrace(TFT_GREEN);
    }

    void display_module::renderGraph()
    {
        if (xValue > GRAPHWIDTH)
        { // graph reset
            xValue = 0;
            graph.drawGraph(GRAPHX, GRAPHY);
            trace.startTrace(TFT_GREEN);
        }

        trace.addPoint(xValue, values.heart_rate_bpm);
    }

    void display_module::renderTime()
    {
        tft.fillRect(TIMEX, TIMEY, 150, 20, TFT_BLACK); // clear area
        tft.setCursor(TIMEX, TIMEY);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.printf("TimeStamp: %d", values.timestamp_ms);
    }

    void display_module::renderHeart()
    {
        tft.fillRect(HEARTX, HEARTY, 150, 20, TFT_BLACK); // clear area
        tft.setCursor(HEARTX, HEARTY);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.printf("Value: %d", values.heart_rate_bpm);
    }

    void display_module::renderTemp()
    {
        tft.fillRect(TEMPX, TEMPY, 150, 20, TFT_BLACK); // clear area
        tft.setCursor(TEMPX, TEMPY);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.printf("Body temp: %.1f°C Ambient temp: %.1f°C", values.skin_temp_c, values.ambient_temp_c);
    }

    void display_module::renderFall()
    {
        tft.fillRect(FALLX, FALLY, 150, 20, TFT_BLACK); // clear area
        tft.setCursor(FALLX, FALLY);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        if (values.fall_detected)
            tft.printf("You are falling!");
        else
            tft.printf("Not falling.");
    }
}