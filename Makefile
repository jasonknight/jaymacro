VERSION=0.1
CXXFLAGS=-w -std=gnu++0x -Wall
CC=g++
all: jayplay jayrec 

jayplay: jayplay.cpp chartbl.h
	g++ $(CXXFLAGS) -O2  -I/usr/X11R6/include -Wall -pedantic -DVERSION=$(VERSION) jayplay.cpp -o jayplay -L/usr/X11R6/lib -lXtst -lX11 -lboost_regex-mt

jayrec: jayrec.cpp
	g++ -O2  -I/usr/X11R6/include -Wall -pedantic -DVERSION=$(VERSION) jayrec.cpp -o jayrec -L/usr/X11R6/lib -lXtst -lX11

clean:
	rm jayrec jayplay 

deb:
	umask 022 && epm -f deb -nsm jay

rpm:
	umask 022 && epm -f rpm -nsm jay
