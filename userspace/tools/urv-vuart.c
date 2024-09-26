#include <stdio.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/select.h>

#include <libwr/shw_io.h>
#include "fpga_io.h"
#include <libwr/switch_hw.h>

#define VUART_BASE 0x10000
#define VUART_TDR 0x10
#define VUART_RDR 0x14
#define RDR_RDY 0x100
#define RDR_DAT_MSK 0xff

int vuart_tx(char *buf, int size)
{
	while(size--)
		_fpga_writel(VUART_BASE+VUART_TDR, *buf++);
	return 0;
}

int vuart_rx(char *buf, int size)
{
	int n_rx = 0;
	int rdr;

	while(size--) {
		rdr = _fpga_readl(VUART_BASE+VUART_RDR);
		if(rdr & RDR_RDY) {
			*buf++ = (rdr & RDR_DAT_MSK);
			++n_rx;
		} else
			return n_rx;
	}

	return n_rx;
}

static int transfer_byte(int from) {
	char c;
	int ret;
	do {
		ret = read(from, &c, 1);
	} while (ret < 0 && errno == EINTR);
	if(ret == 1) {
		vuart_tx(&c, 1);
	} else {
		fprintf(stderr, "nothing to read. Port disconnected?\n");
		return -2;
	}
	return 0;
}


void term_main(void)
{
	while(1) {
		fd_set fds;
		int ret;
		char rx;
		struct timeval tv = {0, 10000};

		FD_ZERO(&fds);
		FD_SET(STDIN_FILENO, &fds);

		ret = select(STDIN_FILENO+1, &fds, NULL, NULL, &tv);
		if(ret == -1) {
			perror("select");
		} else if (ret > 0) {
			if(FD_ISSET(STDIN_FILENO, &fds)) {
				transfer_byte(STDIN_FILENO);
			}
		}

		while((vuart_rx(&rx, 1)) == 1)
			fprintf(stderr,"%c", rx);
		usleep(1000); /* relief the CPU */
	}

}


int main(void)
{
	shw_fpga_mmap_init();
	term_main();

	return 0;
}



