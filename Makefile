CXX=g++
CXXOPTS=-std=c++11 -W -Wall -Wextra -Wno-missing-field-initializers \
	-Werror -O4 -ggdb -pg -fprofile-arcs -ftest-coverage

all: tests crawler

tests: tests.o
	$(CXX) $(CXXOPTS) -ladns -o $@ $<

crawler: main.o
	$(CXX) $(CXXOPTS) -ladns -o $@ $<

%.o: %.c++ *.h
	$(CXX) $(CXXOPTS) -c -o $@ $<

clean:
	rm -vf *.o *.gcno *.gcda *.gcov gmon.out crawler tests
