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

> The impact of some adjustments is not entirely certain and is still under investigation.

