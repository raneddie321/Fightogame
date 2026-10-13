// FIGHTO - automated in-engine gameplay test runner (--autotest).
#pragma once
#include "../platform/platform.h"

void autotest_begin(const GameLaunchOptions* opt);
void autotest_frame(float dt);
void autotest_overlay();
bool autotest_finished();
