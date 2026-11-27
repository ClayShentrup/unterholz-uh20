import numpy as np, itertools, json, sys
from tube_stage import tube_curve, jfet_curve
E12=[1.0,1.2,1.5,1.8,2.2,2.7,3.3,3.9,4.7,5.6,6.8,8.2]
RDs=[m*10**e for e in (3,4) for m in E12 if 2.2e3<=m*10**e<=100e3]
RSs=[m*10**e for e in (2,3) for m in E12 if 100<=m*10**e<=8.2e3]
def match(name,rp,rk,bp,byp):
    xt,yt,_,_=tube_curve(rp,rk,bp,byp)
    best=None
    for rd,rs in itertools.product(RDs,RSs):
        try: xj,yj,vd,vs=jfet_curve(rd,rs,18,byp)
        except Exception: continue
        if not (2<vd<16): continue
        for a in np.geomspace(0.01,0.6,30):            # JFET input volts per tube input volt
            yji=np.interp(xt*a,xj,yj)
            b=np.dot(yji,yt)/np.dot(yji,yji)            # output scale (tube volts per JFET volt)
            e=np.sqrt(np.mean((b*yji-yt)**2))/np.ptp(yt)
            if best is None or e<best[0]: best=(e,rd,rs,a,b,vd,vs)
    e,rd,rs,a,b,vd,vs=best
    print(f"{name}: J201 Rd {rd/1e3:g}k, Rs {rs:g} {'(bypassed)' if byp else '(unbypassed)'}, drain {vd:.1f} V, source {vs:.2f} V | input scale {a:.3f}, output scale 1/{b:.1f} | shape error {100*e:.1f}% of swing")
    return dict(name=name,rd=rd,rs=rs,bypassed=byp,a=a,b=b,vd=vd,err=e)
res=[match("JCM800 cold clipper",100e3,10e3,330,False), match("V2a gain stage",100e3,820,330,False), match("V1 input stage",100e3,2700,330,True)]
json.dump(res,open("stage_matches.json","w"),indent=1)
