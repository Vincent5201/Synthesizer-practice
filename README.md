## A Practice of Fixed-Point Modular Synthesizer
> based on [here](https://gist.github.com/jserv/061b2ac9be06b446f31087357b8e4054)
## Enhancements
* Upgraded q15_t to q31_t.
* Implemented PolyBLEP for Sawtooth and Square waves.
* Replaced the basic accumulator with a resonant SVF, supporting adjustable cutoff and resonance.
* Dual-voice architecture capable of rendering independent melodies.
* Example configurations for Strings, Brass, and Flute-like timbres.
* Upgraded 128-point Sine lookup table to 1024-point.
* Add a simple UI.
* Add a realtime version running on windows.

> The impact of some adjustments is not entirely certain and is still under investigation.

## Running
### Original Synthesizer
* `synth.c` generate specific `.wav` files controled by `ui.py`.
* Run `gcc synth.c -o synth` and then run `python3 ui.py`.

### Realtime Synthesizer
* `synth_rt.c` can detect keyboard and generate sounds in realtime.
* Run `gcc synth_rt.c -lwinmm -o synth_rt` and then run `./synth_rt`

## Problems
In `synth_rt.c`, at the `printf("on %d ", note);` statement on line 616, I observed that when I hold down two keys and press a third one, the "on..." message is no longer output. Based on this, I suspect the issue lies within windows.h and mmsystem.h.