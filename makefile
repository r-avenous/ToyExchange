# Specify that 'clean' and 'common' are phony targets
.PHONY: clean common

common: instruments.o

instruments.o: ./common/instruments.cpp
	g++ -c ./common/instruments.cpp -o ./build/instruments.o

exchange: common ./exchange/main.cpp
	g++ ./exchange/main.cpp ./build/instruments.o -o exchange.out
	./exchange.out

test: common ./tests/instruments_test.cpp
	g++ ./tests/instruments_test.cpp ./build/instruments.o -o a.out
	./a.out

clean:
	rm *.out
