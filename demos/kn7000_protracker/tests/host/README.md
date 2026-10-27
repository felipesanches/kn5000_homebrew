# What the port should sound like, on the PC

**Question:** with the samples in the DSP and the voice commands arriving as they should, what does the port play?
And is that what ProTracker plays?

`render.c` builds the port's own replayer, loader and Paula (`src/pt2/`, `src/kn7/paula.c`) for the PC. It ticks them
2.5/BPM s a tick, as `main.c` does, and plays their voice commands through a model of the DSP mixer: Another World's
`dsp_mixer.py` unit 0, step for step. It writes a mono 44,100 Hz WAV.

    make render                                   # build/host/render
    build/host/render INTRO.MOD INTRO.wav 40
    RENDER_ONLY=1 build/host/render MOD2.MOD c1.wav 8   # one channel alone (0-3)

`tests/mame/compare_render.py` compares a render with libopenmpt (`compare_render.py INTRO.wav INTRO.MOD`), and a MAME
session's sound with a render (`--waveform`). The results are in `tests/results/compare-2026-10-27.txt`.
