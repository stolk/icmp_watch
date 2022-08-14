
icmp_watch: icmp_watch.c
	$(CC) -O2 -Wall icmp_watch.c -o icmp_watch
	strip icmp_watch

icmp_watch.dev: icmp_watch.c
	$(CC) -g -O2 -Wall -Wextra -pedantic -fsanitize=address icmp_watch.c -o icmp_watch.dev

run: icmp_watch
	./icmp_watch www.ibm.com www.apple.com www.google.com www.stolk.org gamer1 xanderpc


