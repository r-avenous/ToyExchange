common:
	g++ -c /common/instruments.cpp -o instruments

exchange:
	common
	g++ /exchange/main.cpp instruments -o exchange.out

clean:
	rm *.out