#!/usr/bin/env python3
"""Prepara el ejecutable iOS (Mach-O 32 bits, MH_EXECUTE no PIE) para el loader de la Vita.

El ejecutable está enlazado en 0x1000 y no trae relocs locales (nlocrel = 0). El código es PIC
(ldr/add pc), así que lo único que hay que reubicar son punteros absolutos en datos. Este script
los encuentra validando cada palabra candidata contra la tabla de símbolos (el binario no tiene
strip) y escribe rebase.bin: u32 magic 'RBS1', u32 count, count * u32 vmaddr de la palabra.

Los imports (__nl/__la_symbol_ptr + relocs externas) los resuelve el loader en la consola leyendo
el propio Mach-O. Acá solo se listan para saber qué implementar.

Uso: python3 -I macho_prep.py <Inotia2> <rebase.bin> [--report report.txt]
"""
import bisect, struct, sys
from collections import Counter

LC_SEGMENT, LC_SYMTAB, LC_DYSYMTAB = 0x1, 0x2, 0xB
S_ZEROFILL, S_NON_LAZY, S_LAZY = 0x1, 0x6, 0x7
INDIRECT_LOCAL, INDIRECT_ABS = 0x80000000, 0x40000000
N_STAB, N_TYPE, N_SECT, N_EXT = 0xE0, 0x0E, 0x0E, 0x01
N_ARM_THUMB_DEF = 0x0008


def parse(path):
    d = open(path, 'rb').read()
    magic, cpu, sub, ftype, ncmds, _sz, flags = struct.unpack_from('<7I', d, 0)
    assert magic == 0xFEEDFACE and ftype == 2, 'se espera MH_EXECUTE de 32 bits'
    segs, sects, symtab, dysym = [], [], None, None
    off = 28
    for _ in range(ncmds):
        cmd, size = struct.unpack_from('<2I', d, off)
        if cmd == LC_SEGMENT:
            name = d[off + 8:off + 24].rstrip(b'\0').decode()
            vmaddr, vmsize, fileoff, filesize, _mp, _ip, nsects, _f = struct.unpack_from('<8I', d, off + 24)
            segs.append(dict(name=name, vmaddr=vmaddr, vmsize=vmsize, fileoff=fileoff, filesize=filesize))
            so = off + 56
            for _ in range(nsects):
                sn = d[so:so + 16].rstrip(b'\0').decode()
                gn = d[so + 16:so + 32].rstrip(b'\0').decode()
                addr, ssize, soff, _al, _ro, _nr, sflags, r1, r2 = struct.unpack_from('<9I', d, so + 32)
                sects.append(dict(seg=gn, name=sn, addr=addr, size=ssize, offset=soff,
                                  type=sflags & 0xFF, r1=r1))
                so += 68
        elif cmd == LC_SYMTAB:
            symtab = struct.unpack_from('<4I', d, off + 8)
        elif cmd == LC_DYSYMTAB:
            dysym = struct.unpack_from('<18I', d, off + 8)
        off += size
    return d, segs, sects, symtab, dysym


def main():
    src, out = sys.argv[1], sys.argv[2]
    report = sys.argv[sys.argv.index('--report') + 1] if '--report' in sys.argv else None
    d, segs, sects, (symoff, nsyms, stroff, _strsize), dysym = parse(src)
    lo = min(s['vmaddr'] for s in segs if s['vmsize'] and s['name'] != '__PAGEZERO')
    hi = max(s['vmaddr'] + s['vmsize'] for s in segs if s['name'] != '__LINKEDIT')

    def read32(va):
        for s in segs:
            if s['vmaddr'] <= va < s['vmaddr'] + s['filesize']:
                return struct.unpack_from('<I', d, s['fileoff'] + va - s['vmaddr'])[0]
        return 0

    def sect_of(va):
        for s in sects:
            if s['addr'] <= va < s['addr'] + s['size']:
                return s
        return None

    # Símbolos definidos (incluye locales: el binario no tiene strip).
    syms, names = [], []
    for i in range(nsyms):
        strx, ntype, nsect, ndesc, value = struct.unpack_from('<IBBhI', d, symoff + i * 12)
        name = d[stroff + strx:d.index(b'\0', stroff + strx)].decode('latin1')
        names.append(name)
        if ntype & N_STAB or (ntype & N_TYPE) != N_SECT:
            continue
        syms.append((value, bool(ndesc & N_ARM_THUMB_DEF), name))
    syms.sort()
    sym_addrs = [s[0] for s in syms]
    text = next(s for s in sects if s['name'] == '__text')
    func_entry = {}  # dirección de entrada (con bit thumb) -> nombre
    for a, thumb, n in syms:
        if text['addr'] <= a < text['addr'] + text['size']:
            func_entry[a | 1 if thumb else a] = n
    stubs = next((s for s in sects if s['name'] == '__symbol_stub4'), None)
    if stubs:  # punteros a stubs de imports (p. ej. tablas de funciones) también son válidos
        for a in range(stubs['addr'], stubs['addr'] + stubs['size'], 12):
            func_entry[a] = 'stub'

    def valid_target(v):
        """Devuelve la clase de destino si v parece un puntero real, si no None."""
        if not (lo <= v < hi):
            return None
        s = sect_of(v & ~1)
        if s is None:
            return None
        if s['name'] in ('__text', '__symbol_stub4'):
            return 'func' if v in func_entry else None
        if s['name'] in ('__cstring', '__ustring'):
            ok = v == s['addr'] or read32(v - 1) & 0xFF == 0 if s['name'] == '__cstring' else True
            return 'cstr' if ok else None
        if v & 3 and s['name'] not in ('__const', '__data'):
            return None
        i = bisect.bisect_right(sym_addrs, v) - 1
        if i >= 0 and sym_addrs[i] == v:
            return 'sym'
        if s['type'] == S_ZEROFILL or s['name'] in ('__common', '__bss'):
            return 'bss-interior' if i >= 0 else None
        return 'interior' if v == s['addr'] or (i >= 0 and v - sym_addrs[i] < 0x10000) else None

    # Slots resueltos por el loader en la consola: no van a rebase.bin.
    indirectsymoff, nindirect = dysym[12], dysym[13]
    extreloff, nextrel = dysym[14], dysym[15]
    handled, imports = set(), Counter()
    local_ptr_slots = []
    for s in sects:
        if s['type'] in (S_NON_LAZY, S_LAZY):
            for i in range(s['size'] // 4):
                idx = struct.unpack_from('<I', d, indirectsymoff + (s['r1'] + i) * 4)[0]
                slot = s['addr'] + i * 4
                handled.add(slot)
                if idx & INDIRECT_LOCAL:
                    local_ptr_slots.append(slot)
                elif not idx & INDIRECT_ABS:
                    imports[names[idx]] += 1
    extrel_slots = []
    for i in range(nextrel):
        addr, info = struct.unpack_from('<iI', d, extreloff + i * 8)
        symnum = info & 0xFFFFFF
        va = addr  # r_address es relativo al 1er segmento (__PAGEZERO, vmaddr 0)
        extrel_slots.append((va, names[symnum]))
        handled.add(va)
        imports[names[symnum]] += 1

    rebase, stats, rejected, rejected_list = [], Counter(), Counter(), []
    scan = [s for s in sects if s['type'] != S_ZEROFILL and s['name'] not in ('__text', '__symbol_stub4', '__cstring', '__ustring')
            and s['seg'] in ('__DATA', '__TEXT')]
    # Por objeto (símbolo dueño) en __data/__const: si tiene más palabras "en rango pero
    # inválidas" que punteros válidos, es una tabla de texto/enteros y sus aciertos son casuales.
    owner_ok, owner_bad, owner_hits = Counter(), Counter(), {}
    for s in scan:
        for va in range(s['addr'] + (-s['addr'] & 3), s['addr'] + s['size'] - 3, 4):
            if va in handled:
                continue
            v = read32(va)
            k = valid_target(v)
            owner = None
            if s['name'] in ('__data', '__const'):
                i = bisect.bisect_right(sym_addrs, va) - 1
                owner = syms[i][2] if i >= 0 and sym_addrs[i] >= s['addr'] else f'{s["seg"]}.{s["name"]}'
            if k:
                if owner:
                    owner_ok[owner] += 1
                    owner_hits.setdefault(owner, []).append((va, s['name'], k))
                else:
                    rebase.append(va)
                    stats[(s['name'], k)] += 1
            elif lo <= v < hi:
                rejected[s['name']] += 1
                rejected_list.append((va, v))
                if owner:
                    owner_bad[owner] += 1
    dropped = []
    for owner, hits in owner_hits.items():
        if owner_bad[owner] > owner_ok[owner]:
            dropped.append((owner, owner_ok[owner], owner_bad[owner]))
            continue
        for va, sn, k in hits:
            rebase.append(va)
            stats[(sn, k)] += 1
    # Los slots locales de __nl_symbol_ptr también son direcciones absolutas: se rebasean.
    rebase += local_ptr_slots
    stats[('__nl_symbol_ptr', 'local')] = len(local_ptr_slots)
    rebase.sort()

    with open(out, 'wb') as f:
        f.write(struct.pack('<4sI', b'RBS1', len(rebase)))
        f.write(struct.pack(f'<{len(rebase)}I', *rebase))

    lines = [f'imagen: {lo:#x}-{hi:#x}', f'rebase: {len(rebase)} palabras -> {out}']
    lines += [f'  {sn:20} {k:14} {n}' for (sn, k), n in sorted(stats.items())]
    lines += [f'rechazadas (en rango, sin símbolo válido): {dict(rejected)}']
    lines += [f'objetos descartados (aciertos casuales en tablas de texto/enteros): {len(dropped)}']
    lines += [f'  {o} ok={a} malas={b}' for o, a, b in sorted(dropped, key=lambda x: -x[1])]
    lines += [f'relocs externas: {len(extrel_slots)}; imports únicos: {len(imports)}']
    lines += [f'  {n}' for n in sorted(imports)]
    lines += ['rechazadas (addr valor):'] + [f'  {a:#x} {v:#x}' for a, v in rejected_list]
    print('\n'.join(lines[:len(stats) + 4]))
    if report:
        open(report, 'w').write('\n'.join(lines) + '\n')


if __name__ == '__main__':
    main()
