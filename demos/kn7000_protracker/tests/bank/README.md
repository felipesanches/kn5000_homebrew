# The sample coder, checked on the PC

**Question:** does the port's sample coder (`src/kn7/bank.c`, which runs on the KN7000 when a module is loaded) code
the samples exactly as Another World's `dsp_bank.py` does, whose decoder runs in the DSP? And do the records decode
back to the samples?

    python3 -I check_bank.py ~/compartilhado/technics-anotherworld/kn7000/tools ~/compartilhado/tmp/pt-bank \
        <Pinball Fantasies data>/INTRO.MOD ... TABLE4.MOD

It builds `bank.c` for the PC (`-DBANK_HOST_TEST`, with `bank_main.c`). For each module it then:
- lays the samples out as the port does (`kn7AllocSamples`);
- codes them with both coders;
- compares the records word for word;
- decodes them with `dsp_bank.decode` and compares the result with the samples;
- compares the checksums.

`check_bank.out` is the run of 2026-10-27 on the six Pinball Fantasies modules: 6 of 6 match.
