INCLUDE = -I/usr/local/Cellar/opencv
LIBS = -lopencv_core -lopencv_imgproc -lopencv_highgui -lopencv_ml
LIBDIRS = -L/usr/local/lib
OBJECTS = EulerianMotionMag.o vibe.o main.o
SOURCE = EulerianMotionMag.cpp vibe.cpp main.cpp

BIN = bin
$(OBJECTS) : $(SOURCE)        
	g++ -c $(SOURCE)
$(BIN):$(objects)
	g++ -o $(BIN) $(OBJECTS) -I $(INCLUDE) $(LIBS)

clean: 
	rm $(OBJECTS) $(BIN)                                 
