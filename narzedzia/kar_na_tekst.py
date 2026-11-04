"""Robi plik tekstu dla aplikacji (format z tekst.h) z pliku karaoke .kar/.mid.

Użycie:
  python3 kar_na_tekst.py PLIK.kar -o tekst.txt (--nut-w-zwrotce 72 | --melodia melodia_barka.h)
      [--kanal N] [--kodowanie cp1250] [--wgraj PORT]

Każda sylaba z pliku karaoke jest przypisywana do brzmiącej nuty kanału
melodii (tego samego, który wybiera midi_na_nuty.py), na którą wypada.
--nut-w-zwrotce: ile brzmiących nut ma melodia w aplikacji (jedna zwrotka);
co tyle nut zaczyna się nowa zwrotka tekstu. 0 = cały tekst jako jedna zwrotka.
--wgraj PORT: wgrywa wynik na Flippera do /ext/apps_data/barka/tekst.txt
(potrzebny pyserial, np. Python z toolchaina ufbt).

Treść tekstu nie jest wypisywana na ekran - tylko liczby.
"""
import argparse
import bisect
import os
import pathlib
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import midi_na_nuty as m  # noqa: E402

SCIEZKA_NA_FLIPPERZE = "/ext/apps_data/barka/tekst.txt"


def nut_w_melodii(sciezka):
    """Liczba brzmiących nut w nagłówku melodii (np. melodia_barka.h)."""
    tekst = pathlib.Path(sciezka).read_text(encoding="utf-8")
    nuty = re.findall(r"\{(\d+), (\d+)\}", tekst)
    if not nuty:
        raise ValueError(f"{sciezka}: brak nut")
    return sum(1 for n, _ in nuty if int(n) != 0)


def dekoduj(bajty, kodowanie):
    try:
        return bajty.decode("utf-8")
    except UnicodeDecodeError:
        return bajty.decode(kodowanie, errors="replace")


def poczatki_dzwiekow(nuty, kanal):
    """Tiki początków brzmiących nut melodii (jak w melodia_barka.h)."""
    return [a for wys, a, _ in m.melodia(nuty, kanal) if wys is not None]


def przypisz(sylaby, poczatki, tolerancja):
    """Dla każdej sylaby (tik) numer brzmiącej nuty, na którą wypada."""
    wynik = []
    for t in sylaby:
        i = bisect.bisect_left(poczatki, t - tolerancja)
        if i >= len(poczatki):
            i = len(poczatki) - 1
        wynik.append(i)
    return wynik


def zbuduj(tresc, numery, nut_w_zwrotce, kodowanie="cp1250"):
    """tresc: lista (tik, bajty), numery: nuta dla każdej sylaby.

    Zwraca tekst w formacie aplikacji: linie, sylaby przez '-', '_' dla nut
    bez nowej sylaby, pusta linia między zwrotkami. Znaczniki karaoke:
    '/' lub '\\' na początku sylaby = nowa linia, spacja = nowe słowo.
    """
    zwrotki = []  # zwrotka = lista linii, linia = lista słów ("ko-tek")
    poprzednia = None
    slowo_otwarte = False
    for (_, bajty), nr in zip(tresc, numery):
        s = dekoduj(bajty, kodowanie).replace("\r", "").replace("\n", "/")
        nowa_linia = s[:1] in ("/", "\\")
        s = s.lstrip("/\\").replace("-", "").replace("_", "").replace("~", "")
        # spacje wewnątrz sylaby (kilka słów na jednej nucie) -> "~"
        sylaba = "~".join(s.split()) or "_"
        nowe_slowo = not slowo_otwarte or s[:1].isspace()

        if poprzednia is not None and nr <= poprzednia:
            # kolejna sylaba na tej samej nucie - doklej do poprzedniej
            if s.strip() and zwrotki and zwrotki[-1] and zwrotki[-1][-1]:
                odstep = "~" if s[:1].isspace() or not slowo_otwarte else ""
                zwrotki[-1][-1][-1] += odstep + "~".join(s.split())
            slowo_otwarte = bool(s) and not s[-1].isspace()
            continue

        zwrotka = nr // nut_w_zwrotce if nut_w_zwrotce else 0
        w_zwrotce = nr % nut_w_zwrotce if nut_w_zwrotce else nr
        if zwrotka >= len(zwrotki):
            while len(zwrotki) <= zwrotka:
                zwrotki.append([])
            linia = ["_"] * w_zwrotce  # nuty zwrotki przed pierwszą sylabą
            zwrotki[zwrotka].append(linia)
            nowe_slowo = True
        else:
            linia = zwrotki[zwrotka][-1]
            luka = nr - poprzednia - 1  # nuty, na których trwa poprzednia sylaba
            if luka > 0:
                if linia:
                    linia[-1] += "-_" * luka
                else:
                    linia.extend(["_"] * luka)
            if nowa_linia:
                linia = []
                zwrotki[zwrotka].append(linia)
                nowe_slowo = True

        if nowe_slowo or not linia or linia[-1] == "_":
            linia.append(sylaba)
        else:
            linia[-1] += "-" + sylaba
        slowo_otwarte = bool(s) and not s[-1].isspace()
        poprzednia = nr

    wyjscie = []
    for z in zwrotki:
        linie = [" ".join(linia) for linia in z if linia]
        if linie:
            wyjscie.append("\n".join(linie))
    return "\n\n".join(wyjscie) + "\n"


def wgraj(port, dane, sciezka=SCIEZKA_NA_FLIPPERZE, polaczenie=None):
    """Zapisuje dane na karcie Flippera przez CLI (storage write_chunk)."""
    if polaczenie is None:
        import serial  # pyserial

        polaczenie = serial.Serial(port, 230400, timeout=0.2)

    def czekaj(znacznik, limit=5.0):
        bufor, koniec = b"", time.time() + limit
        while znacznik not in bufor:
            if time.time() > koniec:
                raise TimeoutError(f"Flipper nie odpowiedział ({znacznik!r})")
            bufor += polaczenie.read(256)
        return bufor

    try:
        polaczenie.write(b"\r")
        czekaj(b">: ")
        katalog = sciezka.rsplit("/", 1)[0]
        polaczenie.write(f"storage mkdir {katalog}\r".encode())
        czekaj(b">: ")
        polaczenie.write(f"storage remove {sciezka}\r".encode())
        czekaj(b">: ")
        polaczenie.write(f"storage write_chunk {sciezka} {len(dane)}\r".encode())
        czekaj(b"Ready")
        polaczenie.write(dane)
        czekaj(b">: ")
    finally:
        polaczenie.close()


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("plik", type=pathlib.Path)
    ap.add_argument("-o", "--wyjscie", type=pathlib.Path, required=True)
    ap.add_argument("--nut-w-zwrotce", type=int, default=0)
    ap.add_argument("--melodia", type=pathlib.Path,
                    help="nagłówek melodii (melodia_barka.h): --nut-w-zwrotce = liczba jego nut")
    ap.add_argument("--kanal", type=int, help="kanał melodii 1-16 (domyślnie jak midi_na_nuty.py)")
    ap.add_argument("--kodowanie", default="cp1250", help="kodowanie tekstu, gdy nie jest UTF-8")
    ap.add_argument("--wgraj", metavar="PORT", help="wgraj wynik na Flippera, np. /dev/cu.usbmodemflip_X")
    a = ap.parse_args(argv)
    if a.melodia and not a.nut_w_zwrotce:
        a.nut_w_zwrotce = nut_w_melodii(a.melodia)

    sylaby, tresc = [], []
    tpq, _, nuty = m.wczytaj(a.plik.read_bytes(), sylaby, tresc)
    if not tresc:
        print("plik nie zawiera tekstu (zdarzeń lyric ani text)", file=sys.stderr)
        return 1
    nuty = [n for n in nuty if 24 <= n[2] <= 108]
    kanal = a.kanal - 1 if a.kanal else m.wybierz_kanal(nuty, sylaby, tpq // 16)
    poczatki = poczatki_dzwiekow(nuty, kanal)
    numery = przypisz([t for t, _ in tresc], poczatki, tpq // 8)
    tekst = zbuduj(tresc, numery, a.nut_w_zwrotce, a.kodowanie)
    naglowek = f"# Wygenerowane przez narzedzia/kar_na_tekst.py z pliku {a.plik.name}\n"
    dane = (naglowek + tekst).encode("utf-8")
    a.wyjscie.write_bytes(dane)
    zwrotki = tekst.count("\n\n") + 1
    print(f"kanał {kanal + 1}: {len(tresc)} sylab, {len(poczatki)} nut, "
          f"{zwrotki} zwrotek, zapisano {a.wyjscie}")
    if a.wgraj:
        wgraj(a.wgraj, dane)
        print(f"wgrano na Flippera: {SCIEZKA_NA_FLIPPERZE}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
