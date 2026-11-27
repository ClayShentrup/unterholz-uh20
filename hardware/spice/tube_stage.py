# DC transfer of a tube stage (grid swing -> plate swing around the bias point) and the JFET stage that matches its shape
import subprocess, numpy as np, os, tempfile, sys
def run(net):
    d=tempfile.mkdtemp(); f=os.path.join(d,"c.cir"); o=os.path.join(d,"o.txt")
    open(f,"w").write(net.replace("OUTFILE",o)); subprocess.run(["ngspice","-b",f],capture_output=True)
    a=np.loadtxt(o); return a[:,0],a[:,1]
def tube_curve(rp,rk,bplus,bypassed):
    # bias point first (DC op), then sweep the grid around 0 with the cathode held at its bias voltage if bypassed
    net=f""".include koren.lib
VB bp 0 {bplus}
VG g 0 0
XV p g k 12AX7
RP bp p {rp}
RK k 0 {rk}
.op
.control
op
print v(k) v(p)
.endc
.end"""
    d=tempfile.mkdtemp(); f=os.path.join(d,"c.cir"); open(f,"w").write(net)
    out=subprocess.run(["ngspice","-b",f],capture_output=True,text=True).stdout
    vk=float([l for l in out.splitlines() if l.strip().startswith("v(k)")][0].split("=")[1]); vp=float([l for l in out.splitlines() if l.strip().startswith("v(p)")][0].split("=")[1])
    kpart=f"VK k 0 {vk}" if bypassed else f"RK k 0 {rk}"
    net=f""".include koren.lib
VB bp 0 {bplus}
VG g 0 0
XV p g k 12AX7
RP bp p {rp}
{kpart}
.dc VG -12 4 0.02
.control
run
wrdata OUTFILE v(p)
.endc
.end"""
    x,y=run(net); return x,y-vp,vp,vk
def jfet_curve(rd,rs,vdd,bypassed):
    net=f""".include j201.lib
VD dd 0 {vdd}
VG g 0 0
J1 d g s J201
RD dd d {rd}
RS s 0 {rs}
.op
.control
op
print v(s) v(d)
.endc
.end"""
    d=tempfile.mkdtemp(); f=os.path.join(d,"c.cir"); open(f,"w").write(net)
    out=subprocess.run(["ngspice","-b",f],capture_output=True,text=True).stdout
    vs=float([l for l in out.splitlines() if l.strip().startswith("v(s)")][0].split("=")[1]); vd=float([l for l in out.splitlines() if l.strip().startswith("v(d)")][0].split("=")[1])
    spart=f"VS s 0 {vs}" if bypassed else f"RS s 0 {rs}"
    net=f""".include j201.lib
VD dd 0 {vdd}
VG g 0 0
J1 d g s J201
RD dd d {rd}
{spart}
.dc VG -3 1 0.004
.control
run
wrdata OUTFILE v(d)
.endc
.end"""
    x,y=run(net); return x,y-vd,vd,vs
if __name__=="__main__":
    for name,rp,rk,bp,byp in (("JCM800 cold clipper (100k/10k unbypassed)",100e3,10e3,330,False),("gain stage (100k/820 unbypassed, V2a)",100e3,820,330,False),("input stage (100k/2.7k bypassed, V1)",100e3,2700,330,True)):
        x,y,vp,vk=tube_curve(rp,rk,bp,byp)
        up=y.max(); dn=-y.min(); g=np.gradient(y,x)[np.argmin(abs(x))]
        print(f"{name}: bias plate {vp:.0f} V, cathode {vk:.2f} V, small-signal gain {abs(g):.1f}, swing +{up:.0f}/-{dn:.0f} V (asymmetry {up/dn:.2f})")
