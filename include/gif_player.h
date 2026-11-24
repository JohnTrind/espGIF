#pragma once

#include <Arduino.h>

void gifPlayerInit();
bool gifPlayerLoad(const String& path);
bool gifPlayerPlayFrame();
void gifPlayerStop();
bool gifPlayerIsPlaying();
String gifPlayerCurrentName();
void gifPlayerRequestSwitch(const String& name);
bool gifPlayerHasPendingSwitch();
void gifPlayerHandleSwitch();
void gifPlayerSaveDefault(const String& name);
String gifPlayerLoadDefault();
void gifPlayerPause();
void gifPlayerResume();

