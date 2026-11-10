import struct, sys, bisect
from capstone import *
import os
# Copy of /System/Library/KernelCollections/SystemKernelExtensions.kc from the machine.
# OFFS are the file offsets of the fileset entries in the macOS 15.8.2 (24H232) collection.
KC=os.environ.get('KC_PATH','dl/kc/SystemKernelExtensions.kc')
OFFS={'hwlibs':204386304,'accel':199462912,'hwsvc':204357632}
f=open(KC,'rb')
def load(off):
    f.seek(off); h=f.read(32); magic,_,_,_,ncmds,szc,_,_=struct.unpack('<8I',h)
    segs=[];syms=None;p=off+32
    for i in range(ncmds):
        f.seek(p); cmd,sz=struct.unpack('<II',f.read(8)); f.seek(p); d=f.read(sz)
        if cmd==0x19:
            name=d[8:24].rstrip(b'\0').decode(); vmaddr,vmsize,fileoff,filesize=struct.unpack('<4Q',d[24:56]); segs.append((name,vmaddr,vmsize,fileoff,filesize))
        elif cmd==2:
            symoff,nsyms,stroff,strsize=struct.unpack('<4I',d[8:24]); syms=(symoff,nsyms,stroff,strsize)
        p+=sz
    symoff,nsyms,stroff,strsize=syms
    f.seek(stroff); strs=f.read(strsize); f.seek(symoff); sd=f.read(nsyms*16)
    tab={}
    for i in range(nsyms):
        strx,typ,sect,desc,val=struct.unpack('<IBBHQ',sd[i*16:i*16+16])
        if typ&0xe0 or (typ&0xe)!=0xe: continue
        e=strs.index(b'\0',strx); tab[strs[strx:e].decode()]=val
    return segs,tab
def rd(segs,va,n):
    for name,vmaddr,vmsize,fileoff,filesize in segs:
        if vmaddr<=va<vmaddr+vmsize:
            f.seek(fileoff+va-vmaddr); return f.read(n)
    raise Exception('unmapped %x'%va)
if __name__=='__main__':
    segs,tab=load(OFFS[sys.argv[1]])
    addrs=sorted(set(tab.values())); byaddr={}
    for k,v in tab.items(): byaddr.setdefault(v,k)
    if sys.argv[2]=='-g':
        import re
        for k in sorted(tab): 
            if re.search(sys.argv[3],k): print('%x %s'%(tab[k],k))
        sys.exit()
    md=Cs(CS_ARCH_X86,CS_MODE_64); 
    for name in sys.argv[2:]:
        va=tab[name]; nxt=addrs[bisect.bisect_right(addrs,va)]
        print('==== %s %x size %x'%(name,va,nxt-va))
        for i in md.disasm(rd(segs,va,nxt-va),va):
            s=i.op_str
            import re
            m=re.search(r'0x(ffffff[0-9a-f]{10})',s)
            ann=''
            if m and int(m.group(1),16) in byaddr: ann='  ; '+byaddr[int(m.group(1),16)]
            m2=re.search(r'rip ([+-]) 0x([0-9a-f]+)\]',s)
            if m2:
                t=i.address+i.size+(int(m2.group(2),16) if m2.group(1)=='+' else -int(m2.group(2),16))
                ann='  ; -> %x %s'%(t,byaddr.get(t,''))
            print('%x: %s %s%s'%(i.address,i.mnemonic,s,ann))
