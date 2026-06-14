main:
	gcc pde2c.c -o pde2c
	./pde2c sketch.pde > sketch.c
	gcc sketch.c -o sketch -O2 -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
