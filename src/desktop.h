
#ifndef DESKTOP_H
#define DESKTOP_H

#include <stdint.h>

//initializes and runs the desktop mode
//switches to VGA 320x200x256 draws backgrounds mouse etc
//exits back to text mode on ESC key
void cmd_desktop(const char *args);

#endif 
