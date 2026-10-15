#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include <Preferences.h>
#include "MagnetoTypes.h"

class StorageManager {
public:
    StorageManager();
    void begin();
    
    void saveParams(int idx, const MagnetoChannelParams& params);
    void loadParams(int idx, MagnetoChannelParams& params);
    
    void saveLastDuration(int durationMin);
    int loadLastDuration();

private:
    Preferences _prefs;
};

#endif // STORAGE_MANAGER_H
