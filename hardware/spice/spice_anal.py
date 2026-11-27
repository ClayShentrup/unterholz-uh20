import numpy as np, sys, glob, re
def load(fn,col):
    d=np.loadtxt(fn); t=d[:,0]; y=d[:,2*col+1]
    tu=np.arange(t[0],t[-1],1/192000); return np.interp(tu,t,y)
def harm(y,fs=192000,f0=220):
    y=y-np.mean(y); n=len(y); w=np.hanning(n); S=np.abs(np.fft.rfft(y*w)); f=np.fft.rfftfreq(n,1/fs)
    h=[S[np.argmin(abs(f-f0*k))-2:np.argmin(abs(f-f0*k))+3].max() for k in range(1,7)]
    return h
pat=sys.argv[1]; col=int(sys.argv[2])
files=sorted(glob.glob(pat),key=lambda s: float(re.findall(r"A([\d.]+)\.txt",s)[0]))
rows=[]
for fn in files:
    A=float(re.findall(r"A([\d.]+)\.txt",fn)[0]); h=harm(load(fn,col)); rows.append((A,h))
g0=20*np.log10(rows[0][1][0]/rows[0][0])
for A,h in rows:
    print(f"in {A:5.2f} V pk ({20*np.log10(A/1.6):6.1f} dBFS @1.6V): comp {20*np.log10(h[0]/A)-g0:+6.1f} dB  THD {100*np.sqrt(sum(x**2 for x in h[1:]))/h[0]:5.1f}%  H2..H6 "+" ".join(f"{20*np.log10(x/h[0]):6.1f}" for x in h[1:]))
