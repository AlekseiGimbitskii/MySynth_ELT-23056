#!/usr/bin/env python3

import pifacecad
import sys

cad = pifacecad.PiFaceCAD()
cad.lcd.backlight_on()
cad.lcd.clear()
cad.lcd.write(sys.argv[1])
