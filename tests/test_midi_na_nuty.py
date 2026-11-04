"""Testy narzedzia/midi_na_nuty.py. Uruchomienie z katalogu projektu:
python3 -m unittest discover -s tests -v
"""
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

KORZEN = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(KORZEN / "narzedzia"))
import midi_na_nuty as m  # noqa: E402


def varlen(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.append((n & 0x7F) | 0x80)
        n >>= 7
    return bytes(reversed(out))


def plik_midi(zdarzenia, tpq=480, fmt=0):
    """zdarzenia: lista (delta, bajty) jednej ścieżki."""
    track = b"".join(varlen(d) + b for d, b in zdarzenia) + b"\x00\xff\x2f\x00"
    return (b"MThd" + struct.pack(">IHHH", 6, fmt, 1, tpq)
            + b"MTrk" + struct.pack(">I", len(track)) + track)


def on(kanal, wys, glos=100):
    return bytes([0x90 | kanal, wys, glos])


def off(kanal, wys):
    return bytes([0x80 | kanal, wys, 0])


class WczytajTest(unittest.TestCase):
    def test_nuty_tempo_i_running_status(self):
        data = plik_midi([
            (0, b"\xff\x51\x03" + (600000).to_bytes(3, "big")),  # 100 BPM
            (0, b"\xf0\x03\x01\x02\xf7"),  # sysex - pominięty
            (0, b"\xc0\x05"),  # zmiana instrumentu - pominięta
            (0, on(0, 60, 90)),
            (240, bytes([62, 80])),  # running status: kolejne note on
            (240, bytes([60, 0])),  # note on z głośnością 0 = note off
            (240, off(0, 62)),
        ])
        tpq, tempa, nuty = m.wczytaj(data)
        self.assertEqual(tpq, 480)
        self.assertEqual(tempa, [(0, 600000)])
        self.assertEqual(nuty, [(0, 480, 60, 0, 0, 90), (240, 720, 62, 0, 0, 80)])

    def test_domyslne_tempo_i_nuta_bez_off(self):
        data = plik_midi([(0, on(1, 70)), (100, b"\xff\x01\x01x")])
        _, tempa, nuty = m.wczytaj(data)
        self.assertEqual(tempa, [(0, 500000)])
        self.assertEqual(nuty, [(0, 100, 70, 1, 0, 100)])

    def test_bledy(self):
        with self.assertRaises(m.BladMidi):
            m.wczytaj(b"RIFF....")
        smpte = bytearray(plik_midi([]))
        smpte[12:14] = struct.pack(">H", 0xE728)
        with self.assertRaises(m.BladMidi):
            m.wczytaj(bytes(smpte))
        with self.assertRaises(m.BladMidi):
            m.wczytaj(plik_midi([(0, bytes([60, 100]))]))  # dane bez statusu

    def test_sylaby_lyric_i_text_kar(self):
        # FF 05 (lyric) ma pierwszeństwo przed FF 01 (text)
        sylaby = []
        m.wczytaj(plik_midi([(0, b"\xff\x01\x02@T"), (10, b"\xff\x05\x02la"),
                             (5, b"\xff\x01\x02xx"), (5, b"\xff\x05\x02la")]), sylaby)
        self.assertEqual(sylaby, [10, 20])
        # .kar: sylaby jako text; nagłówki "@" i zdarzenia w tiku 0 pomijane
        sylaby = []
        m.wczytaj(plik_midi([(0, b"\xff\x01\x02@K"), (0, b"\xff\x01\x02ti"),
                             (30, b"\xff\x01\x03@Lx"), (10, b"\xff\x01\x02ta"),
                             (20, b"\xff\x01\x02to")]), sylaby)
        self.assertEqual(sylaby, [40, 60])
        # bez listy wczytaj działa jak dawniej
        self.assertEqual(len(m.wczytaj(plik_midi([(10, b"\xff\x05\x01a")]))), 3)

    def test_tik_na_sekundy_ze_zmiana_tempa(self):
        sek = m.tik_na_sekundy(480, [(0, 500000), (960, 1000000)])
        self.assertAlmostEqual(sek(480), 0.5)
        self.assertAlmostEqual(sek(960), 1.0)
        self.assertAlmostEqual(sek(1440), 2.0)


class MelodiaTest(unittest.TestCase):
    def test_wybor_kanalu_bez_perkusji(self):
        nuty = [(0, 10, 50, 0, 0, 100), (0, 10, 72, 1, 0, 100), (0, 10, 90, 9, 0, 100)]
        self.assertEqual(m.wybierz_kanal(nuty), 1)
        with self.assertRaises(m.BladMidi):
            m.wybierz_kanal([(0, 10, 40, 9, 0, 100)])

    def test_wybor_kanalu_po_sylabach(self):
        sylaby = [0, 100, 200]
        nuty = [
            # kanał 0: wysoki, ale nie trafia w sylaby
            (50, 90, 90, 0, 0, 100), (150, 190, 90, 0, 0, 100),
            # kanał 1: trafia we wszystkie sylaby, z nakładkami
            (0, 150, 60, 1, 0, 100), (100, 200, 62, 1, 0, 100), (200, 300, 64, 1, 0, 100),
            # kanał 2: trafia we wszystkie, bez nakładek - wygrywa remis
            (0, 100, 60, 2, 0, 100), (100, 200, 62, 2, 0, 100), (200, 300, 64, 2, 0, 100),
        ]
        self.assertEqual(m.zgodnosc_z_sylabami(nuty, 0, sylaby, 5), 0)
        self.assertEqual(m.zgodnosc_z_sylabami(nuty, 2, sylaby, 5), 3)
        self.assertEqual(m.zgodnosc_z_sylabami(nuty, 2, [3, 103, 250], 5), 2)
        self.assertEqual(m.wybierz_kanal(nuty, sylaby, 5), 2)
        self.assertEqual(m.wybierz_kanal(nuty), 0)  # bez tekstu: najwyższy

    def test_legato(self):
        linia = [(60, 0, 50), (None, 50, 100), (62, 100, 150), (None, 150, 400), (64, 400, 450)]
        self.assertEqual(m.legato(linia, 99),
                         [(60, 0, 100), (62, 100, 150), (None, 150, 400), (64, 400, 450)])
        self.assertEqual(m.legato([(None, 0, 10), (60, 10, 20)], 99), [(None, 0, 10), (60, 10, 20)])

    def test_najwyzszy_glos_i_pauzy(self):
        nuty = [
            (0, 100, 60, 0, 0, 100), (0, 100, 64, 0, 0, 100),  # akord - zostaje 64
            (100, 200, 67, 0, 0, 100),
            (300, 400, 67, 0, 0, 100),  # po pauzie 200-300
        ]
        self.assertEqual(m.melodia(nuty, 0),
                         [(64, 0, 100), (67, 100, 200), (None, 200, 300), (67, 300, 400)])

    def test_powtorzona_nuta_nie_jest_sklejana(self):
        nuty = [(0, 100, 60, 0, 0, 100), (100, 200, 60, 0, 0, 100)]
        self.assertEqual(m.melodia(nuty, 0), [(60, 0, 100), (60, 100, 200)])

    def test_dominujaca_pomija_alikwoty_i_cisze(self):
        nuty = [
            (0, 200, 72, 0, 0, 50), (0, 200, 84, 0, 0, 60),  # 84 = oktawa nad 72
            (200, 400, 76, 0, 0, 80),
            (400, 600, 30, 0, 0, 120),  # tylko bas poza zakresem -> pauza
            (600, 800, 79, 0, 0, 80),
        ]
        linia = m.dominujaca(nuty, 0, 100, zakres=(55, 88))
        self.assertEqual(linia, [(72, 0, 200), (76, 200, 400), (None, 400, 600), (79, 600, 800)])

    def test_dominujaca_wygladza_pojedyncza_komorke(self):
        nuty = [(0, 300, 72, 0, 0, 80), (100, 200, 74, 0, 0, 100)]
        self.assertEqual(m.dominujaca(nuty, 0, 100), [(72, 0, 300)])

    def test_przytnij_i_na_nuty(self):
        linia = [(None, 0, 10), (60, 10, 30), (None, 30, 40), (62, 40, 80), (None, 80, 90)]
        self.assertEqual(m.przytnij(linia), [(60, 10, 30), (None, 30, 40), (62, 40, 80)])
        self.assertEqual(m.przytnij(linia, 20, 60), [(60, 20, 30), (None, 30, 40), (62, 40, 60)])
        self.assertEqual(m.na_nuty([(60, 0, 10), (None, 10, 15)], transpozycja=12),
                         [(72, 10), (0, 5)])
        # długie dźwięki dzielone na kawałki uint16
        self.assertEqual(m.na_nuty([(60, 0, 70000)]), [(60, 65535), (60, 4465)])
        with self.assertRaises(m.BladMidi):
            m.na_nuty([(120, 0, 10)], transpozycja=12)


@unittest.skipUnless(shutil.which("cc"), "brak kompilatora C")
class NaglowekTest(unittest.TestCase):
    def test_naglowek_kompiluje_sie_z_melodia_c(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = pathlib.Path(tmp)
            (tmp / "melodia_barka.h").write_text(
                m.naglowek_c([(72, 480), (0, 240), (76, 480)], 480, 90, "barka", "Barka", "x.mid"))
            (tmp / "t.c").write_text(
                '#include "melodia_barka.h"\n'
                "int main(void) { return melodia_poprawna(&melodia_barka) &&\n"
                "    melodia_barka.ile == 3 && melodia_czas_ms(&melodia_barka) == 1667 ? 0 : 1; }\n")
            exe = tmp / "t"
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", f"-I{KORZEN}",
                            "-o", str(exe), str(tmp / "t.c"), str(KORZEN / "melodia.c")],
                           check=True)
            self.assertEqual(subprocess.run([str(exe)]).returncode, 0)


class MainTest(unittest.TestCase):
    def test_od_pliku_do_naglowka(self):
        data = plik_midi([(0, on(0, 72)), (480, off(0, 72)), (0, on(0, 74)), (480, off(0, 74))])
        with tempfile.TemporaryDirectory() as tmp:
            tmp = pathlib.Path(tmp)
            (tmp / "a.mid").write_bytes(data)
            wyj = tmp / "melodia_barka.h"
            self.assertEqual(m.main([str(tmp / "a.mid"), "-o", str(wyj), "--transpozycja", "-12"]), 0)
            tekst = wyj.read_text()
            self.assertIn("{60, 480}, {62, 480},", tekst)
            self.assertIn('.tytul = "Barka"', tekst)
            self.assertIn(".tempo_bpm = 120", tekst)
            self.assertIn("Wygenerowane przez narzedzia/midi_na_nuty.py z pliku a.mid", tekst)

    def test_karaoke_zakres_i_legato(self):
        data = plik_midi([
            (0, on(0, 0)),  # znacznik odliczania o wysokości 0 - pomijany
            (0, on(1, 90)),  # kanał 2: wysoki akompaniament, bez sylab
            (240, b"\xff\x05\x02la"), (0, on(0, 72)),
            (240, off(0, 72)), (240, b"\xff\x05\x02la"), (0, on(0, 74)),
            (480, off(0, 74)), (0, off(0, 0)), (0, off(1, 90)),
        ])
        with tempfile.TemporaryDirectory() as tmp:
            tmp = pathlib.Path(tmp)
            (tmp / "a.kar").write_bytes(data)
            wyj = tmp / "m.h"
            self.assertEqual(m.main([str(tmp / "a.kar"), "-o", str(wyj), "--legato"]), 0)
            # kanał 1 wybrany po sylabach; pauza 240 tików doklejona do 72
            self.assertIn("static const Nuta melodia_barka_nuty[] = {\n    {72, 480}, {74, 480},\n};",
                          wyj.read_text())


if __name__ == "__main__":
    unittest.main()
