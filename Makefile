CC = g++ -std=c++23
SFML_DIR += -IC:\SFML-2.5.1\include -LC:\SFML-2.5.1\lib
MAC_SFML_DIR += -I/opt/homebrew/Cellar/sfml/2.6.1/include -L/opt/homebrew/Cellar/sfml/2.6.1/lib
LDFLAGS += -lmingw32
SFMLFLAGS += -lsfml-graphics -lsfml-window -lsfml-system -lsfml-audio -lsfml-main -mwindows
MAC_SFMLFLAGS += -lsfml-graphics -lsfml-window -lsfml-system -lsfml-audio

all:
	$(CC) $(SFML_DIR) ./source/main.cpp -lm -o run.exe $(SFMLFLAGS)

mac:
	clang++ -std=c++23 $(MAC_SFML_DIR)  ./source/main.cpp -lm -o react $(MAC_SFMLFLAGS)

vova:
	$(CC) -ID:\SFML-2.5.1\include -LD:\SFML-2.5.1\lib ./source/main.cpp -lm -o run.exe $(SFMLFLAGS)