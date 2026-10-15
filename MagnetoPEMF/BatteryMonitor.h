#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include "Config.h"

class BatteryMonitor {
public:
    BatteryMonitor();
    void begin();
    void update();
    
    float getVoltage() const { return _voltage; }
    int getPercentage() const { return _percentage; }
    int getRawAdc() const { return _rawAdc; }
    
    bool isLow() const { return _percentage < BAT_LOW_THRESHOLD; }
    bool isEmpty() const { return _percentage < BAT_CRITICAL_TH; }

private:
    float _voltage;
    int _percentage;
    int _rawAdc;
    unsigned long _lastRead;
    
    float readRawVoltage();
    int calculatePercentage(float voltage);
};

#endif // BATTERY_MONITOR_H
