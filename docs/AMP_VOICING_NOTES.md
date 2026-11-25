# Amp voicing notes

Where each amp's sound comes from, what the model now does about it, and how sure we are.
"Confirmed" means published by the manufacturer or agreed across several independent circuit analyses.
"Likely" means common knowledge in amp-tech circles but not checked against an original schematic here.

## Shared changes

- **Power tube character** (`AmpModelSpec::powerKnee`). The output stage used one generic soft curve for
  every amp. It now has a per-model knee: EL34 (Marshalls) breaks up early and gradually, 6L6
  (5150, Rectifier, Mark, Bass 400) stays cleaner then clips harder, 6550 (SVT) has the most headroom
  and the hardest knee. The tube complement is shown in the amp model tooltip.
- **Treble-peaking interstage networks** (`StageSpec::trebleHz/trebleDb`). High-gain preamps feed the
  clipping stage through a resistor bridged by a small cap, which pushes treble into the clipper. This
  can now be modelled per stage.

## Per model

| Model | Key circuit facts | Change | Confidence |
|---|---|---|---|
| Brit Plexi (1959 Super Lead) | Distortion comes mostly from the power amp, not the preamp; 500 pF bright cap; 4x EL34 | EL34 knee (1.6) | Confirmed (topology), likely (values) |
| Brit 800 (JCM800 2203) | V1a 2.7k / 0.68 uF and V1b with a 0.68 uF partial bypass trim the bass before the clipper; V2a cold clipper with an unbypassed 10k cathode; 470k mixer resistor bridged by 470 pF; 4x EL34 | V1b low shelf (300 Hz, -6 dB), +5 dB treble shelf at 720 Hz into the clipper, EL34 knee, level trim re-calibrated | 10k cold clipper confirmed; 0.68 uF bypasses and 470 pF likely |
| American 51 (5150 lead) | SLO-derived cascade, small coupling caps (2.2 nF into a middle stage) for the tight lows, cold-clipper last stage; 4x 6L6GC | Colder last stage, 6L6 knee | Likely |
| Recto Modern (Dual Rectifier) | 39k unbypassed cold clipper with a 1 nF plate cap, stack driven by a cathode follower after the gain; Modern mode raises the treble cap from 500 pF to 680 pF and adds about 3 dB around 2.4 kHz; 6L6GC | Treble cap 680 pF, colder clipper with a lower plate roll-off, presence corner at 2.4 kHz, 6L6 knee | Confirmed (blog analysis of the stack, several forum sources for the 39k) |
| Mark C+ Lead (Mark IIC+) | Plate-fed tone stack before the lead cascade, so EQ shapes what distorts; Simul-Class 6L6 | 6L6 knee (stack placement was already right) | Confirmed (topology) |
| SVT Classic | Ampeg's published figures: bass +/-12 dB @ 40 Hz, mid +10/-20 dB at 220/450/800/1.6k/3k, treble +15/-20 dB @ 4 kHz, Ultra Lo +2 dB @ 40 Hz / -10 dB @ 500 Hz, Ultra Hi +9 dB @ 8 kHz; 6x 6550; -3 dB @ 40 Hz and 15 kHz | Stack re-fitted to those numbers (and unit tested), output transformer top raised to 15 kHz, 6550 knee | Confirmed (Ampeg SVT-CL spec sheet) |
| Flip-Top B15, Bass 400 | Unchanged apart from the tube knee | | |

## Next steps to get closer

The network policy of the build environment blocked the schematic sites (Rob Robinette, Dr. Tube, Ampage),
so stage values for the 5150, Recto and Mark lead cascades are still estimates. The most reliable next step
is to compare against captures of the real amps (frequency response and THD vs. input level at a few
gain settings) and fit the per-stage values to those.

Sources: [Ampeg SVT-CL owner's manual](https://ampeg.com/pdf/SVTCL_OM.pdf),
[Rectifier Modern tone stack analysis](https://warpedmusician.wordpress.com/2016/01/07/dual-rectifier-channel-3-modern-tone-stack/),
[Rectifier design concepts](https://warpedmusician.wordpress.com/2015/11/25/channels-2-and-3-part-1-mesa-rectifier-design-concepts/),
[cold clipper values (Amp Garage)](https://ampgarage.com/forum/viewtopic.php?t=36402),
[Marshall Super Lead (Wikipedia)](https://en.wikipedia.org/wiki/Marshall_Super_Lead),
[Mark IIC+ (Wikipedia)](https://en.wikipedia.org/wiki/Mesa/Boogie_Mark_IIC+).
