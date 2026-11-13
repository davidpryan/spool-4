# SPOOL-4

SPOOL-4 is for players: you never enter a step, you play, and it keeps it. The first take sets a loop's length to the nearest bar; each pass after it is an overdub layer that UNDO removes and REDO restores, so experimenting costs nothing. Quantise is a dial applied as the loop plays, never written into the notes, so it can be turned back at any time. And because the research found that the most-regretted moment is the good idea played when nothing was recording, every note on the keys is kept: CAPTURE puts the phrase you just played into the loop.

![SPOOL-4 on the FM-1's screen](manual/img/screen-loops.png "SPOOL-4 on the FM-1's screen")

Version {{VERSION}}. **Untested on a real FM-1 so far:** it builds, passes its tests in fm1-base's simulator, and
runs in the browser emulator. Installing is at your own risk; the installer can put M-VAVE's own firmware back.

- [Try it in the browser](emu/): the same code the FM-1 runs, with sound. No FM-1 needed.
- [Install](install/): from Chrome or Edge, with the FM-1 connected by USB.
- [Manual](manual/): what it is for, the controls, the screens.
- [Download {{PKG}}](firmware/{{PKG}}): for the command-line installer, `python3 base/tools/fm1_install.py {{PKG}}`.
- [Source](https://github.com/davidpryan/spool-4): MPL-2.0. Built on fm1-base and felucca-bsp, from Felucca by Leo
  Kuroshita, Hügelton Instruments. One of six sequencer firmwares built from a survey of sequencer archetypes.

M-VAVE and FM-1 are trademarks of their owners. SPOOL-4 is not affiliated with M-VAVE.
