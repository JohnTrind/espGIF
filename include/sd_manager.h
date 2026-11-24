#pragma once

#include <Arduino.h>
#include <vector>

bool sdInit();
std::vector<String> listGifs();
bool deleteGif(const String& name);
String gifPath(const String& name);
uint64_t getCardFreeBytes();
uint64_t getCardTotalBytes();
int getGifRotation(const String& name);
void setGifRotation(const String& name, int rotation);
