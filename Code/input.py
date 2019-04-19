#!/usr/bin/env python3

import pifacecad

def update_pin_text(event):
    print(str(event.pin_num))
    
cad = pifacecad.PiFaceCAD()
listener = pifacecad.SwitchEventListener(chip=cad)
for i in range(8):
    listener.register(i, pifacecad.IODIR_FALLING_EDGE, update_pin_text)
listener.activate()
