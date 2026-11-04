# Barka na Flipperze

Aplikacja `.fap` na Flipper Zero: portret papieża, a po naciśnięciu OK
Flipper gra na głośniczku melodię „Barki” (pierwsza zwrotka z refrenem,
ok. 1 minuty). Projekt uczelniany.

| Gotowe do grania | Podczas grania |
|---|---|
| ![Ekran aplikacji - gotowe](docs/ekran_gotowe.png) | ![Ekran aplikacji - gra](docs/ekran_gra.png) |

*Zrzuty ekranu z Flippera z firmware Momentum.*

## Obsługa

| Przycisk | Działanie |
|---|---|
| OK | graj / stop |
| Wstecz | wyjście (zatrzymuje melodię) |

Na ekranie: stan („Gotowe” / „Gra...”), czas (długość melodii albo ile już
zagrano) i pasek postępu.

Dźwięk idzie przez serwis powiadomień Flippera, więc uwzględnia głośność
ustawioną w **Settings → Notifications** i tryb cichy (Stealth). Głośnik gra
jeden dźwięk naraz, dlatego melodia jest jednogłosowa.

## Instalacja

Skopiuj `barka.fap` na kartę microSD do `/ext/apps/Tools/` (np. przez
qFlipper) i uruchom z **Apps → Tools → Barka**.

Gotowy plik powstaje w `dist/` po zbudowaniu (niżej). Aplikacja buduje się na
oficjalne firmware (sprawdzone na 1.5.1, API 88.2) i na Momentum (mntm-012,
API 87.1) - plik `.fap` trzeba zbudować pod firmware, które masz na Flipperze.

## Budowanie

Potrzebny [uFBT](https://github.com/flipperdevices/flipperzero-ufbt):

```sh
ufbt                       # oficjalne firmware
UFBT_HOME=~/.ufbt-mntm ufbt   # Momentum (SDK pobrany wcześniej przez ufbt update)
```

## Melodia

Melodia jest w `melodia_barka.h`, wygenerowanym z pliku karaoke (`.kar`)
narzędziem `narzedzia/midi_na_nuty.py`:

```sh
python3 narzedzia/midi_na_nuty.py "Barka 1.kar" --info
python3 narzedzia/midi_na_nuty.py "Barka 1.kar" -o melodia_barka.h --do 63.4 --transpozycja 12 --legato
```

- `--info` opisuje kanały pliku; przy plikach karaoke pokazuje też, ile sylab
  tekstu trafia w nuty danego kanału - kanał melodii śpiewanej trafia we
  wszystkie.
- Bez `--kanal` narzędzie samo wybiera kanał melodii (po sylabach tekstu, a bez
  tekstu - kanał o najwyższych dźwiękach). Gdy w jednej chwili brzmi kilka
  dźwięków, zostaje najwyższy.
- `--od` / `--do` wycinają fragment (w sekundach), `--transpozycja 12` gra
  oktawę wyżej - na małym głośniku wyższe dźwięki brzmią wyraźniej.
- `--legato` dokleja do nut krótkie pauzy, które w plikach karaoke robią
  z melodii staccato.
- `--tryb dominujacy` jest dla plików MIDI zrobionych automatycznie z nagrania
  audio (dziesiątki nut na sekundę). Działa heurystycznie i daje wyraźnie
  gorszy wynik niż porządny plik MIDI.

Pliki `.mid` / `.kar` nie są trzymane w repo. Bez `melodia_barka.h` aplikacja
gra przykład z domeny publicznej - „Wlazł kotek na płotek”
(`melodia_przyklad.h`).

## Testy

```sh
make -C tests                                # logika aplikacji (C, ASan/UBSan)
python3 -m unittest discover -s tests -v     # narzędzie MIDI
```

Testy w C sprawdzają częstotliwości nut, czasy trwania, odtwarzacz (włączanie
i wyłączanie dźwięku, odstępy między nutami, koniec melodii, przepełnienie
licznika czasu) i portret, a także czy wbudowana melodia jest poprawna i mieści
się w zakresie głośnika. Testy narzędzia budują pliki MIDI w pamięci
i kompilują wygenerowany nagłówek.

## Narzędzia

- `narzedzia/midi_na_nuty.py` - MIDI / karaoke → `melodia_barka.h`
- `narzedzia/zrzut.py` - zrzut ekranu Flippera przez USB (RPC); opcje `--ok`
  i `--back` najpierw naciskają przycisk

## Licencja

[PolyForm Noncommercial 1.0.0](LICENSE) - projekt uczelniany, wykorzystanie
komercyjne jest zabronione.
