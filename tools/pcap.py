#!/usr/bin/env python3
import struct, sys
p=sys.argv[1]
d=open(p,'rb').read()
magic=d[:4]
if magic==b'\xd4\xc3\xb2\xa1': endian='<'
elif magic==b'\xa1\xb2\xc3\xd4': endian='>'
else: raise SystemExit('bad pcap')
off=24; i=0
while off+16<=len(d):
    ts,us,incl,orig=struct.unpack(endian+'IIII',d[off:off+16]); off+=16
    pkt=d[off:off+incl]; off+=incl; i+=1
    if len(pkt)<14: continue
    et=struct.unpack('!H',pkt[12:14])[0]
    if et!=0x0800 or len(pkt)<34: 
        print(i,'eth',hex(et)); continue
    ip=pkt[14:]; ihl=(ip[0]&15)*4; proto=ip[9]
    src='.'.join(map(str,ip[12:16])); dst='.'.join(map(str,ip[16:20]))
    if proto==1:
        t=ip[ihl]; print(i,'ICMP',src,'->',dst,'type',t)
    elif proto==17 and len(ip)>=ihl+8:
        udp=ip[ihl:]; sp,dp,ln=struct.unpack('!HHH',udp[:6])
        print(i,'UDP',src,sp,'->',dst,dp,'len',ln)
        if sp==53 or dp==53:
            dns=udp[8:]
            if len(dns)>=12:
                qd=struct.unpack('!H',dns[4:6])[0]
                print('   DNS qd',qd,'flags',hex(struct.unpack('!H',dns[2:4])[0]),'payload',dns[:64].hex())
    else:
        print(i,'IP proto',proto,src,'->',dst)
