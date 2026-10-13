// FIGHTO - shared desktop helpers.
#pragma once
#include "platform.h"

void desktop_init_paths(const char* exePath);
void desktop_parse_args(int argc, char** argv, GameLaunchOptions* opt, bool* windowed);
