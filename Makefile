CC = g++ -std=c++23
SFML_DIR += -IC:\SFML-2.5.1\include -LC:\SFML-2.5.1\lib
LDFLAGS += -lmingw32
SFMLFLAGS += -lsfml-graphics -lsfml-window -lsfml-system -lsfml-audio -lsfml-main -mwindows

all:
	$(CC) $(SFML_DIR) ./source/main.cpp -lm -o run.exe $(SFMLFLAGS)