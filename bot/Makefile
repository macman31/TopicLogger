CC = g++
LD = g++
TARGET = topiclogger
SOURCE = main.cpp
MODULES = $(addsuffix .o,$(patsubst %.cpp,%,$(SOURCE)))
CCFLAGS = `mysql_config --cflags` -I/usr/local/Cellar/yaml-cpp/0.5.0/include
LDFLAGS = `mysql_config --libs` -lyaml-cpp -lircclient

$(TARGET): $(MODULES)
	$(LD) $(MODULES) $(LDFLAGS) -o $(TARGET)

$(MODULES): %.o: %.cpp
	$(CC) -c $? -o $@ $(CCFLAGS)