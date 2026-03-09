CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -std=c11
LDFLAGS =

.PHONY: all clean v1 v2 v3 v3f run

all: v1 v3f

v1: sj_compress
v2: sj_compress_v2
v3: sj_compress_v3
v3f: sj_v3f

sj_compress: main.o sj_spatial_compress.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

sj_compress_v2: main_v2.o sj_spatial_v2.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

sj_compress_v3: main_v3.o sj_spatial_v3.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

sj_v3f: main_v3f.o sj_spatial_v3f.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o sj_compress sj_compress_v2 sj_compress_v3 sj_v3f

run: sj_v3f
	./sj_v3f
