/* Barka - portret papieża i melodia na głośniczku Flippera.
 * OK = graj / stop, Wstecz = wyjście. */
#include <furi.h>
#include <gui/elements.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <stdio.h>
#include <string.h>

#include "melodia.h"
#include "melodia_wybrana.h"
#include "odtwarzacz.h"
#include "portret.h"

#define BARKA_KROK_MS     (10u)
#define BARKA_ODSWIEZ_MS  (250u)
#define BARKA_TEKST_X     (56)
#define BARKA_PRAWA_KRAW  (127)

typedef struct {
    FuriMutex* mutex;
    FuriMessageQueue* wejscie;
    ViewPort* view_port;
    Gui* gui;
    NotificationApp* powiadomienia;
    Odtwarzacz odtwarzacz;
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

static void barka_format_czasu(char* buf, size_t size, uint32_t ms) {
    const uint32_t s = ms / 1000u;
    snprintf(buf, size, "%lu:%02lu", (unsigned long)(s / 60u), (unsigned long)(s % 60u));
}

static void barka_rysuj(Canvas* canvas, void* context) {
    BarkaApp* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    const Odtwarzacz* o = &app->odtwarzacz;
    const bool gra = o->stan == OdtwarzaczGra;

    canvas_clear(canvas);
    for(uint8_t y = 0; y < PORTRET_H; y++) {
        for(uint8_t x = 0; x < PORTRET_W; x++) {
            if(portret_piksel(x, y)) canvas_draw_dot(canvas, x, y);
        }
    }

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, BARKA_TEKST_X, 12, "Barka");

    canvas_set_font(canvas, FontSecondary);
    if(strcmp(o->melodia->tytul, "Barka") != 0) {
        canvas_draw_str(canvas, BARKA_TEKST_X, 23, o->melodia->tytul);
    }
    canvas_draw_str(canvas, BARKA_TEKST_X, 34, gra ? "Gra..." : "Gotowe");
    char czas[12];
    barka_format_czasu(
        czas,
        sizeof(czas),
        gra ? odtwarzacz_uplynelo_ms(o, app->czas_ms) : melodia_czas_ms(o->melodia));
    canvas_draw_str_aligned(canvas, BARKA_PRAWA_KRAW, 34, AlignRight, AlignBottom, czas);
    elements_progress_bar(
        canvas,
        BARKA_TEKST_X,
        37,
        (size_t)(BARKA_PRAWA_KRAW - BARKA_TEKST_X + 1),
        odtwarzacz_postep(o, app->czas_ms));
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
        odswiez = przed != app->odtwarzacz.stan ||
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
    free(app);
    return 0;
}
