#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <arpa/inet.h>

#define PORT 9	

int main(int argc, char *argv[])
{
	int i, err, yes;
	char *ptr;
	char macaddr[6], magic[102];
	int sock;
	struct sockaddr_in addr;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s macaddr\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	/* Building a Magic Packet */
	ptr = argv[1];
	for (i = 0; i < 6; i++) {
		macaddr[i] = (char)strtoul(ptr, &ptr, 16);
		ptr++;
	}
	for (i = 0; i < 6; i++)
		magic[i] = 0xff;
	for (i = 6; i < 102; i++)
		magic[i] = macaddr[i % 6];

	/* Preparing for packet dispatch */
	if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		perror("socket");
		exit(EXIT_FAILURE);
	}
	yes = 1;
	err = setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
	if (err == -1) {
		perror("setsockopt");
		exit(EXIT_FAILURE);
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;			/* IPv4 */
	addr.sin_addr.s_addr = INADDR_BROADCAST;	/* Broadcast */
	addr.sin_port = htons(PORT);			/* discard service */

	/* dispatch packet */
	err = sendto(sock, magic, sizeof(magic), 0,
				(const struct sockaddr *)&addr, sizeof(addr));
	if (err == -1) {
		perror("sendto");
		exit(EXIT_FAILURE);
	}

	close(sock);

	return 0;
}
