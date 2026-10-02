#pragma once

#include "Types.hpp"

#include <bitset>
#include <vector>

const uint8_t BANKDATA_TYPE_SPLIT = 0x40;
const uint8_t BANKDATA_TYPE_RHYTHM = 0x80;

const uint8_t BANKDATA_TYPE_CGB = 0x07;
const uint8_t BANKDATA_TYPE_FIX = 0x08;

const uint8_t BANKDATA_TYPE_PCM = 0x00;
const uint8_t BANKDATA_TYPE_SQ1 = 0x01;
const uint8_t BANKDATA_TYPE_SQ2 = 0x02;
const uint8_t BANKDATA_TYPE_WAVE = 0x03;
const uint8_t BANKDATA_TYPE_NOISE = 0x04;
