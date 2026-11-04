"""Testy narzedzia/kar_na_tekst.py (sylaby testowe, bez prawdziwych tekstów)."""
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest

KORZEN = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(KORZEN / "narzedzia"))
sys.path.insert(0, str(KORZEN / "tests"))
import kar_na_tekst as k  # noqa: E402
from test_midi_na_nuty import off, on, plik_midi  # noqa: E402


def syl(*teksty):
    return [(i, t.encode("utf-8")) for i, t in enumerate(teksty)]


class ZbudujTest(unittest.TestCase):
    def test_slowa_linie_i_zwrotki(self):
        t = syl("/La", "li ", "lo", "-lu", "/Ra ", "ri")
        self.assertEqual(k.zbuduj(t, [1, 2, 3, 5, 6, 7], 4), "_ La-li lo\n\n_ lu\nRa ri\n")

    def test_przedluzenia_w_slowie_i_po_slowie(self):
        t = syl("Ko", "tek ", "la")
        self.assertEqual(k.zbuduj(t, [0, 3, 4], 0), "Ko-_-_-tek la\n")
        t = syl("Ko ", "la")
        self.assertEqual(k.zbuduj(t, [0, 3], 0), "Ko-_-_ la\n")

    def test_dwie_sylaby_na_jednej_nucie(self):
        t = syl("Ra", "z", " dwa")
        self.assertEqual(k.zbuduj(t, [0, 0, 1], 0), "Raz dwa\n")

    def test_znaki_formatu_usuwane(self):
        t = syl("a-b", "_c ", " d")
        self.assertEqual(k.zbuduj(t, [0, 1, 2], 0), "ab-c d\n")

    def test_kodowanie(self):
        self.assertEqual(k.dekoduj("żółw".encode("utf-8"), "cp1250"), "żółw")
        self.assertEqual(k.dekoduj("żółw".encode("cp1250"), "cp1250"), "żółw")

    def test_przypisz(self):
        poczatki = [0, 100, 200, 300]
        self.assertEqual(k.przypisz([0, 98, 150, 205, 999], poczatki, 10), [0, 1, 2, 2, 3])


class FakePort:
    def __init__(self):
        self.zapisane = []
        self.odpowiedzi = [b"\r\n>: ", b"\r\n>: ", b"\r\n>: ", b"Ready\r\n", b"\r\n>: "]
        self.zamkniety = False

    def write(self, dane):
        self.zapisane.append(dane)

    def read(self, n):
        return self.odpowiedzi.pop(0) if self.odpowiedzi else b""

    def close(self):
        self.zamkniety = True


class WgrajTest(unittest.TestCase):
    def test_komendy_cli(self):
        port = FakePort()
        k.wgraj("x", b"abc", "/ext/apps_data/barka/tekst.txt", port)
        self.assertEqual(port.zapisane, [
            b"\r",
            b"storage mkdir /ext/apps_data/barka\r",
            b"storage remove /ext/apps_data/barka/tekst.txt\r",
            b"storage write_chunk /ext/apps_data/barka/tekst.txt 3\r",
            b"abc",
        ])
        self.assertTrue(port.zamkniety)


def plik_kar():
    """Melodia na kanale 1 (4 nuty x 2 zwrotki) z sylabami jako "text" (.kar)."""
    zdarzenia = [(0, b"\xff\x01\x06@KTest")]
    teksty = [b"/Ra", b"ta ", b"ta", b"\xbfa", b"/Ra", b"ta ", b"ta", b"ta"]  # cp1250: \xbf = ż
    for i, t in enumerate(teksty):
        zdarzenia.append((0 if i == 0 else 0, b"\xff\x01" + bytes([len(t)]) + t))
        zdarzenia.append((0, on(0, 72 + i % 4)))
        zdarzenia.append((240, off(0, 72 + i % 4)))
    zdarzenia[1] = (240, zdarzenia[1][1])  # pierwsza sylaba po 240 tikach
    zdarzenia.append((0, on(1, 40)))  # akompaniament bez sylab
    zdarzenia.append((480, off(1, 40)))
    return plik_midi(zdarzenia)


class MainTest(unittest.TestCase):
    def test_kar_do_pliku(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = pathlib.Path(tmp)
            (tmp / "t.kar").write_bytes(plik_kar())
            wyj = tmp / "tekst.txt"
            self.assertEqual(k.main([str(tmp / "t.kar"), "-o", str(wyj), "--nut-w-zwrotce", "4"]), 0)
            self.assertEqual(wyj.read_text(encoding="utf-8"),
                             "# Wygenerowane przez narzedzia/kar_na_tekst.py z pliku t.kar\n"
                             "Ra-ta ta-ża\n\nRa-ta ta-ta\n")

    def test_plik_bez_tekstu(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = pathlib.Path(tmp)
            (tmp / "a.mid").write_bytes(plik_midi([(0, on(0, 60)), (10, off(0, 60))]))
            self.assertEqual(k.main([str(tmp / "a.mid"), "-o", str(tmp / "x.txt")]), 1)

    @unittest.skipUnless(shutil.which("cc"), "brak kompilatora C")
    def test_wynik_czyta_parser_aplikacji(self):
        """Plik z narzędzia wczytany przez tekst.c: sylaby na właściwych nutach."""
        with tempfile.TemporaryDirectory() as tmp:
            tmp = pathlib.Path(tmp)
            (tmp / "t.kar").write_bytes(plik_kar())
            wyj = tmp / "tekst.txt"
            k.main([str(tmp / "t.kar"), "-o", str(wyj), "--nut-w-zwrotce", "4"])
            (tmp / "t.c").write_text(
                '#include "tekst.h"\n#include <stdio.h>\n#include <stdlib.h>\n'
                "int main(int argc, char** argv) {\n"
                '  FILE* f = fopen(argv[1], "rb"); char b[4096];\n'
                "  size_t n = fread(b, 1, sizeof(b), f); fclose(f);\n"
                "  Tekst* t = malloc(sizeof(Tekst));\n"
                "  if(tekst_parsuj(t, b, n) != TekstOk) return 2;\n"
                '  printf("%u %u|", t->ile_zwrotek, t->ile_sylab);\n'
                '  for(unsigned i = 0; i < t->ile_sylab; i++) printf("%u ", t->sylaby[i].nuta);\n'
                '  printf("|%s", tekst_linia(t, 0)); return 0; }\n')
            exe = tmp / "t"
            subprocess.run(["cc", "-std=c11", "-Wall", "-Werror", f"-I{KORZEN}", "-o", str(exe),
                            str(tmp / "t.c"), str(KORZEN / "tekst.c")], check=True)
            wynik = subprocess.run([str(exe), str(wyj)], capture_output=True, text=True, check=True)
            self.assertEqual(wynik.stdout, "2 8|0 1 2 3 0 1 2 3 |Rata taza")


if __name__ == "__main__":
    unittest.main()
