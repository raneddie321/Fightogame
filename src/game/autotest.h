// FIGHTO - automated in-engine gameplay test runner (--autotest).
#pragma once
#include "../platform/platform.h"

void autotest_begin(const GameLaunchOptions* opt);
void autotest_frame(float dt);
void autotest_overlay();
void autotest_after_frame();  // after the UI is drawn: captures pending screenshots
bool autotest_finished();
