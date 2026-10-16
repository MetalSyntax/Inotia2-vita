#!/usr/bin/env python3
"""Sincroniza ux0_data/inotia2/ con ux0:data/inotia2/ de la Vita por FTP (VitaShell, puerto 1337).

- Sube solo lo que falta o cambió de tamaño (los .c2e/.png no cambian entre corridas).
- Migra la estructura plana vieja: borra lo suelto en ux0:data/inotia2/ que no sea parte de la
  nueva (Inotia2.app/, rebase.bin, saves/, logs/). En saves/ (partidas) nunca se sube ni se borra
  nada; un Documents/ de la estructura vieja se renombra a saves/.
- --logs: solo baja los logs/log_NNN.log nuevos o que cambiaron y los .psp2dmp de ux0:data/ a logs/.

Uso: python3 -I tools/deploy_data.py [--ip 192.168.x.y] [--logs] [--dry-run]
"""
import ftplib, json, os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOCAL = os.path.join(ROOT, 'ux0_data', 'inotia2')
REMOTE = '/ux0:/data/inotia2'
KEEP = {'Inotia2.app', 'rebase.bin', 'saves', 'logs'}
NO_PUSH = {'saves', 'logs'}  # los escribe la consola


def ls(ftp, path):
    """{nombre: (es_dir, tamaño)} parseando LIST (VitaShell no implementa MLSD)."""
    out = {}
    try:  # el FTP de VitaShell ignora el argumento de LIST: hay que entrar al directorio
        ftp.cwd(path)
        lines = []
        ftp.retrlines('LIST', lines.append)
    except ftplib.error_perm:
        return None
    for l in lines:
        p = l.split(None, 8)
        if len(p) == 9 and p[8] not in ('.', '..'):
            out[p[8]] = (l[0] == 'd', int(p[4]))
    return out


def rmtree(ftp, path, dry):
    for n, (d, _) in (ls(ftp, path) or {}).items():
        rmtree(ftp, f'{path}/{n}', dry) if d else (dry or ftp.delete(f'{path}/{n}'))
    print('  rm', path)
    dry or ftp.rmd(path)


def push(ftp, local, remote, dry, stats):
    have = ls(ftp, remote)
    if have is None:
        print('  mkdir', remote)
        dry or ftp.mkd(remote)
        have = {}
    for n in sorted(os.listdir(local)):
        if n.startswith('._') or n == '.DS_Store':
            continue
        lp, rp = os.path.join(local, n), f'{remote}/{n}'
        if remote == REMOTE and n in NO_PUSH:
            if n not in have:
                print('  mkdir', rp)
                dry or ftp.mkd(rp)
            continue
        if os.path.isdir(lp):
            push(ftp, lp, rp, dry, stats)
        elif have.get(n) != (False, os.path.getsize(lp)):
            print('  put', rp)
            stats[0] += 1
            if not dry:
                with open(lp, 'rb') as f:
                    ftp.storbinary(f'STOR {rp}', f)
        else:
            stats[1] += 1


def pull_logs(ftp):
    dst = os.path.join(ROOT, 'logs')
    os.makedirs(dst, exist_ok=True)
    got = [(f'{REMOTE}/logs/{n}', n, sz) for n, (d, sz) in (ls(ftp, f'{REMOTE}/logs') or {}).items() if n.endswith('.log')]
    got += [(f'/ux0:/data/{n}', n, sz) for n, (d, sz) in (ls(ftp, '/ux0:/data') or {}).items() if n.endswith('.psp2dmp')]
    for rp, n, sz in sorted(got):
        lp = os.path.join(dst, n)
        if os.path.isfile(lp) and os.path.getsize(lp) == sz:
            continue
        try:
            with open(os.path.join(dst, n), 'wb') as f:
                ftp.retrbinary(f'RETR {rp}', f.write)
            print('  get', rp, '->', f'logs/{n}')
        except ftplib.error_perm:
            os.remove(os.path.join(dst, n))
            print('  (no existe)', rp)


def main():
    cfg = json.load(open(os.path.join(ROOT, '.psvita-toolkit.json')))
    ip = sys.argv[sys.argv.index('--ip') + 1] if '--ip' in sys.argv else cfg['vita_ip']
    dry = '--dry-run' in sys.argv
    ftp = ftplib.FTP()
    ftp.connect(ip, cfg.get('vita_port', 1337), timeout=15)
    ftp.login()
    if '--logs' in sys.argv:
        return pull_logs(ftp)
    if not os.path.isfile(os.path.join(LOCAL, 'Inotia2.app', 'Inotia2')):
        sys.exit('Falta ux0_data/inotia2/Inotia2.app: correr tools/stage_data.sh primero')
    remote = ls(ftp, REMOTE) or {}
    if 'Documents' in remote and 'saves' not in remote:  # partidas de la estructura vieja
        print('  mv', f'{REMOTE}/Documents', '->', 'saves')
        dry or ftp.rename(f'{REMOTE}/Documents', f'{REMOTE}/saves')
        remote['saves'] = remote.pop('Documents')
    for n, (d, _) in remote.items():
        if n == 'log.txt' and not dry:  # log de la estructura vieja: se baja antes de borrarlo
            os.makedirs(os.path.join(ROOT, 'logs'), exist_ok=True)
            with open(os.path.join(ROOT, 'logs', 'log_estructura-vieja.log'), 'wb') as f:
                ftp.retrbinary(f'RETR {REMOTE}/log.txt', f.write)
            print('  get', f'{REMOTE}/log.txt', '-> logs/log_estructura-vieja.log')
        if n not in KEEP:  # restos de la estructura plana
            rmtree(ftp, f'{REMOTE}/{n}', dry) if d else (print('  rm', f'{REMOTE}/{n}'), dry or ftp.delete(f'{REMOTE}/{n}'))
    stats = [0, 0]
    push(ftp, LOCAL, REMOTE, dry, stats)
    print(f'subidos {stats[0]}, sin cambios {stats[1]}' + (' (dry-run)' if dry else ''))
    ftp.quit()


if __name__ == '__main__':
    main()
