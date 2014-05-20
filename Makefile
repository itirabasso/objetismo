CXXFLAGS=-O3 -std=c++0x -ffast-math -W -Wall -Wextra -pedantic -g -fopenmp
LDFLAGS=-lpng
BINFILE=objetismo
DEPS=objetismo.cpp

$(BINFILE): $(DEPS)
	# g++ -W -Wall -Wextra -pedantic -o test objetismo.cpp -lpng
	
	$(CXX) $(CXXFLAGS) $(DEPS) -o $(BINFILE) $(LDFLAGS)
	# g++ -O3 -march=native -W -Wall -Wextra -pedantic -g objetismo.cpp -o objetismo -lpng
	# g++ -O3 -W -Wall -Wextra -pedantic -g objetismo.cpp -o objetismo -lpng
	# g++ -O3 -funsafe-math-optimizations -W -Wall -Wextra -pedantic -g objetismo.cpp -o objetismo -lpng
val:
	valgrind --leak-check=full --show-reachable=yes ./$(BINFILE) ab.png

debug:
	g++ -g -std=c++0x -W -Wall -Wextra -pedantic -o objetismo objetismo.cpp -lpng

prof:
	valgrind --tool=callgrind $(BINFILE)

clean:
	rm $(BINFILE)
