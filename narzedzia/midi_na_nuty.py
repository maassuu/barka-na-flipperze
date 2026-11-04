"""Wyciąga jednogłosową melodię z pliku MIDI i zapisuje ją jako nagłówek C.

Użycie:
  python3 midi_na_nuty.py PLIK.mid --info
      pokazuje kanały: liczbę nut, zakres dźwięków, czas trwania
  python3 midi_na_nuty.py PLIK.mid -o melodia_barka.h [--kanal N] [--transpozycja P]
      [--od SEK] [--do SEK] [--legato] [--nazwa barka] [--tytul Barka]

Bez --kanal wybierany jest kanał, w którego nuty trafiają sylaby tekstu
(pliki karaoke .mid/.kar), a bez tekstu - kanał z najwyższą średnią
wysokością dźwięków (bez perkusji, kanał 10). Gdy w jednej chwili brzmi kilka dźwięków,
zostaje najwyższy. Tylko biblioteka standardowa Pythona.
"""
import argparse
import bisect
import pathlib
import struct
import sys

PERKUSJA = 9  # kanał 10 w numeracji od 1
NUTA_MAX_TIKI = 0xFFFF


class BladMidi(ValueError):
    pass


def _varlen(data, pos):
    val = 0
    for _ in range(4):
        if pos >= len(data):
            raise BladMidi("ucięta liczba o zmiennej długości")
        b = data[pos]
        pos += 1
        val = (val << 7) | (b & 0x7F)
        if not b & 0x80:
            return val, pos
    raise BladMidi("za długa liczba o zmiennej długości")


def wczytaj(data, sylaby=None, tresc=None):
    """Zwraca (tpq, tempa, nuty).

    tempa: lista (tik, mikrosekundy_na_cwiercnute), posortowana
    nuty:  lista (start, koniec, wysokosc, kanal, sciezka, glosnosc) w tikach
    sylaby: jeśli podana lista, dopisywane są do niej tiki sylab tekstu
            (zdarzenia lyric, a bez nich text - pliki .kar)
    tresc:  jeśli podana lista, dopisywane są do niej pary (tik, bajty sylaby)
    """
    if data[:4] != b"MThd":
        raise BladMidi("to nie jest plik MIDI (brak MThd)")
    hlen, fmt, ntracks, division = struct.unpack(">IHHH", data[4:14])
    if division & 0x8000:
        raise BladMidi("czas SMPTE nie jest obsługiwany")
    if fmt not in (0, 1):
        raise BladMidi(f"format {fmt} nie jest obsługiwany")
    pos = 8 + hlen
    tempa, nuty, liryki, teksty = [], [], [], []
    for track in range(ntracks):
        if data[pos:pos + 4] != b"MTrk":
            raise BladMidi(f"brak MTrk dla ścieżki {track}")
        tlen = struct.unpack(">I", data[pos + 4:pos + 8])[0]
        end = pos + 8 + tlen
        if end > len(data):
            raise BladMidi("ucięta ścieżka")
        p, tick, status = pos + 8, 0, None
        grajace = {}  # (kanal, wysokosc) -> lista (start, glosnosc)
        while p < end:
            delta, p = _varlen(data, p)
            tick += delta
            b = data[p]
            if b & 0x80:
                status = b
                p += 1
            elif status is None:
                raise BladMidi("running status bez wcześniejszego statusu")
            if status == 0xFF:
                typ = data[p]
                ln, p = _varlen(data, p + 1)
                if typ == 0x51 and ln == 3:
                    tempa.append((tick, int.from_bytes(data[p:p + 3], "big")))
                elif typ == 0x05:
                    liryki.append((tick, data[p:p + ln]))
                elif typ == 0x01 and tick > 0 and not data[p:p + 1] == b"@":
                    teksty.append((tick, data[p:p + ln]))  # .kar: sylaby jako "text"
                p += ln
                if typ == 0x2F:
                    break
                status = None
                continue
            if status in (0xF0, 0xF7):
                ln, p = _varlen(data, p)
                p += ln
                status = None
                continue
            kind, kanal = status & 0xF0, status & 0x0F
            if kind in (0xC0, 0xD0):
                p += 1
                continue
            a, v = data[p], data[p + 1]
            p += 2
            if kind == 0x90 and v > 0:
                grajace.setdefault((kanal, a), []).append((tick, v))
            elif kind == 0x80 or (kind == 0x90 and v == 0):
                starty = grajace.get((kanal, a))
                if starty:
                    start, glos = starty.pop(0)
                    if tick > start:
                        nuty.append((start, tick, a, kanal, track, glos))
        for (kanal, a), starty in grajace.items():
            for start, glos in starty:  # bez note off - do końca ścieżki
                if tick > start:
                    nuty.append((start, tick, a, kanal, track, glos))
        pos = end
    tempa.sort()
    if not tempa or tempa[0][0] > 0:
        tempa.insert(0, (0, 500000))  # domyślnie 120 BPM
    nuty.sort()
    wybrane = sorted(liryki or teksty, key=lambda x: x[0])
    if sylaby is not None:
        sylaby.extend(t for t, _ in wybrane)
    if tresc is not None:
        tresc.extend(wybrane)
    return division, tempa, nuty


def tik_na_sekundy(tpq, tempa):
    """Funkcja zamieniająca tik na sekundy z uwzględnieniem zmian tempa."""
    punkty, sek = [], 0.0
    for i, (tik, us) in enumerate(tempa):
        if i:
            prev_tik, prev_us = tempa[i - 1]
            sek += (tik - prev_tik) * prev_us / 1e6 / tpq
        punkty.append((tik, sek, us))

    def f(t):
        for tik, s, us in reversed(punkty):
            if t >= tik:
                return s + (t - tik) * us / 1e6 / tpq
        return 0.0

    return f


def zgodnosc_z_sylabami(nuty, kanal, sylaby, tolerancja):
    """Ile sylab tekstu wypada (+- tolerancja tików) na początek nuty kanału."""
    poczatki = sorted(n[0] for n in nuty if n[3] == kanal)
    trafione = 0
    for t in sylaby:
        i = bisect.bisect_left(poczatki, t - tolerancja)
        if i < len(poczatki) and poczatki[i] <= t + tolerancja:
            trafione += 1
    return trafione


def opis(tpq, tempa, nuty, sylaby=()):
    sek = tik_na_sekundy(tpq, tempa)
    kanaly = {}
    for n in nuty:
        kanaly.setdefault(n[3], []).append(n)
    linie = [f"tiki na ćwierćnutę: {tpq}, tempa: "
             + ", ".join(f"{60e6 / us:.1f} BPM od tiku {t}" for t, us in tempa)]
    for kanal, ns in sorted(kanaly.items()):
        wys = [n[2] for n in ns]
        # ile razy nuty nakładają się (wielogłosowość)
        nakladki = sum(1 for a, b in zip(ns, ns[1:]) if b[0] < a[1])
        linie.append(
            f"kanał {kanal + 1:2d}{' (perkusja)' if kanal == PERKUSJA else ''}: "
            f"{len(ns)} nut, wysokość {min(wys)}-{max(wys)} (średnio {sum(wys) / len(wys):.1f}), "
            f"{sek(ns[0][0]):.1f}-{sek(max(n[1] for n in ns)):.1f} s, nakładek {nakladki}"
            + (f", sylaby tekstu {zgodnosc_z_sylabami(nuty, kanal, sylaby, tpq // 16)}/{len(sylaby)}"
               if sylaby else ""))
    return "\n".join(linie)


def wybierz_kanal(nuty, sylaby=(), tolerancja=0):
    """Kanał melodii: ten, w którego nuty trafia najwięcej sylab tekstu
    (pliki karaoke), a bez tekstu - z najwyższą średnią wysokością.
    Perkusja (kanał 10) jest pomijana."""
    kandydaci = {}
    for n in nuty:
        if n[3] != PERKUSJA:
            kandydaci.setdefault(n[3], []).append(n[2])
    if not kandydaci:
        raise BladMidi("brak nut poza perkusją")

    def ocena(k):
        srednia = sum(kandydaci[k]) / len(kandydaci[k])
        if sylaby:
            # remis: mniej nakładających się nut = czystsza linia melodii
            ns = [n for n in nuty if n[3] == k]
            nakladki = sum(1 for x, y in zip(ns, ns[1:]) if y[0] < x[1])
            return (zgodnosc_z_sylabami(nuty, k, sylaby, tolerancja), -nakladki, srednia)
        return (0, 0, srednia)

    return max(kandydaci, key=ocena)


def melodia(nuty, kanal):
    """Jednogłosowa linia: lista (wysokosc albo None dla pauzy, start, koniec).

    W każdej chwili brzmi najwyższa z granych nut kanału (skyline).
    """
    ns = [n for n in nuty if n[3] == kanal]
    if not ns:
        raise BladMidi(f"kanał {kanal + 1} nie ma nut")
    granice = sorted({n[0] for n in ns} | {n[1] for n in ns})
    wynik = []
    for a, b in zip(granice, granice[1:]):
        brzmi = [n for n in ns if n[0] <= a and n[1] >= b]
        wys = max((n[2] for n in brzmi), default=None)
        # nowa nuta zaczyna się w a - nie sklejamy jej z poprzednią tej samej wysokości
        nowa = any(n[0] == a and n[2] == wys for n in brzmi)
        if wynik and wynik[-1][0] == wys and not nowa:
            wynik[-1] = (wys, wynik[-1][1], b)
        else:
            wynik.append((wys, a, b))
    return wynik


def dominujaca(nuty, kanal, krok, zakres=(55, 88), prog=0.1):
    """Linia melodii z gęstego zapisu (np. MIDI z nagrania audio).

    Czas dzielony jest na komórki po `krok` tików. W każdej wygrywa dźwięk
    z zakresu z największą sumą głośność x czas, z pominięciem alikwotów
    (dźwięków o oktawę lub duodecymę nad silnym niższym). Komórka, w której
    zwycięzca ma mniej niż `prog` całej energii, jest pauzą. Pojedyncze
    komórki odstające od sąsiadów są wygładzane.
    """
    ns = [n for n in nuty if n[3] == kanal]
    if not ns:
        raise BladMidi(f"kanał {kanal + 1} nie ma nut")
    lo, hi = zakres
    ile = (max(n[1] for n in ns) + krok - 1) // krok
    energia = [{} for _ in range(ile)]
    poczatki = [set() for _ in range(ile)]
    for a, b, wys, _, _, glos in ns:
        poczatki[a // krok].add(wys)
        for c in range(a // krok, (b - 1) // krok + 1):
            czesc = min(b, (c + 1) * krok) - max(a, c * krok)
            if czesc > 0:
                energia[c][wys] = energia[c].get(wys, 0) + glos * czesc
    komorki = []
    for c, e in enumerate(energia):
        razem = sum(e.values())
        kandydaci = {
            w: v for w, v in e.items()
            if lo <= w <= hi and e.get(w - 12, 0) <= v / 2 and e.get(w - 19, 0) <= v / 2
        }
        wys = max(kandydaci, key=lambda w: (kandydaci[w], -w), default=None)
        if wys is not None and kandydaci[wys] < prog * razem:
            wys = None
        komorki.append(wys)
    for c in range(1, ile - 1):
        if komorki[c - 1] == komorki[c + 1] != komorki[c]:
            komorki[c] = komorki[c - 1]
    linia = []
    for c, wys in enumerate(komorki):
        nowa = wys is not None and wys in poczatki[c]
        if linia and linia[-1][0] == wys and not nowa:
            linia[-1] = (wys, linia[-1][1], (c + 1) * krok)
        else:
            linia.append((wys, c * krok, (c + 1) * krok))
    return linia


def legato(linia, max_pauza):
    """Dokleja do poprzedniej nuty pauzy nie dłuższe niż max_pauza tików.

    Pliki karaoke często zapisują śpiew jako krótkie nuty z przerwami - na
    głośniku brzmi to jak staccato. Dłuższe pauzy (oddechy) zostają.
    """
    out = []
    for wys, a, b in linia:
        if wys is None and out and out[-1][0] is not None and b - a <= max_pauza:
            out[-1] = (out[-1][0], out[-1][1], b)
        else:
            out.append((wys, a, b))
    return out


def przytnij(linia, od_tiku=None, do_tiku=None):
    out = []
    for wys, a, b in linia:
        if od_tiku is not None:
            a = max(a, od_tiku)
        if do_tiku is not None:
            b = min(b, do_tiku)
        if b > a:
            out.append((wys, a, b))
    while out and out[0][0] is None:
        out.pop(0)
    while out and out[-1][0] is None:
        out.pop()
    return out


def na_nuty(linia, transpozycja=0):
    """(wysokosc, start, koniec) -> lista (nuta_midi albo 0 dla pauzy, tiki)."""
    out = []
    for wys, a, b in linia:
        nuta = 0 if wys is None else wys + transpozycja
        if wys is not None and not 1 <= nuta <= 127:
            raise BladMidi(f"nuta {nuta} poza zakresem po transpozycji")
        dl = b - a
        while dl > 0:  # bardzo długie dźwięki dzielone na kawałki uint16
            kawalek = min(dl, NUTA_MAX_TIKI)
            out.append((nuta, kawalek))
            dl -= kawalek
    return out


def naglowek_c(nuty, tpq, bpm, nazwa, tytul, zrodlo):
    linie = [
        f"/* Wygenerowane przez narzedzia/midi_na_nuty.py z pliku {zrodlo}. */",
        "#pragma once",
        "",
        '#include "melodia.h"',
        "",
        f"static const Nuta melodia_{nazwa}_nuty[] = {{",
    ]
    for i in range(0, len(nuty), 8):
        linie.append("    " + " ".join(f"{{{n}, {d}}}," for n, d in nuty[i:i + 8]))
    linie += [
        "};",
        "",
        f"static const Melodia melodia_{nazwa} = {{",
        f'    .tytul = "{tytul}",',
        f"    .tempo_bpm = {bpm},",
        f"    .tiki_na_cwiercnute = {tpq},",
        f"    .nuty = melodia_{nazwa}_nuty,",
        f"    .ile = sizeof(melodia_{nazwa}_nuty) / sizeof(melodia_{nazwa}_nuty[0]),",
        "};",
        "",
    ]
    return "\n".join(linie)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("plik", type=pathlib.Path)
    ap.add_argument("--info", action="store_true")
    ap.add_argument("-o", "--wyjscie", type=pathlib.Path)
    ap.add_argument("--kanal", type=int, help="kanał 1-16")
    ap.add_argument("--transpozycja", type=int, default=0, help="o ile półtonów")
    ap.add_argument("--od", type=float, help="początek fragmentu w sekundach")
    ap.add_argument("--do", type=float, help="koniec fragmentu w sekundach")
    ap.add_argument("--tryb", choices=("najwyzszy", "dominujacy"), default="najwyzszy",
                    help="najwyzszy: zwykły MIDI z melodią; dominujacy: MIDI z nagrania audio")
    ap.add_argument("--krok", type=int, default=8,
                    help="tryb dominujacy: komórka jako ułamek całej nuty (8 = ósemka)")
    ap.add_argument("--zakres", help="zakres dźwięków MIDI, np. 24-108 (domyślnie 24-108, "
                    "w trybie dominujacy 55-88); nuty spoza zakresu są pomijane")
    ap.add_argument("--prog", type=float, default=0.1, help="tryb dominujacy: próg ciszy")
    ap.add_argument("--legato", action="store_true",
                    help="doklej do nut pauzy krótsze niż ćwierćnuta")
    ap.add_argument("--nazwa", default="barka")
    ap.add_argument("--tytul", default="Barka")
    a = ap.parse_args(argv)

    sylaby = []
    tpq, tempa, nuty = wczytaj(a.plik.read_bytes(), sylaby)
    if a.info:
        print(opis(tpq, tempa, nuty, sylaby))
        return 0
    if not a.wyjscie:
        ap.error("podaj -o PLIK.h albo --info")
    if len({us for _, us in tempa}) > 1:
        print("uwaga: plik zmienia tempo, używam pierwszego", file=sys.stderr)
    zakres = a.zakres or ("55-88" if a.tryb == "dominujacy" else "24-108")
    lo, hi = (int(x) for x in zakres.split("-"))
    if a.tryb == "najwyzszy":
        # np. nuty o wysokości 0 bywają znacznikami odliczania, nie dźwiękami
        nuty = [n for n in nuty if lo <= n[2] <= hi]
    kanal = a.kanal - 1 if a.kanal else wybierz_kanal(nuty, sylaby, tpq // 16)
    if a.tryb == "dominujacy":
        linia = dominujaca(nuty, kanal, max(1, tpq * 4 // a.krok), (lo, hi), a.prog)
    else:
        linia = melodia(nuty, kanal)
    sek = tik_na_sekundy(tpq, tempa)

    def tik_z_sekund(s):
        lo, hi = 0, max(n[1] for n in nuty)
        while lo < hi:
            mid = (lo + hi) // 2
            if sek(mid) < s:
                lo = mid + 1
            else:
                hi = mid
        return lo

    linia = przytnij(linia,
                     tik_z_sekund(a.od) if a.od is not None else None,
                     tik_z_sekund(a.do) if a.do is not None else None)
    if a.legato:
        linia = legato(linia, tpq - 1)
    wynik = na_nuty(linia, a.transpozycja)
    bpm = round(60e6 / tempa[0][1])
    a.wyjscie.write_text(naglowek_c(wynik, tpq, bpm, a.nazwa, a.tytul, a.plik.name), encoding="utf-8")
    print(f"kanał {kanal + 1}: {sum(1 for n, _ in wynik if n)} nut, zapisano {a.wyjscie}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
