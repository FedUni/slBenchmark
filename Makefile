ifeq ($(OS),Windows_NT)
	CV_INCLUDE = /c/opencv/install/include
	CV_LIB = /c/opencv/bin
	CV_VERSION = 300
else
	#CV_INCLUDE = /usr/local/Cellar/opencv/3.4.0_1/include
	#CV_LIB = /usr/local/Cellar/opencv/3.4.0_1/lib
	CV_LIB = /usr/local/lib
	CV_VERSION = 
endif

CV_MODULES = core imgcodecs imgproc videoio highgui
CV_LIBRARIES = $(patsubst %,-lopencv_%$(CV_VERSION),$(CV_MODULES))

CPPFLAGS = -g -I$(CV_INCLUDE) -DDEBUG_BUILD
LFLAGS = -L$(CV_LIB) $(CV_LIBRARIES) -I$(CV_INCLUDE) -DDEBUG_BUILD

LIBSRC := $(wildcard *Implementation.cpp) slBenchmark.cpp
LIBOBJS = $(patsubst %.cpp, %.o, $(LIBSRC))

all: slBenchmark

debug: CPPFLAGS += -g
debug: slBenchmark

slBenchmark: $(LIBOBJS) main.cpp
	g++ $(LFLAGS) $(LIBOBJS) main.cpp -o slBenchmark `pkg-config opencv --cflags --libs`

$(LIBOBJS):%.o: %.cpp %.h
	$(CXX) $(CPPFLAGS) -c $< -o $@

clean:
	-$(RM) $(LIBOBJS) slBenchmark

clean_experiments:
	-$(RM) -r [0-9]*/
