#pragma once

void resetButtonBegin();
// Returns true while a reset is in progress (caller should skip normal work).
bool resetButtonLoop();
