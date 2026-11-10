import struct, sys, bisect, re, mmap, glob, os
# Directory holding a copy of the machine's dyld shared cache
# (/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/dyld_shared_cache_x86_64h*).
D=os.environ.get('DSC_DIR','dl/dsc').rstrip('/')+'/'
B=D+'dyld_shared_cache_x86_64h'
files=[B]+sorted(glob.glob(B+'.0*'))
maps=[]  # (addr,size,mm,fileoff)
mms=[]
for fn in files:
    f=open(fn,'rb'); mm=mmap.mmap(f.fileno(),0,prot=mmap.PROT_READ); mms.append(mm)
    mo,mc=struct.unpack_from('<II',mm,0x10)
    for i in range(mc):
        a,s,fo=struct.unpack_from('<QQQ',mm,mo+i*32); maps.append((a,s,mm,fo))
maps.sort(key=lambda m:m[0])
def rd(va,n):
    for a,s,mm,fo in maps:
        if a<=va<a+s: return mm[fo+va-a:fo+va-a+n]
    raise Exception('unmapped %x'%va)
def cstr(va,mx=400):
    d=rd(va,mx); return d.split(b'\0')[0].decode('latin1')
m0=mms[0]
io,ic=struct.unpack_from('<II',m0,0x1c0)
if not io: io,ic=struct.unpack_from('<II',m0,0x18)
images={}
for i in range(ic):
    a,_,_,po=struct.unpack_from('<QQQI',m0,io+i*32)
    images[m0[po:po+300].split(b'\0')[0].decode()]=a
def find(pat): return [p for p in images if re.search(pat,p)]
class Img:
    def __init__(s,path):
        s.path=path; s.base=images[path]
        h=rd(s.base,32); ncmds=struct.unpack('<8I',h)[4]; p=s.base+32; s.segs=[]; s.sects=[]; s.tab={}; s.imps=[]
        sym=None; dys=None
        for i in range(ncmds):
            cmd,sz=struct.unpack('<II',rd(p,8)); d=rd(p,sz)
            if cmd==0x19:
                name=d[8:24].rstrip(b'\0').decode(); vm,vs,fo,fs=struct.unpack('<4Q',d[24:56]); s.segs.append((name,vm,vs,fo,fs))
                ns=struct.unpack('<I',d[64:68])[0]
                for k in range(ns):
                    q=d[72+k*80:72+(k+1)*80]; sn=q[:16].rstrip(b'\0').decode(); a,z=struct.unpack('<QQ',q[32:48]); r1,r2=struct.unpack('<II',q[68:76]); fl=struct.unpack('<I',q[64:68])[0]
                    s.sects.append((name,sn,a,z,fl,r1,r2))
            elif cmd==2: sym=struct.unpack('<4I',d[8:24])
            elif cmd==0xb: dys=struct.unpack('<18I',d[8:80])
            p+=sz
        le=[x for x in s.segs if x[0]=='__LINKEDIT'][0]
        cv=lambda off: le[1]+off-le[3]
        symoff,nsyms,stroff,strsize=sym
        sd=rd(cv(symoff),nsyms*16); strs=rd(cv(stroff),strsize); s.allsyms=[]
        for i in range(nsyms):
            strx,typ,sect,desc,val=struct.unpack_from('<IBBHQ',sd,i*16)
            e=strs.find(b'\0',strx); nm=strs[strx:e].decode('latin1'); s.allsyms.append(nm)
            if typ&0xe0 or (typ&0xe)!=0xe: continue
            s.tab[nm]=val
        s.byaddr={}
        for k,v in s.tab.items(): s.byaddr.setdefault(v,k)
        # stubs / got via indirect symtab
        if dys:
            ioff,n=dys[12],dys[13]
            ind=struct.unpack('<%dI'%n,rd(cv(ioff),n*4)) if n else ()
            for seg,sn,a,z,fl,r1,r2 in s.sects:
                t=fl&0xff
                if t in(6,7,8):
                    step=r2 if t==8 else 8
                    for k in range(z//step if step else 0):
                        ix=ind[r1+k] if r1+k<len(ind) else 0
                        if ix<nsyms: s.byaddr.setdefault(a+k*step,'imp:'+s.allsyms[ix])
        s.addrs=sorted(set(s.tab.values()))
    def sect(s,name):
        return [x for x in s.sects if x[1]==name]
    def dis(s,name_or_va,maxn=0):
        from capstone import Cs,CS_ARCH_X86,CS_MODE_64
        va=s.tab[name_or_va] if isinstance(name_or_va,str) else name_or_va
        i=bisect.bisect_right(s.addrs,va); nxt=s.addrs[i] if i<len(s.addrs) else va+0x400
        if maxn: nxt=min(nxt,va+maxn)
        print('==== %s %x size %x'%(s.byaddr.get(va,''),va,nxt-va))
        md=Cs(CS_ARCH_X86,CS_MODE_64)
        for i in md.disasm(rd(va,nxt-va),va):
            o=i.op_str; ann=''
            m2=re.search(r'rip ([+-]) 0x([0-9a-f]+)\]',o)
            t=None
            if m2: t=i.address+i.size+(int(m2.group(2),16) if m2.group(1)=='+' else -int(m2.group(2),16))
            elif i.mnemonic in('call','jmp') and o.startswith('0x'): t=int(o,16)
            if t is not None:
                ann='  ; -> %x %s'%(t,s.byaddr.get(t,''))
                if t not in s.byaddr and m2:
                    try:
                        c=cstr(t,80)
                        if len(c)>3 and all(32<=ord(ch)<127 for ch in c): ann+=' "%s"'%c
                        else:
                            q=struct.unpack('<Q',rd(t,8))[0]
                            try:
                                c=cstr(q,80)
                                if len(c)>3 and all(32<=ord(ch)<127 for ch in c): ann+=' ->"%s"'%c
                            except Exception: pass
                    except Exception: pass
            print('%x: %s %s%s'%(i.address,i.mnemonic,o,ann))
    def strings(s,pat):
        out=[]
        for seg,sn,a,z,fl,r1,r2 in s.sects:
            if sn in('__cstring','__const','__objc_methname','__objc_classname','__oslogstring','__data'):
                d=rd(a,z)
                for m in re.finditer(rb'[\x20-\x7e]{4,}',d):
                    t=m.group().decode()
                    if re.search(pat,t): out.append((a+m.start(),t))
        return out
if __name__=='__main__':
    if sys.argv[1]=='images':
        for p in find(sys.argv[2]): print('%x %s'%(images[p],p))
    elif sys.argv[1]=='syms':
        im=Img(find(sys.argv[2])[0])
        for k in sorted(im.tab):
            if re.search(sys.argv[3],k): print('%x %s'%(im.tab[k],k))
    elif sys.argv[1]=='allsyms':
        im=Img(find(sys.argv[2])[0])
        for k in sorted(set(im.allsyms)):
            if re.search(sys.argv[3],k): print(k)
    elif sys.argv[1]=='str':
        im=Img(find(sys.argv[2])[0])
        for a,t in im.strings(sys.argv[3]): print('%x %s'%(a,t))
    elif sys.argv[1]=='dis':
        im=Img(find(sys.argv[2])[0])
        for n in sys.argv[3:]: im.dis(int(n,16) if re.match(r'^[0-9a-f]{9,}$',n) else n)

def xrefs(im, target):
    """addresses of rip-relative references to target in im's __text"""
    res=[]
    for seg,sn,a,z,fl,r1,r2 in im.sects:
        if sn!='__text': continue
        d=rd(a,z); import array
        t=struct.pack('<q',0)
        # brute force: rel32 at offset o means target = a+o+4+rel
        mv=memoryview(d)
        for o in range(0,z-4):
            rel=target-(a+o+4)
            if -0x80000000<=rel<0x80000000 and d[o]==(rel&0xff) and d[o+1]==((rel>>8)&0xff) and d[o+2]==((rel>>16)&0xff) and d[o+3]==((rel>>24)&0xff):
                res.append(a+o)
    return res
def cfstr(im, s):
    """address of the CFString object whose characters are s"""
    out=[]
    for a,t in im.strings('^'+re.escape(s)+'$'):
        for seg,sn,sa,z,fl,r1,r2 in im.sects:
            if sn=='__cfstring':
                d=rd(sa,z)
                for o in range(0,z,32):
                    p=struct.unpack_from('<Q',d,o+16)[0]
                    if p==a or (p&0xffffffffffff)==a or (p&0x7fffffffffff)==a: out.append(sa+o)
    return out
def func_of(im, va):
    i=bisect.bisect_right(im.addrs,va)-1
    return im.addrs[i], im.byaddr.get(im.addrs[i])

BASE=maps[0][0]
def ptr(va):
    """decode a (chained-fixup) pointer stored at va"""
    p=struct.unpack('<Q',rd(va,8))[0]
    if p==0: return 0
    return BASE+(p&0x3ffffffff)
def cfstr(im, s):
    out=[]
    for a,t in im.strings('^'+re.escape(s)+'$'):
        for seg,sn,sa,z,fl,r1,r2 in im.sects:
            if sn=='__cfstring':
                for o in range(0,z,32):
                    if ptr(sa+o+16)==a: out.append(sa+o)
    return out
def vtable(im, name, n=60):
    a=im.tab[name]; r=[]
    for i in range(2,n):
        p=ptr(a+i*8); nm=im.byaddr.get(p)
        if not nm: break
        r.append(((i-2)*8,p,nm))
    return r
