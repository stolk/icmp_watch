
icmp_watch: icmp_watch.c
	$(CC) -g -O2 -Wall icmp_watch.c -o icmp_watch

run: icmp_watch
	./icmp_watch www.ibm.com www.apple.com www.google.com www.stolk.org gamer1 xanderpc


