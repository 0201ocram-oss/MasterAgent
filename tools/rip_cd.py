"""Estrae tracce da un CD audio su Windows, con doppia lettura di verifica.

  python tools/rip_cd.py toc D: [--save disc.json]       TOC, CD-Text e metadati MusicBrainz
  python tools/rip_cd.py rip D: <cartella> [--tracks 1,4,7] [--meta disc.json]

Lettura raw (IOCTL_CDROM_RAW_READ): ogni traccia viene letta due volte e i blocchi che
non coincidono vengono riletti finché una versione si ripete. Scrive FLAC 16 bit / 44,1 kHz,
senza alcun trattamento del segnale. L'offset di lettura del lettore non viene corretto
(qualche centinaio di campioni al massimo: irrilevante per l'analisi).
"""
import argparse
import base64
import ctypes
import ctypes.wintypes as wt
import hashlib
import json
import re
import sys
import threading
import time
import urllib.request
from pathlib import Path

import numpy as np
import soundfile as sf

k32 = ctypes.WinDLL('kernel32', use_last_error=True)
k32.CreateFileW.argtypes = [wt.LPCWSTR, wt.DWORD, wt.DWORD, ctypes.c_void_p, wt.DWORD, wt.DWORD, wt.HANDLE]
k32.CreateFileW.restype = wt.HANDLE
k32.DeviceIoControl.argtypes = [wt.HANDLE, wt.DWORD, ctypes.c_void_p, wt.DWORD,
                                ctypes.c_void_p, wt.DWORD, ctypes.POINTER(wt.DWORD), ctypes.c_void_p]
k32.DeviceIoControl.restype = wt.BOOL
k32.VirtualAlloc.argtypes = [ctypes.c_void_p, ctypes.c_size_t, wt.DWORD, wt.DWORD]
k32.VirtualAlloc.restype = ctypes.c_void_p
k32.CancelIoEx.argtypes = [wt.HANDLE, ctypes.c_void_p]
k32.CancelIoEx.restype = wt.BOOL
k32.CloseHandle.argtypes = [wt.HANDLE]
k32.OpenThread.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
k32.OpenThread.restype = wt.HANDLE
k32.CancelSynchronousIo.argtypes = [wt.HANDLE]
k32.CancelSynchronousIo.restype = wt.BOOL

IOCTL_CDROM_READ_TOC = 0x24000
IOCTL_CDROM_READ_TOC_EX = 0x24054
IOCTL_CDROM_RAW_READ = 0x2403E
SECTOR = 2352        # byte per settore audio = 588 frame stereo 16 bit
CHUNK = 20           # settori per lettura
DATA_GAP = 11400     # lead-out + lead-in prima della sessione dati di un CD Extra
CACHE_FLUSH = 1600   # settori letti altrove per svuotare la cache del lettore (~3,7 MB)
TIMEOUT_MS = 15000   # oltre questo tempo la lettura si annulla e il lettore viene riaperto


class RawReadInfo(ctypes.Structure):
    _fields_ = [('DiskOffset', ctypes.c_longlong), ('SectorCount', wt.ULONG), ('TrackMode', ctypes.c_int)]


class TocEx(ctypes.Structure):
    _fields_ = [('fmt', ctypes.c_ubyte), ('session', ctypes.c_ubyte), ('r1', ctypes.c_ubyte), ('r2', ctypes.c_ubyte)]


class Drive:
    """Ogni comando al lettore gira in un thread con un limite di tempo: alcuni lettori USB a volte
    non rispondono più a una lettura, e il driver tratterrebbe la chiamata per sempre."""

    def __init__(self, letter):
        self.path = '\\\\.\\' + letter.strip(':\\/') + ':'
        self.h = None
        self.reopen()

    def reopen(self):
        if self.h:
            k32.CloseHandle(self.h)
        self.h = k32.CreateFileW(self.path, 0x80000000, 3, None, 3, 0, None)
        if self.h in (None, wt.HANDLE(-1).value):
            raise OSError(ctypes.get_last_error(), 'impossibile aprire ' + self.path)
        # buffer nuovo a ogni apertura: un comando abbandonato potrebbe ancora scriverci
        self.buf = k32.VirtualAlloc(None, CHUNK * SECTOR, 0x3000, 0x04)

    def call(self, code, inp, out_ptr, out_size):
        """Esegue un IOCTL con timeout; restituisce i byte scritti, oppure None se fallisce o scade."""
        res = {}

        def work():
            res['tid'] = k32.GetCurrentThreadId()
            n = wt.DWORD()
            ok = k32.DeviceIoControl(self.h, code, ctypes.byref(inp) if inp is not None else None,
                                     ctypes.sizeof(inp) if inp is not None else 0,
                                     out_ptr, out_size, ctypes.byref(n), None)
            res['n'] = n.value if ok else None

        th = threading.Thread(target=work, daemon=True)
        th.start()
        th.join(TIMEOUT_MS / 1000)
        if th.is_alive():
            print(f'    il lettore non risponde da {TIMEOUT_MS // 1000} s: annullo e lo riapro', flush=True)
            k32.CancelIoEx(self.h, None)
            th.join(3)
            if th.is_alive() and 'tid' in res:
                ht = k32.OpenThread(0x0001, False, res['tid'])          # THREAD_TERMINATE
                if ht:
                    k32.CancelSynchronousIo(ht)
                    k32.CloseHandle(ht)
                th.join(3)
            if th.is_alive():
                print('    il comando resta appeso: lo abbandono e apro un nuovo collegamento al lettore', flush=True)
                self.h = None                                           # il vecchio handle resta al thread appeso
            self.reopen()
            time.sleep(2)
            return None
        return res.get('n')

    def ioctl(self, code, inp, out_size):
        out = ctypes.create_string_buffer(out_size)
        n = self.call(code, inp, out, out_size)
        return out.raw[:n] if n is not None else None

    def toc(self):
        raw = self.ioctl(IOCTL_CDROM_READ_TOC, None, 804)
        if raw is None:
            raise OSError(ctypes.get_last_error(), 'lettura TOC fallita (CD inserito?)')
        first, last = raw[2], raw[3]
        entries = []
        for i in range(last - first + 2):          # tracce + lead-out
            d = raw[4 + 8 * i: 12 + 8 * i]
            entries.append({'num': d[2], 'lba': (d[5] * 60 + d[6]) * 75 + d[7] - 150,
                            'data': bool(d[1] & 4), 'preemphasis': bool(d[1] & 1)})
        tracks, leadout = entries[:-1], entries[-1]['lba']
        for i, t in enumerate(tracks):
            nxt = tracks[i + 1] if i + 1 < len(tracks) else None
            t['end'] = nxt['lba'] if nxt else leadout
            if nxt and nxt['data'] and not t['data']:
                t['end'] -= DATA_GAP
            t['seconds'] = round((t['end'] - t['lba']) / 75, 2)
        return tracks, leadout

    def cdtext(self):
        raw = self.ioctl(IOCTL_CDROM_READ_TOC_EX, TocEx(5, 0, 0, 0), 65540)
        if not raw or len(raw) < 4:
            return {}
        packs = raw[4: 2 + ((raw[0] << 8) | raw[1])]
        out = {}
        for ptype, key in ((0x80, 'title'), (0x81, 'artist')):
            text, start = b'', None
            for i in range(0, len(packs) - 17, 18):
                p = packs[i: i + 18]
                if p[0] != ptype or (p[3] >> 4) & 7:     # solo il blocco 0 (prima lingua)
                    continue
                if start is None:
                    start = p[1] & 0x7F
                text += p[4:16]
            if start is None:
                continue
            prev = ''
            for k, s in enumerate(text.split(b'\0')):
                s = s.decode('latin-1')
                s = prev if s == '\t' else s.strip()      # '\t' = uguale alla traccia precedente
                if s:
                    out.setdefault(start + k, {})[key] = s
                prev = s
        return out

    def read(self, lba, count):
        info = RawReadInfo(lba * 2048, count, 2)          # 2 = CDDA
        n = self.call(IOCTL_CDROM_RAW_READ, info, ctypes.c_void_p(self.buf), count * SECTOR)
        return ctypes.string_at(self.buf, n) if n == count * SECTOR else None

    def read_robust(self, lba, count):
        """Legge `count` settori; se il blocco fallisce riprova, poi passa settore per settore."""
        for _ in range(3):
            t0 = time.time()
            d = self.read(lba, count)
            if time.time() - t0 > 5:
                print(f'    lettura lenta: settore {lba}, {time.time() - t0:.0f} s', flush=True)
            if d is not None:
                return d, 0
        print(f'    blocco illeggibile al settore {lba}: provo settore per settore', flush=True)
        out, bad = [], 0
        for s in range(lba, lba + count):
            d = None
            for _ in range(5):
                d = self.read(s, 1)
                if d is not None:
                    break
            if d is None:
                d, bad = bytes(SECTOR), bad + 1
            out.append(d)
        return b''.join(out), bad

    def flush_cache(self, lba):
        for b in range(lba, lba + CACHE_FLUSH, CHUNK):
            self.read(b, CHUNK)


def musicbrainz(tracks, leadout):
    sel = tracks[:-1] if len(tracks) > 1 and tracks[-1]['data'] else tracks    # CD Extra: solo sessione audio
    lead = sel[-1]['end'] + 150
    offs = {t['num']: t['lba'] + 150 for t in sel}
    first, last = sel[0]['num'], sel[-1]['num']
    s = '%02X%02X%08X' % (first, last, lead) + ''.join('%08X' % offs.get(i, 0) for i in range(1, 100))
    discid = base64.b64encode(hashlib.sha1(s.encode()).digest()).decode().translate(str.maketrans('+/=', '._-'))
    toc = '+'.join(str(v) for v in [first, last, lead] + [offs[n] for n in sorted(offs)])
    url = (f'https://musicbrainz.org/ws/2/discid/{discid}?toc={toc}'
           '&inc=recordings+artist-credits&cdstubs=no&fmt=json')
    req = urllib.request.Request(url, headers={'User-Agent': 'MasterAgentRipper/1.0 (uso personale)'})
    try:
        with urllib.request.urlopen(req, timeout=20) as r:
            js = json.load(r)
    except Exception as e:
        return {'discid': discid, 'error': str(e)}

    def credit(ac):
        return ''.join(a.get('name', '') + a.get('joinphrase', '') for a in ac or [])

    releases = []
    for rel in js.get('releases', []):
        media = rel.get('media', [])
        medium = next((m for m in media if any(d.get('id') == discid for d in m.get('discs', []))), None)
        medium = medium or next((m for m in media if m.get('track-count') == len(sel)), media[0] if media else {})
        releases.append({
            'album': rel.get('title'), 'artist': credit(rel.get('artist-credit')),
            'date': rel.get('date', ''), 'country': rel.get('country', ''),
            'disc': medium.get('position'), 'discs': len(media),
            'tracks': {str(t['position']): {'title': t.get('title'), 'artist': credit(t.get('artist-credit'))}
                       for t in medium.get('tracks', [])},
        })
    return {'discid': discid, 'releases': releases}


def safe_name(s):
    return re.sub(r'[<>:"/\\|?*\x00-\x1f]', '', s).strip().rstrip('.')


def rip_track(drv, t, audio_end):
    start, end = t['lba'], t['end']
    blocks = [(b, min(CHUNK, end - b)) for b in range(start, end, CHUNK)]
    far = (start + audio_end // 2) % max(audio_end - CACHE_FLUSH, 1)
    t0, unread = time.time(), 0

    data = []
    for i, (b, c) in enumerate(blocks):
        d, bad = drv.read_robust(b, c)
        data.append(d)
        unread += bad
        if i % 200 == 0:
            print(f'    lettura 1: {100 * i // len(blocks):3d}%', flush=True)
    drv.flush_cache(far)

    mismatched = {}
    for i, (b, c) in enumerate(blocks):
        d, _ = drv.read_robust(b, c)
        if d != data[i]:
            mismatched[i] = d
        if i % 200 == 0:
            print(f'    lettura 2: {100 * i // len(blocks):3d}%', flush=True)

    if mismatched:
        print(f'    blocchi diversi tra le due letture: {len(mismatched)}, li rileggo', flush=True)
    suspect = 0
    for i, d2 in mismatched.items():
        b, c = blocks[i]
        seen = {}
        for v in (data[i], d2):
            seen.setdefault(hashlib.sha1(v).digest(), [v, 0])[1] += 1
        for _ in range(8):
            drv.flush_cache(far)
            d, _ = drv.read_robust(b, c)
            e = seen.setdefault(hashlib.sha1(d).digest(), [d, 0])
            e[1] += 1
            if e[1] >= 3:
                break
        best = max(seen.values(), key=lambda e: e[1])
        data[i] = best[0]
        if best[1] < 3:
            suspect += 1

    pcm = np.frombuffer(b''.join(data), dtype='<i2').reshape(-1, 2)
    swapped = False
    mid = pcm[len(pcm) // 2: len(pcm) // 2 + 441000].astype(np.float64).mean(axis=1)
    if mid.std() > 0:
        r = np.corrcoef(mid[:-1], mid[1:])[0, 1]
        sw = pcm[len(pcm) // 2: len(pcm) // 2 + 441000].byteswap().astype(np.float64).mean(axis=1)
        if np.corrcoef(sw[:-1], sw[1:])[0, 1] > r + 0.3:   # lettore che restituisce big-endian
            pcm, swapped = pcm.byteswap(), True
    return pcm, {'blocks': len(blocks), 'reread': len(mismatched), 'suspect': suspect,
                 'unreadable_sectors': unread, 'byteswapped': swapped,
                 'speed_x': round(2 * (end - start) / 75 / max(time.time() - t0, 1e-6), 1)}


def cmd_toc(args):
    drv = Drive(args.drive)
    tracks, leadout = drv.toc()
    info = {'tracks': [{k: t[k] for k in ('num', 'seconds', 'data', 'preemphasis')} for t in tracks],
            'cdtext': {str(k): v for k, v in drv.cdtext().items()},
            'musicbrainz': musicbrainz(tracks, leadout)}
    js = json.dumps(info, ensure_ascii=False, indent=1)
    if args.save:
        Path(args.save).write_text(js, encoding='utf-8')
    print(js)


def cmd_rip(args):
    drv = Drive(args.drive)
    tracks, leadout = drv.toc()
    audio = [t for t in tracks if not t['data']]
    audio_end = max(t['end'] for t in audio)
    wanted = {int(x) for x in args.tracks.split(',')} if args.tracks else {t['num'] for t in audio}
    meta = json.loads(Path(args.meta).read_text(encoding='utf-8')) if args.meta else {}
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    report = []
    for t in audio:
        if t['num'] not in wanted:
            continue
        m = meta.get('tracks', {}).get(str(t['num']), {})
        title = m.get('title') or f'Traccia {t["num"]:02d}'
        artist = m.get('artist') or meta.get('artist', '')
        name = safe_name(f'{t["num"]:02d} {artist} - {title}' if artist else f'{t["num"]:02d} {title}')
        path = out / (name + '.flac')
        if path.exists():
            print(f'[{t["num"]:02d}] {name}: già estratta, salto', flush=True)
            continue
        print(f'[{t["num"]:02d}] {name}  ({t["seconds"]:.0f} s)', flush=True)
        pcm, st = rip_track(drv, t, audio_end)
        with sf.SoundFile(str(path), 'w', 44100, 2, 'PCM_16', format='FLAC') as f:
            for key, val in (('title', title), ('artist', artist), ('album', meta.get('album')),
                             ('date', meta.get('date')), ('tracknumber', str(t['num']))):
                if val:
                    try:
                        setattr(f, key, val)
                    except Exception:
                        pass
            f.write(pcm)
        st.update({'num': t['num'], 'file': path.name, 'preemphasis': t['preemphasis']})
        report.append(st)
        print(f'    ok: riletti {st["reread"]}/{st["blocks"]} blocchi, sospetti {st["suspect"]}, '
              f'settori illeggibili {st["unreadable_sectors"]}, {st["speed_x"]}x', flush=True)
    album = safe_name(f'{meta.get("artist", "")} - {meta.get("album", "")}') if meta.get('album') else 'cd'
    (out / f'rip_report {album}.json').write_text(json.dumps(report, ensure_ascii=False, indent=1), encoding='utf-8')
    print('FINE', flush=True)


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('toc')
    p.add_argument('drive')
    p.add_argument('--save')
    p.set_defaults(fn=cmd_toc)
    p = sub.add_parser('rip')
    p.add_argument('drive')
    p.add_argument('out')
    p.add_argument('--tracks')
    p.add_argument('--meta')
    p.set_defaults(fn=cmd_rip)
    args = ap.parse_args()
    sys.stdout.reconfigure(encoding='utf-8')
    args.fn(args)


if __name__ == '__main__':
    main()
