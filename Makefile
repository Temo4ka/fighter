CC = g++ -std=c++23
SFML_DIR += -IC:\SFML-3.0.0\include -LC:\SFML-3.0.0\lib
MAC_SFML_DIR += -I/opt/homebrew/Cellar/sfml/3.0.0/include -L/opt/homebrew/Cellar/sfml/3.0.0/lib
LDFLAGS += -lmingw32
SFMLFLAGS += -lsfml-graphics -lsfml-window -lsfml-system -lsfml-audio -lsfml-main -mwindows
MAC_SFMLFLAGS += -lsfml-graphics -lsfml-window -lsfml-system -lsfml-audio

all:
	$(CC) $(SFML_DIR) ./source/main.cpp -lm -o run.exe $(SFMLFLAGS)

mac:
	clang++ -std=c++23 $(MAC_SFML_DIR)  ./source/main.cpp -lm -o react $(MAC_SFMLFLAGS)

vova:
	$(CC) -ID:\SFML-3.0.0\include -LD:\SFML-3.0.0\lib ./source/main.cpp -lm -o run.exe $(SFMLFLAGS)