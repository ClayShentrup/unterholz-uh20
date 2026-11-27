"""The UH-20 power-amp supply as ngspice netlist text: a centre-tapped 2 x 22 V AC secondary (31.1 V peak, 0.3 ohm per half),
a bridge, the Sag element, and 4700 uF per rail. The LM3886 is a gain block limited 3 V inside the rails, and it draws its load
current (speaker current on its own half-cycle, plus 50 mA quiescent) from the rail nodes rp / rn.

Sag element, per rail, between the bridge output (bp / bn) and the reservoir (rp / rn):
  kind "rheo":   the original 0-2.7 ohm power rheostat (2.7 * sag / 10 ohm).
  kind "active": the active rheostat. A pass MOSFET (IRF4905 P-channel on the + rail, IRF3205 N-channel on the - rail) in series with
                 a 25 mOhm sense resistor. An OPA197 floating on the bridge output (isolated +/-12 V, e.g. a Murata NMA1512SC) compares
                 a fixed 1/108 tap of the element's voltage (100 ohm / 10.7k across it) with the Sag pot's wiper (100k linear across the
                 sense resistor, fraction g = sag / 10) and drives the gate, forcing  V_element / 108 = g * I * 0.025, i.e. the element
                 is 2.7 * g ohm. The pot carries microamps. Sag 0 is the pot's switch: a relay (10 mOhm contacts) shorts the element.
soft=True adds the power-up soft-start (47 ohm in the primary until a relay shorts it at 0.5 s)."""

RS = 0.025            # sense resistor (4-terminal, 3 W)
R1, R2 = 100.0, 10.7e3   # fixed divider across the element: tap = 1/108 = RS / 2.7
RK = 0.010            # Sag-0 bypass relay contacts
RW = 0.3              # each half of the secondary

MODELS = """* pass devices (VDMOS fits to the datasheets: Vth, Rds(on) at 10 V, gfs, Ciss/Crss)
.model IRF4905 VDMOS(pchan Vto=-3.2 Kp=25 Rd=5m Rs=4m Rg=2 lambda=0.01 Cgs=2.8n Cgdmax=2n Cgdmin=0.3n a=0.5 Cjo=1.5n Is=5p Rb=6m ksubthres=0.1)
.model IRF3205 VDMOS(Vto=3.0 Kp=60 Rd=3m Rs=2m Rg=1.5 lambda=0.01 Cgs=3n Cgdmax=1.5n Cgdmin=0.15n a=0.5 Cjo=1n Is=5p Rb=4m ksubthres=0.1)
* bridge rectifier diodes (8 A class, about 0.9 V at 1 A)
.model DBR D(IS=2n N=1.7 RS=8m CJO=60p TT=2u BV=200)
* OPA197-class op-amp: 10 MHz GBW, 20 V/us, 100 dB, rail-to-rail output, 1.5 mA, internal node referenced to its own supplies
.subckt OPA inp inn vh vl out
BMID m 0 V=(V(vh)+V(vl))/2
CI1 inp m 5p
CI2 inn m 5p
BGM m n1 I=0.02*tanh((V(inp)-V(inn))/0.318)
CC n1 m 1n
RO1 n1 m 1.6Meg
BHI hc 0 V=V(vh)-0.05
BLO lc 0 V=V(vl)+0.05
DHI n1 hc DCLMP
DLO lc n1 DCLMP
.model DCLMP D(IS=1e-14 N=0.2)
BOUT o 0 V=max(min(V(n1),V(vh)-0.05),V(vl)+0.05)
ROUT o out 20
RQ vh vl 16k
.ends
"""


RSOFT = 47.0          # power-up soft-start: 47 ohm (10 W) in series with the 120 V primary, shorted by a relay after 0.5 s
TSOFT = 0.5


def transformer(soft=False):
    if not soft:
        src = "VSA sa 0 SIN(0 31.1 60)\nVSB sb 0 SIN(0 -31.1 60)\n"
    else:   # the primary resistor, seen from each half: (47 ohm / (120/22)^2) times the sum of both rails' charging currents
        rc = RSOFT * (22 / 120) ** 2
        e = f"31.1*sin(2*pi*60*time)-sgn(sin(2*pi*60*time))*{rc:.4f}*(i(VAP)+i(VAN))*max(0,min(1,({TSOFT}-time)/1m))"
        src = f"* soft-start: {RSOFT:g} ohm in the primary until {TSOFT} s\nBSA sa 0 V={e}\nBSB sb 0 V=-({e})\n"
    return f"""* secondary: 2 x 22 V AC (31.1 V peak), centre tap to ground, {RW} ohm per half; bridge
{src}RWA sa ta {RW}
RWB sb tb {RW}
DB1 ta bq DBR
DB2 tb bq DBR
DB3 bm ta DBR
DB4 bm tb DBR
* ammeters: i(VAP) charges the + rail, i(VAN) the - rail
VAP bq bp DC 0
VAN bn bm DC 0
"""


def element(kind, sag, rail):
    """One rail's Sag element. rail '+' : bp -> rp (current into the + reservoir); '-' : rn -> bn (current out of the - reservoir)."""
    g = min(max(sag / 10, 0), 1)
    b, r, s = ("bp", "rp", "p") if rail == "+" else ("bn", "rn", "n")
    if kind == "rheo":
        return f"RSAG{s} {b} {r} {max(2.7 * g, 1e-4):.6g}\n"
    gp = max(g, 1e-4)
    t = f"""* active rheostat ({rail} rail): sense {RS} ohm, pass FET, divider 1/108 across the element, Sag pot (100k lin) across the sense R
VF{s}H vh{s} {b} DC 12
VF{s}L {b} vl{s} DC 12
RSN{s} {b} x{s} {RS}
R1{s} {b} t1{s} {R1:g}
R2{s} t1{s} {r} {R2:g}
RPA{s} {b} t2{s} {100e3 * gp:.6g}
RPB{s} t2{s} x{s} {100e3 * (1 - gp) + 0.1:.6g}
RI1{s} t1{s} i1{s} 1k
RI2{s} t2{s} i2{s} 1k
XOA{s} i1{s} i2{s} vh{s} vl{s} gd{s} OPA
RG{s} gd{s} g{s} 47
"""
    t += f"MP{s} {r} g{s} x{s} IRF4905\n" if rail == "+" else f"MN{s} {r} g{s} x{s} IRF3205\n"
    if g < 0.005:
        t += f"RK{s} {b} {r} {RK}\n"   # Sag 0: the pot's switch closes the bypass relay
    return t


def supply(kind, sag, soft=False):
    """Rails rp / rn from the secondary through the Sag element into 4700 uF each, loaded by the LM3886 (output node 'out', 8 ohm)."""
    return (transformer(soft) + element(kind, sag, "+") + element(kind, sag, "-") + """CRP rp 0 4700u
CRN 0 rn 4700u
* LM3886 supply currents: the speaker current on its own half-cycle plus 50 mA quiescent, from each rail
* (scaled to zero while a rail is below 10 V, i.e. only at power-up, so the operating point exists)
BIP rp 0 I=(max(V(out),0)/8+0.05)*min(max(V(rp)/10,0),1)
BIN 0 rn I=(max(-V(out),0)/8+0.05)*min(max(-V(rn)/10,0),1)
""")


def lm3886(gain, inp="pin"):
    """The LM3886 as a gain block limited 3 V inside the live rails."""
    return f"BOUT out 0 V=max(min({gain}*V({inp}),V(rp)-3),V(rn)+3)\nRLOAD out 0 8\n"
