// dekstop.h
#ifndef DEKSTOP_H
#define DEKSTOP_H

#include <stdint.h>



// Initializes and runs the "dekstop" mode.
// Switches to VGA 320x200x256, draws background, mouse, etc.
// Exits back to text mode on ESC.
void cmd_desktop(const char *args);

#endif // DEKSTOP_H
