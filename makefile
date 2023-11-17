CC = g++

CXXFLAGS = -Wall -m64 -g -std=c++11

TARGET = mdp_client

OBJS = main.o \
       mc_client.o

.phony : all clean

all: $(TARGET)
	echo "make done!"

$(TARGET) : $(OBJS)
	$(CC) $(CXXFLAGS) -o $@ $(OBJS) -L lib $(LIBS)

$%.o : %.cpp
	$(CC) $(CXXFLAGS) -c $< -o $@

clean :
	rm -rf *.o $(TARGET)
	echo "clean done!"

