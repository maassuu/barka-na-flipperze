/* Barka - portret papieża i melodia na głośniczku Flippera.
 * OK = graj / stop, Wstecz = wyjście. Opcjonalnie tekst do melodii z karty:
 * /ext/apps_data/barka/tekst.txt (format opisany w tekst.h). */
#include <furi.h>
#include <gui/elements.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <stdio.h>
#include <storage/storage.h>
#include <string.h>

#include "melodia.h"
#include "melodia_wybrana.h"
#include "odtwarzacz.h"
#include "portret.h"
#include "tekst.h"

#define BARKA_KROK_MS      (10u)
#define BARKA_ODSWIEZ_MS   (250u)
#define BARKA_TEKST_X      (56)
#define BARKA_PRAWA_KRAW   (127)
#define BARKA_SZEROKOSC    (BARKA_PRAWA_KRAW - BARKA_TEKST_X + 1)
#define BARKA_WIERSZE      (3u)
#define BARKA_MAX_PLIK     (16u * 1024u)
/* Najpierw plik wgrany ręcznie, potem tekst dołączony do .fap przy budowaniu */
static const char* const barka_pliki_tekstu[] = {
    APP_DATA_PATH("tekst.txt"),
    APP_ASSETS_PATH("tekst.txt"),
};

typedef struct {
    FuriMutex* mutex;
    FuriMessageQueue* wejscie;
    ViewPort* view_port;
    Gui* gui;
    NotificationApp* powiadomienia;
    Odtwarzacz odtwarzacz;
    Tekst* tekst; /* NULL, gdy na karcie nie ma pliku tekstu */
    uint32_t czas_ms; /* migawka czasu dla rysowania */
} BarkaApp;

/* Dźwięk przez serwis powiadomień: uwzględnia głośność z ustawień Flippera
 * i tryb cichy (Stealth). Komunikat jest zmieniany tylko między wywołaniami
 * notification_message_block, które czeka na jego obsłużenie.
 * message_do_not_reset jest konieczny: bez niego serwis wyłącza dźwięk zaraz
 * po końcu sekwencji i z nuty zostaje tylko krótkie kliknięcie. */
static NotificationMessage barka_ton = {
    .type = NotificationMessageTypeSoundOn,
    .data.sound = {.frequency = 440.0f, .volume = 1.0f},
};
static const NotificationSequence barka_sekwencja_ton = {&barka_ton, &message_do_not_reset, NULL};
static const NotificationSequence barka_sekwencja_cisza = {&message_sound_off, NULL};

static uint32_t barka_teraz_ms(void) {
    return (uint32_t)((uint64_t)furi_get_tick() * 1000u / furi_kernel_get_tick_frequency());
}

static void barka_graj(BarkaApp* app, Dzwiek dzwiek) {
    if(dzwiek.akcja == DzwiekWlacz) {
        barka_ton.data.sound.frequency = dzwiek.czestotliwosc;
        notification_message_block(app->powiadomienia, &barka_sekwencja_ton);
    } else if(dzwiek.akcja == DzwiekWylacz) {
        notification_message_block(app->powiadomienia, &barka_sekwencja_cisza);
    }
}

/* Wczytuje tekst z pliku; brak pliku albo błąd = NULL */
static Tekst* barka_wczytaj_plik(Storage* storage, const char* sciezka) {
    Tekst* tekst = NULL;
    File* plik = storage_file_alloc(storage);
    if(storage_file_open(plik, sciezka, FSAM_READ, FSOM_OPEN_EXISTING)) {
        const uint64_t rozmiar = storage_file_size(plik);
        if(rozmiar > 0u && rozmiar <= BARKA_MAX_PLIK) {
            char* bufor = malloc((size_t)rozmiar);
            const size_t przeczytane = storage_file_read(plik, bufor, (size_t)rozmiar);
            if(przeczytane == rozmiar) {
                tekst = malloc(sizeof(Tekst));
                const TekstWynik wynik = tekst_parsuj(tekst, bufor, przeczytane);
                if(wynik != TekstOk) {
                    FURI_LOG_W("Barka", "%s: blad %d", sciezka, (int)wynik);
                    free(tekst);
                    tekst = NULL;
                }
            }
            free(bufor);
        }
    }
    storage_file_close(plik);
    storage_file_free(plik);
    return tekst;
}

/* Tekst z karty albo z .fap; bez tekstu aplikacja po prostu gra melodię */
static Tekst* barka_wczytaj_tekst(void) {
    Tekst* tekst = NULL;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    for(size_t i = 0; tekst == NULL && i < COUNT_OF(barka_pliki_tekstu); i++) {
        tekst = barka_wczytaj_plik(storage, barka_pliki_tekstu[i]);
    }
    furi_record_close(RECORD_STORAGE);
    return tekst;
}

static void barka_format_czasu(char* buf, size_t size, uint32_t ms) {
    const uint32_t s = ms / 1000u;
    snprintf(buf, size, "%lu:%02lu", (unsigned long)(s / 60u), (unsigned long)(s % 60u));
}

/* Szerokość fragmentu w bieżącym foncie (canvas_string_width chce '\0') */
static uint16_t barka_szerokosc(const char* s, size_t dlugosc, void* kontekst) {
    Canvas* canvas = kontekst;
    char tmp[64];
    if(dlugosc >= sizeof(tmp)) dlugosc = sizeof(tmp) - 1u;
    memcpy(tmp, s, dlugosc);
    tmp[dlugosc] = '\0';
    return canvas_string_width(canvas, tmp);
}

static void barka_rysuj_fragment(Canvas* canvas, int32_t x, int32_t y, const char* s, size_t dl) {
    char tmp[64];
    if(dl >= sizeof(tmp)) dl = sizeof(tmp) - 1u;
    memcpy(tmp, s, dl);
    tmp[dl] = '\0';
    canvas_draw_str(canvas, x, y, tmp);
}

/* Bieżąca linia tekstu (do 3 wierszy) z podświetloną sylabą */
static void barka_rysuj_tekst(Canvas* canvas, const BarkaApp* app) {
    const Odtwarzacz* o = &app->odtwarzacz;
    const Tekst* t = app->tekst;
    const Melodia* m = o->melodia;

    /* numer brzmiącej nuty; w pauzie - poprzednia nuta */
    uint16_t nuta = (uint16_t)melodia_dzwieki_przed(m, o->indeks);
    if(m->nuty[o->indeks].nuta == NUTA_PAUZA && nuta > 0u) nuta--;
    const TekstPozycja poz = tekst_pozycja(t, o->powtorzenie, nuta);
    if(poz.linia < 0) return;

    const TekstLinia* linia = &t->linie[poz.linia];
    const char* s = tekst_linia(t, (uint16_t)poz.linia);
    uint16_t poczatki[8];
    const size_t ile = tekst_zawin(
        s, linia->dlugosc, BARKA_SZEROKOSC, barka_szerokosc, canvas, poczatki, 8);

    /* wiersz z podświetloną sylabą ma być widoczny */
    size_t sylaba_od = 0, sylaba_do = 0, wiersz_sylaby = 0;
    if(poz.sylaba >= 0) {
        const TekstSylaba* syl = &t->sylaby[poz.sylaba];
        sylaba_od = syl->start - linia->start;
        sylaba_do = sylaba_od + syl->dlugosc;
        while(wiersz_sylaby + 1u < ile && poczatki[wiersz_sylaby + 1u] <= sylaba_od) {
            wiersz_sylaby++;
        }
    }
    const size_t pierwszy =
        wiersz_sylaby >= BARKA_WIERSZE ? wiersz_sylaby - (BARKA_WIERSZE - 1u) : 0u;

    for(size_t w = pierwszy; w < ile && w < pierwszy + BARKA_WIERSZE; w++) {
        const int32_t y = 22 + (int32_t)(w - pierwszy) * 10;
        const size_t od = poczatki[w];
        const size_t do_ = tekst_koniec_wiersza(s, linia->dlugosc, poczatki, ile, w);
        barka_rysuj_fragment(canvas, BARKA_TEKST_X, y, s + od, do_ - od);
        if(poz.sylaba >= 0 && sylaba_od >= od && sylaba_do <= do_ && sylaba_do > sylaba_od) {
            const int32_t x = BARKA_TEKST_X + barka_szerokosc(s + od, sylaba_od - od, canvas);
            const int32_t sz = barka_szerokosc(s + sylaba_od, sylaba_do - sylaba_od, canvas);
            canvas_draw_box(canvas, x - 1, y - 8, (size_t)(sz + 1), 10);
            canvas_set_color(canvas, ColorWhite);
            barka_rysuj_fragment(canvas, x, y, s + sylaba_od, sylaba_do - sylaba_od);
            canvas_set_color(canvas, ColorBlack);
        }
    }
}

static void barka_rysuj(Canvas* canvas, void* context) {
    BarkaApp* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    const Odtwarzacz* o = &app->odtwarzacz;
    const bool gra = o->stan == OdtwarzaczGra;
    const float postep = odtwarzacz_postep(o, app->czas_ms);

    canvas_clear(canvas);
    for(uint8_t y = 0; y < PORTRET_H; y++) {
        for(uint8_t x = 0; x < PORTRET_W; x++) {
            if(portret_piksel(x, y)) canvas_draw_dot(canvas, x, y);
        }
    }

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, BARKA_TEKST_X, 12, "Barka");
    canvas_set_font(canvas, FontSecondary);

    if(gra && app->tekst != NULL) {
        barka_rysuj_tekst(canvas, app);
        /* cienki pasek postępu: tor 1 px, wypełnienie 2 px; nad nim
         * wydłużenia liter (do y=43), pod nim 2 px odstępu od "OK: stop" */
        canvas_draw_line(canvas, BARKA_TEKST_X, 45, BARKA_PRAWA_KRAW, 45);
        canvas_draw_box(
            canvas,
            BARKA_TEKST_X,
            44,
            (size_t)(postep * (float)BARKA_SZEROKOSC),
            2);
    } else {
        if(app->tekst != NULL) {
            canvas_draw_str(canvas, BARKA_TEKST_X, 23, "z tekstem");
        } else if(strcmp(o->melodia->tytul, "Barka") != 0) {
            canvas_draw_str(canvas, BARKA_TEKST_X, 23, o->melodia->tytul);
        }
        canvas_draw_str(canvas, BARKA_TEKST_X, 34, gra ? "Gra..." : "Gotowe");
        char czas[12];
        barka_format_czasu(
            czas,
            sizeof(czas),
            gra ? odtwarzacz_uplynelo_ms(o, app->czas_ms) : odtwarzacz_czas_ms(o));
        canvas_draw_str_aligned(canvas, BARKA_PRAWA_KRAW, 34, AlignRight, AlignBottom, czas);
        elements_progress_bar(canvas, BARKA_TEKST_X, 37, (size_t)BARKA_SZEROKOSC, postep);
    }
    canvas_draw_str(canvas, BARKA_TEKST_X, 55, gra ? "OK: stop" : "OK: graj");
    canvas_draw_str(canvas, BARKA_TEKST_X, 63, "Wstecz: wyjdz");

    furi_mutex_release(app->mutex);
}

static void barka_wejscie(InputEvent* event, void* context) {
    BarkaApp* app = context;
    furi_message_queue_put(app->wejscie, event, 0);
}

int32_t barka_app(void* p) {
    UNUSED(p);
    BarkaApp* app = malloc(sizeof(BarkaApp));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->wejscie = furi_message_queue_alloc(8, sizeof(InputEvent));
    odtwarzacz_init(&app->odtwarzacz, MELODIA_WYBRANA);
    app->tekst = barka_wczytaj_tekst();
    if(app->tekst != NULL) {
        /* każda zwrotka tekstu to jedno przejście melodii */
        odtwarzacz_ustaw_powtorzenia(&app->odtwarzacz, app->tekst->ile_zwrotek);
    }
    app->czas_ms = barka_teraz_ms();

    app->powiadomienia = furi_record_open(RECORD_NOTIFICATION);
    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, barka_rysuj, app);
    view_port_input_callback_set(app->view_port, barka_wejscie, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    bool dziala = true;
    uint32_t ostatnie_odswiezenie = 0;
    while(dziala) {
        InputEvent event;
        const bool jest = furi_message_queue_get(
                              app->wejscie, &event, furi_ms_to_ticks(BARKA_KROK_MS)) ==
                          FuriStatusOk;
        const uint32_t teraz = barka_teraz_ms();
        Dzwiek dzwiek = {DzwiekBezZmian, 0.0f};
        bool odswiez = false;

        furi_mutex_acquire(app->mutex, FuriWaitForever);
        app->czas_ms = teraz;
        const OdtwarzaczStan przed = app->odtwarzacz.stan;
        const size_t indeks_przed = app->odtwarzacz.indeks;
        if(jest && event.type == InputTypeShort && event.key == InputKeyOk) {
            dzwiek = odtwarzacz_przelacz(&app->odtwarzacz, teraz);
        } else if(jest && event.key == InputKeyBack &&
                  (event.type == InputTypeShort || event.type == InputTypeLong)) {
            dzwiek = odtwarzacz_stop(&app->odtwarzacz);
            dziala = false;
        } else {
            dzwiek = odtwarzacz_krok(&app->odtwarzacz, teraz);
        }
        const bool gra = app->odtwarzacz.stan == OdtwarzaczGra;
        /* z tekstem odświeżamy przy każdej nowej nucie (sylaba) */
        odswiez = przed != app->odtwarzacz.stan ||
                  (gra && app->tekst != NULL && indeks_przed != app->odtwarzacz.indeks) ||
                  (gra && (uint32_t)(teraz - ostatnie_odswiezenie) >= BARKA_ODSWIEZ_MS);
        furi_mutex_release(app->mutex);

        barka_graj(app, dzwiek);
        if(odswiez) {
            ostatnie_odswiezenie = teraz;
            view_port_update(app->view_port);
        }
    }

    /* Na wszelki wypadek: głośnik zawsze cichy po wyjściu */
    notification_message_block(app->powiadomienia, &barka_sekwencja_cisza);

    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    furi_message_queue_free(app->wejscie);
    furi_mutex_free(app->mutex);
    free(app->tekst);
    free(app);
    return 0;
}
