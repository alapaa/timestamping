sender: sender.cpp
	g++ -I ../src -std=c++11 sender.cpp ../src/gpl_code_remove.cpp ../src/util.cpp ../src/packet.cpp \
	../src/logging.cpp  -lpthread -pg -o sender
receiver: receiver.cpp
	g++ -I ../src -std=c++11 receiver.cpp ../src/util.cpp ../src/logging.cpp ../src/gpl_code_remove.cpp -lpthread -o receiver
