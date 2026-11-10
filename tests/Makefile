# Testy hosta dla modułów niezależnych od sprzętu.
# Użycie: make -C tests                       (zbuduj i uruchom)
#         make -C tests EXTRA_CFLAGS=-O2      (dodatkowe flagi, wymagane zostają)
#         make -C tests clean

CC ?= cc
# Wymagane flagi ustawione przez '=', żeby zmienna CFLAGS ze środowiska
# nie wyłączyła po cichu -Werror ani sanitizerów.
CFLAGS = -std=c11 -O1 -g -Wall -Wextra -Werror -Wdouble-promotion -Wshadow \
         -Wconversion -Wno-sign-conversion -fsanitize=address,undefined $(EXTRA_CFLAGS)

SRC_DIR = ..
BUILD = build

APP_SRC = $(SRC_DIR)/melodia.c $(SRC_DIR)/odtwarzacz.c $(SRC_DIR)/portret.c $(SRC_DIR)/tekst.c
TEST_SRC = test_main.c test_melodia.c test_odtwarzacz.c test_portret.c test_tekst.c

.PHONY: all run clean

all: run

$(BUILD)/testy: $(APP_SRC) $(TEST_SRC) $(wildcard $(SRC_DIR)/*.h) test_framework.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(SRC_DIR) -o $@ $(APP_SRC) $(TEST_SRC)

run: $(BUILD)/testy
	./$(BUILD)/testy

clean:
	rm -rf $(BUILD)
