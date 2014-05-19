all:
	# g++ -W -Wall -Wextra -pedantic -o test objetismo.cpp -lpng
	
	g++ -O3 -ffast-math -march=native -W -Wall -Wextra -pedantic -g objetismo.cpp -o objetismo -lpng
	# g++ -O3 -march=native -W -Wall -Wextra -pedantic -g objetismo.cpp -o objetismo -lpng
	# g++ -O3 -W -Wall -Wextra -pedantic -g objetismo.cpp -o objetismo -lpng
	# g++ -O3 -funsafe-math-optimizations -W -Wall -Wextra -pedantic -g objetismo.cpp -o objetismo -lpng
	# g++ -O3 -funsafe-math-optimizations -W -Wall -Wextra -pedantic -g objetismo.cpp -o objetismo -lpng
val:
	valgrind --leak-check=full --show-reachable=yes ./objetismo

prof:
	valgrind --tool=callgrind ./objetismo

clean:
	rm objetismo.o objetismo
