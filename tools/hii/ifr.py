import struct,sys,uuid
d=open(sys.argv[1],'rb').read()
def guid(b): return str(uuid.UUID(bytes_le=bytes(b)))
lists=[]; off=0
while off+20<=len(d):
    g=d[off:off+16]; ln=struct.unpack_from('<I',d,off+16)[0]
    if ln<20 or off+ln>len(d): break
    lists.append((off,guid(g),ln)); off+=ln
print('package lists',len(lists),'consumed',off,'of',len(d),file=sys.stderr)
def strings(pk):
    hdrsize,sio=struct.unpack_from('<II',pk,4)
    lang=pk[4+8+32+2:hdrsize].split(b'\0')[0].decode('ascii','replace')
    p=sio; sid=1; out={}
    while p<len(pk):
        t=pk[p]
        if t==0x00: break
        elif t==0x14:
            e=p+1
            while pk[e:e+2]!=b'\0\0': e+=2
            out[sid]=pk[p+1:e].decode('utf-16le','replace'); sid+=1; p=e+2
        elif t==0x15:
            e=p+2
            while pk[e:e+2]!=b'\0\0': e+=2
            out[sid]=pk[p+2:e].decode('utf-16le','replace'); sid+=1; p=e+2
        elif t==0x16:
            n=struct.unpack_from('<H',pk,p+1)[0]; e=p+3
            for _ in range(n):
                s=e
                while pk[e:e+2]!=b'\0\0': e+=2
                out[sid]=pk[s:e].decode('utf-16le','replace'); sid+=1; e+=2
            p=e
        elif t==0x10:
            e=pk.index(b'\0',p+1); out[sid]=pk[p+1:e].decode('latin1'); sid+=1; p=e+1
        elif t==0x20: sid+=struct.unpack_from('<H',pk,p+1)[0]; p+=3
        elif t==0x21: sid+=pk[p+1]; p+=2
        elif t==0x30: out[sid]=out.get(struct.unpack_from('<H',pk,p+1)[0],''); sid+=1; p+=3
        elif t==0x40:
            bt=pk[p+1]
            if bt==0x30: ln=pk[p+2]
            elif bt==0x31: ln=struct.unpack_from('<H',pk,p+2)[0]
            else: ln=struct.unpack_from('<I',pk,p+2)[0]
            p+=ln
        else:
            print('unknown string block',hex(t),'at',p,file=sys.stderr); break
    return lang,out
res=[]
for (lo,lg,ln) in lists:
    p=lo+20; S={}; forms=[]
    while p<lo+ln:
        h=struct.unpack_from('<I',d,p)[0]; pl=h&0xffffff; pt=h>>24
        if pl<4: break
        pk=d[p:p+pl]
        if pt==4:
            lang,o=strings(pk)
            if lang.lower().startswith('en') or not S: S=o
        elif pt==2: forms.append(pk[4:])
        elif pt==0xdf: break
        p+=pl
    res.append((lg,S,forms))
def s(S,i): return S.get(i,'<%d>'%i)
for lg,S,forms in res:
    for f in forms:
        p=0; depth=0; vs={}; cond=[]
        while p<len(f):
            op=f[p]; l=f[p+1]&0x7f; scope=f[p+1]>>7
            if l<2: break
            b=f[p:p+l]; ind='  '*depth
            if op==0x0e: print(f"{ind}FormSet {guid(b[2:18])} title={s(S,struct.unpack_from('<H',b,18)[0])!r}")
            elif op==0x01: print(f"{ind}Form id={struct.unpack_from('<H',b,2)[0]:#x} title={s(S,struct.unpack_from('<H',b,4)[0])!r}")
            elif op==0x24:
                g=guid(b[2:18]); vid,sz=struct.unpack_from('<HH',b,18); nm=b[22:].split(b'\0')[0].decode('latin1'); vs[vid]=nm
                print(f"{ind}VarStore id={vid:#x} guid={g} size={sz:#x} name={nm}")
            elif op==0x26:
                vid=struct.unpack_from('<H',b,2)[0]; g=guid(b[4:20]); attr,sz=struct.unpack_from('<IH',b,20); nm=b[26:].split(b'\0')[0].decode('latin1'); vs[vid]=nm
                print(f"{ind}VarStoreEfi id={vid:#x} guid={g} attr={attr:#x} size={sz:#x} name={nm}")
            elif op in (0x05,0x06,0x07,0x1c):
                pr,hl,qid,vid,vo=struct.unpack_from('<HHHHH',b,2); fl=b[12]
                kind={5:'OneOf',6:'CheckBox',7:'Numeric',0x1c:'String'}[op]
                print(f"{ind}{kind} {s(S,pr)!r} var={vs.get(vid,hex(vid))} off={vo:#x} qid={qid:#x} flags={fl:#x} help={s(S,hl)[:70]!r}")
            elif op==0x09:
                st=struct.unpack_from('<H',b,2)[0]; fl=b[4]; ty=b[5]; val=int.from_bytes(b[6:6+{0:1,1:2,2:4,3:8}.get(ty,1)],'little')
                print(f"{ind}  Option {s(S,st)!r} = {val:#x}{' (default)' if fl&0x10 else ''}{' (mfg default)' if fl&0x20 else ''}")
            elif op==0x5b:
                did=struct.unpack_from('<H',b,2)[0]; ty=b[4]; val=int.from_bytes(b[5:5+{0:1,1:2,2:4,3:8}.get(ty,1)],'little')
                print(f"{ind}  Default id={did} = {val:#x}")
            elif op==0x0a: print(f"{ind}SuppressIf")
            elif op==0x19: print(f"{ind}GrayOutIf")
            elif op==0x0f:
                pr=struct.unpack_from('<H',b,2)[0]; fid=struct.unpack_from('<H',b,13)[0] if l>=15 else 0
                print(f"{ind}Ref {s(S,pr)!r} -> form {fid:#x}")
            elif op==0x02: print(f"{ind}Subtitle {s(S,struct.unpack_from('<H',b,2)[0])!r}")
            elif op==0x12: print(f"{ind}  EqIdVal qid={struct.unpack_from('<H',b,2)[0]:#x} val={struct.unpack_from('<H',b,4)[0]:#x}")
            elif op==0x14:
                q=struct.unpack_from('<H',b,2)[0]; n=struct.unpack_from('<H',b,4)[0]
                print(f"{ind}  EqIdValList qid={q:#x} vals={[hex(x) for x in struct.unpack_from('<%dH'%n,b,6)]}")
            elif op==0x46: print(f"{ind}  True")
            elif op==0x47: print(f"{ind}  False")
            if op==0x29: depth=max(0,depth-1)
            elif scope: depth+=1
            p+=l
