# LEDGER-4

LEDGER-4 brings the tracker's way of writing music to the FM-1: notes as rows of text, read top to bottom, with a command on each row that says *how* the note plays. Its aim is depth from very little: a phrase of 16 rows is reused by reference in chains (with a transpose each time), and chains are laid out in a song, so a whole track is a few hundred bytes of numbers that can be edited precisely and repeated without copying. The commands (arpeggio, chance, delay, kill, retrigger, slide, tempo, hop, filter, MIDI CC, random, groove, echo) give every row the micro-control that a step grid hides behind menus.

![LEDGER-4 on the FM-1's screen](manual/img/screen-phrase.png "LEDGER-4 on the FM-1's screen")

Version {{VERSION}}. **Untested on a real FM-1 so far:** it builds, passes its tests in fm1-base's simulator, and
runs in the browser emulator. Installing is at your own risk; the installer can put M-VAVE's own firmware back.

- [Try it in the browser](emu/): the same code the FM-1 runs, with sound. No FM-1 needed.
- [Install](install/): from Chrome or Edge, with the FM-1 connected by USB.
- [Manual](manual/): what it is for, the controls, the screens.
- [Download {{PKG}}](firmware/{{PKG}}): for the command-line installer, `python3 base/tools/fm1_install.py {{PKG}}`.
- [Source](https://github.com/davidpryan/ledger-4): MPL-2.0. Built on fm1-base and felucca-bsp, from Felucca by Leo
  Kuroshita, Hügelton Instruments. One of six sequencer firmwares built from a survey of sequencer archetypes.

M-VAVE and FM-1 are trademarks of their owners. LEDGER-4 is not affiliated with M-VAVE.
