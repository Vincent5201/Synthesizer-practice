# Fixed-Point Modular Synthesizer Practice
> This is a learning  project for fixed-point math and synthesizers. Based on an [existing framework]((https://gist.github.com/jserv/061b2ac9be06b446f31087357b8e4054)), I modified and enhanced the system with the assistance of LLMs to deepen technical understanding.

## Enhancements
* Upgraded q15_t to q31_t.
* Implemented PolyBLEP for Sawtooth and Square waves.
* Replaced the basic accumulator with a resonant SVF, supporting adjustable cutoff and resonance.
* Dual-voice architecture capable of rendering independent melodies.
* Example configurations for Strings, Brass, and Flute-like timbres.
* Upgraded 128-point Sine lookup table to 1024-point.
* Add a simple UI.
* Add a realtime version (by key press) running on windows.


## Running
### Original Synthesizer
* `synth.c` generate specific `.wav` files controled by `ui.py`.
* Run `gcc synth.c -o synth` and then run `python3 ui.py`.

### Realtime Synthesizer
* `synth_rt.c` can detect keyboard and generate sounds in realtime.
* Run `gcc synth_rt.c -lwinmm -o synth_rt` and then run `./synth_rt`

## Issue
In `synth_rt.c`, at the `printf("on %d ", note);` statement on line 616, I observed that when I hold down two keys and press a third one, the "on..." message is no longer output. Based on this, I suspect the issue lies within windows.h or hardware constraint.
