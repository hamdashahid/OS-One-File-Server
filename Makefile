CXX = g++
CXXFLAGS = -std=c++17 -pthread -Wall
LDFLAGS = -lsfml-graphics -lsfml-window -lsfml-system

SRC = src/main.cpp src/vehicle.cpp src/intersection.cpp src/controller.cpp src/parking.cpp src/ui_sfml.cpp
INCLUDE = include/

TARGET = traffic_sim

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE) $(SRC) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)
