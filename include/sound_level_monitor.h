#pragma once

void initSoundLevelMonitor();
void updateSoundLevelMonitor();
void suspendSoundLevelMonitor();
void resumeSoundLevelMonitor();
void resetSoundLevelWindow();

bool soundLevelWindowReady();
bool isEnvironmentQuiet();
