main:
	#gcc main.c -o sketch $(pkg-config --cflags --libs raylib) -O2 -lraylib -lGL -lm
	gcc main.c -o sketch -O2 -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
