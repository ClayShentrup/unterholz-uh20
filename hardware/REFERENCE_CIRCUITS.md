# Marshall reference circuits (collected 2026-11-26 for the UH-20 LEAD channel)
[S] read from a schematic, [T] stated in text, [?] hard to read, [I] inferred.

## JTM45 (KT66/GZ34). Schematic: https://www.drtube.com/schematics/marshall/jtm45tr.gif (1961/62/1987T trem drawing)
- V1 (ECC83), both channels: 100k plates, shared 820R/250uF cathode, 68k/68k inputs, 1M grid leak, 0.02uF to 1M vol pots, 100pF bright cap on one vol [S]
- Mixer: 270k+270k (470k after late 1966 [T]); ~500pF across the lead mixer on 1967 amps [T]
- V2a: 100k plate, 820R unbypassed; plate 190V, cathode 1.1V [S]
- V2b cathode follower: plate to 310V, 100k cathode [S]
- Tone stack: 270pF treble cap, 56k slope, 0.02uF bass, 0.02uF mid; 250k lin treble, 1M log bass, 25k lin mid [S][T] (Robinette says 33k slope; roef says 250pF usually)
- Presence 5k lin + 0.1uF; NFB 27k [S]
- PI (LTP): 82k/100k plates, 470R + 10k tail, 1M/1M grids, 0.1uF grid bypass, 0.1uF out; fed via 27k from 380V; plates 250V [S]
- Power: KT66 pair, 220k grid leaks [?], 470R 1W screens, plates 430V / screens 440V; GZ34, 16uF + 32uF, 8.2k + 10k droppers, 20H choke [S]; OT ~6.5-8k primary [T]

## 1959 Super Lead (EL34). Schematic: https://www.drtube.com/schematics/marshall/1959u.gif (Unicord 1970)
- V1: 100k plates; normal 820R/250uF, 0.022uF; bright 2.7k/0.68uF, 0.0022uF [S]
- 1M vols; 5000pF bright cap on the bright vol; 470k/470k mixers, 500pF across the bright one [S]
- V2a: 100k plate, 820R(1k)/0.68uF [S]; V2b CF 100k [S]
- Tone stack: 500pF treble, 33k slope, 0.022/0.022; 250k/1M/25k [S] (56k/250pF -> 33k/500pF in 1968 [T])
- NFB 47k (8 ohm) / 100k (4 ohm); presence 5k + 0.1uF
- PI: 82k/100k, 47pF across the plates, 470R, 10k tail, 1M/1M, 0.1uF, 0.022uF outs; 20k dropper [S]
- 4x EL34, 220k leaks, 5.6k stoppers, 1k screens; silicon bridge; OT 1.75k [T]; plates 460-490V [T]

## JCM800 2203 (1981). Schematics: https://www.drtube.com/schematics/marshall/jcm800pr.gif, .../jcm800pw.gif
- V1b input: 68k stopper, 1M leak, 100k plate, 2.7k/0.68uF, 0.022uF [S]
- 470k || 470pF -> 1M log preamp vol with 0.001uF bright cap [S]
- V1a COLD CLIPPER: 100k plate, 10k cathode UNBYPASSED, 0.022uF [S][T]
- Interstage 470k || 470pF, 470k to ground [S]
- V2a: 100k plate, 820R (bypass optional) [S]; V2b CF 100k [S]
- Tone stack: 470pF treble, 33k slope, 0.022/0.022; 220k lin treble [?], 1M bass [?], 22k lin mid [?]; 1M log master after the stack [S]
- Presence 22k lin + 0.1uF + 4.7k; NFB 100k from the 4 ohm tap [S][T]
- PI same as the 1959; EL34 -42V bias, 1k 5W screens; OT 3.2k p-p (2204) [T]

## Koren SPICE models [T] (normankoren.com, wdv.com errata)
12AX7: MU 100, EX 1.4, KG1 1060, KP 600, KVB 300, CCG 2.3p, CGP 2.4p, CCP 0.9p, RGI 2k
EL34: MU 11, EX 1.35, KG1 650, KG2 4200, KP 60, KVB 24
KT88: MU 8.8, EX 1.35, KG1 730, KG2 4200, KP 32, KVB 16; 6L6GC: MU 8.7, EX 1.35, KG1 1460, KG2 4500, KP 48, KVB 12 (no published KT66 set)

## Ancestor: Fender 5F6-A Bassman [T] (ampbooks): 12AY7 V1 shared 820R/250uF; 250pF / 56k / 0.02 / 0.02 stack; same PI

## Marshall 2555 Silver Jubilee (1987) — https://www.drtube.com/schematics/marshall/2555amp.gif, 2550amp.gif (crisper, same preamp)
- V1A: .047u in, 68k stopper, 1M leak, 100k plate, 2.7k/.68u, 47pF plate-cathode [S]
- Gain VR1 1M log fed by .022u, .001u bright cap; wiper -> 100k -> V1B [S]
- V1B: 100k plate, 470p plate->B+, 1.5k/10u; out .022u [S]
- LEAD CLIPPER (after V1B, BEFORE the tone stack): .022u -> 47k series -> node Y; LED pair anti-parallel from Y to a midpoint;
  midpoint to ground: 2x 1N4007 in series one way, 1x 1N4007 the other (asymmetric: LED+2Si vs LED+1Si); 1nF Y->ground; no series cap [S]
- Y -> Lead Master 220k lin -> 47k -> V2A (mixer: 47k lead / 100k rhythm / 47k clip, 1M plate->grid feedback, 100k/1.5k) [S]
- Rhythm clip: 2x 1N4007 anti-parallel + 2.2nF, switched by the VR1 pull [S/I]
- Tone stack is NOT a standard TMB: 4.7n + tandem 1M log bass, 220p treble cap, 100k slope, .1u/10k, mid 100k log via .01u, 470p, 2.2n [S]
- Relay switching per the drawing (lead = relay de-energised); one forum note contradicts part of this [?]

## Mesa Mark IIC+ (community drawing, "numbers not official") — https://cdn.boogieforum.com/attach/6/6850-Mark-IIC-Schematic-FINAL-1-.pdf
- V1A: 1M leak, 150k plate, 1.5k/.47u (+22u via 15k shorted by PULL SHIFT) [S]
- Stack from the V1A plate: treble 250k, bass 250k, mid 10k, vol 1M; treble cap 250p (+750p TREBLE SHIFT); slope 100k; bass .1u; mid .047u; bright 180p (Mark III: 100p) [S][T]
- V1B: 100k / 1.5k / 22u [S]
- Rhythm path: .1u, 91-100k; 3.3M||10p series, 680k||47p to ground (the big attenuator) -> V2B 100k / 1.5k unbypassed -> .047u, 47k -> lead master 250k -> 150k -> V2A 120k / 1k / .47u (+22u PULL DEEP) -> .047u, 15k -> master 1M -> EQ -> PI [S]
- Mark III text: rhythm 1 = blackface-style front end + 2nd stage + 3.3M attenuator; 3rd-stage bypass 0.47u [T]
