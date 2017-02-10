sender: sender.cpp ../src/util.cpp ../src/packet.cpp send_queue.h
	g++ -O3 -DNDBEBUG -I ../src -std=c++11 sender.cpp ../src/gpl_code_remove.cpp ../src/util.cpp ../src/packet.cpp \
	../src/logging.cpp  -lpthread -o sender
receiver: receiver.cpp
	g++ -O3 -I ../src -std=c++11 receiver.cpp ../src/util.cpp ../src/logging.cpp ../src/gpl_code_remove.cpp -lpthread -o receiver
