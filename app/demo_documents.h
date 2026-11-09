#pragma once

#include "core/board.h"

// UI thread; returns a self-contained PCB fixture for B rendering and interaction tests.
eda::Board emptyBoardTemplate();
eda::Board exampleBoard();
