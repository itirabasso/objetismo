CXXFLAGS=-O3 -ffast-math -W -Wall -Wextra -pedantic -g 
LDFLAGS=-lpng
BINFILE=objetismo
DEPS=objetismo.cpp

$(BINFILE): $(DEPS)
	# g++ -W -Wall -Wextra -pedantic -o test objetismo.cpp -lpng
	
	$(CXX) $(CXXFLAGS) $(DEPS) -o $(BINFILE) $(LDFLAGS)
	# g++ -O3 -march=native -W -Wall -Wextra -pedantic -g objetismo.cpp -o objetismo -lpng
	# g++ -O3 -W -Wall -Wextra -pedantic -g objetismo.cpp -o objetismo -lpng
	# g++ -O3 -funsafe-math-optimizations -W -Wall -Wextra -pedantic -g objetismo.cpp -o objetismo -lpng
	# g++ -O3 -funsafe-math-optimizations -W -Wall -Wextra -pedantic -g objetismo.cpp -o objetismo -lpng
val:
	valgrind --leak-check=full --show-reachable=yes $(BINFILE)

prof:
	valgrind --tool=callgrind $(BINFILE)

clean:
	rm $(BINFILE).o $(BINFILE)
