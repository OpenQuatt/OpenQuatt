#!/usr/bin/env python3
"""Audit the row-source pointer order of the pinned ARM constructor.

Narrow static register/literal analysis of memcpy sites, not a complete CPU or
CiC execution emulator. Requires llvm-objdump on PATH (or --objdump path).
The fixed ELF SHA256 is mandatory; an unknown build is rejected.
"""
from __future__ import annotations
import argparse, hashlib, json, re, shutil, struct, subprocess
from pathlib import Path
from build_v2_model import ELF_SHA256, ROW_MAP

def trace(elf: Path, objdump: str) -> list[dict]:
    blob=elf.read_bytes()
    if hashlib.sha256(blob).hexdigest()!=ELF_SHA256:raise ValueError('Unexpected ELF SHA256')
    result=subprocess.run([objdump,'-d','--no-show-raw-insn','--start-address=0x13498c',
                           '--stop-address=0x13a800',str(elf)],check=True,capture_output=True,text=True)
    regs={f'r{i}':None for i in range(13)}|{'sp':None,'lr':None,'pc':None}
    paths={key:[] for key in regs};copies=[]
    def set_reg(name,value,path=None):regs[name]=value;paths[name]=[] if path is None else list(path)
    for line in result.stdout.splitlines():
        match=re.match(r'\s*([0-9a-f]+):\s+([a-z][a-z0-9.]*)\s*(.*)',line)
        if not match:continue
        addr=int(match[1],16);op=match[2];args=match[3].split('@')[0].strip()
        tokens=[part.strip() for part in args.split(',')]
        if addr>0x139a50:break
        if op=='ldr' and '[pc,' in args:
            lm=re.search(r'@ 0x([0-9a-f]+)',line)
            if lm:
                literal=int(lm[1],16);value=struct.unpack_from('<I',blob,literal-0x10000)[0]
                set_reg(tokens[0],value,[dict(instruction=hex(addr),literal=hex(literal),value=hex(value))])
        elif op in ('add','sub') and len(tokens)==3 and tokens[0] in regs:
            a=regs.get(tokens[1]);b=int(tokens[2][1:],0) if tokens[2].startswith('#') else regs.get(tokens[2])
            if a is not None and b is not None:
                set_reg(tokens[0],a+b if op=='add' else a-b,paths[tokens[1]]+[dict(instruction=hex(addr),op=op,delta=b)])
            else:set_reg(tokens[0],None)
        elif op=='mov' and len(tokens)==2:
            if tokens[1].startswith('#'):set_reg(tokens[0],int(tokens[1][1:],0))
            elif tokens[1] in regs:set_reg(tokens[0],regs[tokens[1]],paths[tokens[1]])
        elif op=='bl':
            if args.startswith('0x123e8') and regs['r2']==80:
                pointer=regs['r1']
                group='P' if pointer and 0x210668<=pointer<0x212af8 else ('C' if pointer and 0x212af8<=pointer<0x214f88 else '?')
                base=0x210668 if group=='P' else 0x212af8
                if group=='?' or (pointer-base)%80:raise ValueError(f'Unresolved row at {addr:#x}')
                copies.append(dict(copy_va=hex(addr),pointer_va=hex(pointer),group=group,
                                   row=(pointer-base)//80,trace=paths['r1']))
            for name in ['r0','r1','r2','r3','r12','lr']:set_reg(name,None)
        elif op.startswith('ldm'):
            for name in re.findall(r'\b(?:r\d+|lr)\b',args.split('{')[-1]):set_reg(name,None)
            if '!' in args:set_reg(tokens[0].strip('!'),None)
        elif op in ['ldr','ldrh','ldrb','movw','movt','lsl','lsr','and','orr','mul','rsb'] or op.startswith('mov'):
            if tokens[0] in regs:set_reg(tokens[0],None)
    for group in ['P','C']:
        if [row['row'] for row in copies if row['group']==group]!=sum(ROW_MAP,[]):
            raise ValueError(f'Constructor row order/count mismatch for {group}')
    if len(copies)!=272:raise ValueError('Expected 272 power/COP copy sites')
    return copies

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf',type=Path,required=True)
    parser.add_argument('--objdump',default=shutil.which('llvm-objdump'))
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args()
    if not args.objdump:parser.error('llvm-objdump not found; supply --objdump')
    try:
        rows=trace(args.elf,args.objdump);args.out.parent.mkdir(parents=True,exist_ok=True)
        args.out.write_text(json.dumps(rows,indent=2)+'\n')
    except (OSError,ValueError,subprocess.CalledProcessError) as exc:parser.exit(1,f'ERROR: {exc}\n')
    print('PASS: 136 thermal + 136 COP logical rows; complete 8 x 17 row layout verified')
if __name__=='__main__':main()
