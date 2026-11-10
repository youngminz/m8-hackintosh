import sys
w,h=(int(sys.argv[3]),int(sys.argv[4])) if len(sys.argv)>4 else (1920,1080); fs=w*h*3//2
a=open(sys.argv[1],'rb').read(); b=open(sys.argv[2],'rb').read()
n=min(len(a),len(b))//fs
for i in range(n):
    x=a[i*fs:(i+1)*fs]; y=b[i*fs:(i+1)*fs]
    yd=sum(abs(p-q) for p,q in zip(x[:w*h:7],y[:w*h:7]))/(w*h/7)
    cd=sum(abs(p-q) for p,q in zip(x[w*h::7],y[w*h::7]))/(w*h/2/7)
    big=sum(1 for p,q in zip(x[:w*h:7],y[:w*h:7]) if abs(p-q)>2)/(w*h/7)*100
    if i<6 or yd>0.5 or i==n-1: print('frame %d: mean |hw-sw| luma %.3f chroma %.3f, luma off by >2: %.2f%%, hw mean luma %.1f'%(i,yd,cd,big,sum(x[:w*h:7])/(w*h/7)))
print(n,'frames compared')
