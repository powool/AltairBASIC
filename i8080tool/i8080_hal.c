// Intel 8080 (KR580VM80A) microprocessor core model
//
// Copyright (C) 2012 Alexander Demin <alexander@demin.ws>
//
// Credits
//
// Viacheslav Slavinsky, Vector-06C FPGA Replica
// http://code.google.com/p/vector06cc/
//
// Dmitry Tselikov, Bashrikia-2M and Radio-86RK on Altera DE1
// http://bashkiria-2m.narod.ru/fpga.html
//
// Ian Bartholomew, 8080/8085 CPU Exerciser
// http://www.idb.me.uk/sunhillow/8080.html
//
// Frank Cringle, The original exerciser for the Z80.
//
// Thanks to zx.pk.ru and nedopc.org/forum communities.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

#include "i8080_hal.h"
#include <errno.h>
#include <sys/select.h>
#include <stdint.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>

static unsigned char memory[0x10000];

int i8080_hal_memory_read_word(int addr) {
    return 
        (i8080_hal_memory_read_byte(addr + 1) << 8) |
        i8080_hal_memory_read_byte(addr);
}

void i8080_hal_memory_write_word(int addr, int word) {
    i8080_hal_memory_write_byte(addr, word & 0xff);
    i8080_hal_memory_write_byte(addr + 1, (word >> 8) & 0xff);
}

int i8080_hal_memory_read_byte(int addr) {
    return memory[addr & 0xffff];
}

void i8080_hal_memory_write_byte(int addr, int byte) {
    memory[addr & 0xffff] = byte;
}

//#define USE_TEST_INPUT
#ifdef USE_TEST_INPUT
// 4.1 test input:
const uint8_t * const testInput4_1 = (uint8_t *)
"32000\r"
"C\r"
"Y\r"
"\0 20 IF LEFT$(A$,1)=\"Y\" THEN OPEN \"I\",1,\"INSTR\",0 ELSE 90\r\r"
"\0\0\0\0\0\0\0\0 LIST\r\r"
"\0\0\0\0\0\0\0\0 LIST\r\r"
"\0\0\r\r\377";

// 3.0 test input:
const uint8_t * const testInput3_0 = (uint8_t *)
"32000\r"
"96\r"
"Y\r"
"\0 20 IF LEFT$(A$,1)=\"Y\" THEN OPEN \"I\",1,\"INSTR\",0 ELSE 90\r\r"
"\0\0\0\0\0\0\0\0 LIST\r\r"
"\0\0\0\0\0\0\0\0 LIST\r\r"
"\0\0\r\r\377";
static const uint8_t *it = testInput4_1;
static int skipFirstByte = 1;

int i8080_hal_io_input(int port) {
//	printf("\nreading on port %02x\n", port);

	// asm: bit 0 == 1 -> no char
	if (port == 0) {
//		printf("\n3.0: polling for character on port %02x\n", port);
		if (*it != 0xff) {
			return 0x02;
		}
		// no more keyboard data, just exit
		fflush(stdout);
		exit(0);
	}

	// Extended 4.1:
	if (port == 0x10) {
		if (*it != 0xff) {
//			printf("\n4.1: polling for character on port %02x\n", port);
			return 0x03;
		}
		// no more keyboard data, just exit
		fflush(stdout);
		exit(0);
	}

	if (port == 1 || port == 0x11) {
		if (port == 0x01 && skipFirstByte) {
			// 3.0 reads the keyboard during init process
			// for some reason, so ignore it
			it = testInput3_0;
			skipFirstByte = 0;
			return 0x00;
		}
		if (*it != 0xff) {
//			printf("\nreturning character %02x for IN %02x\n", *it, port);
			return *it++;
		}
		// no more keyboard data, just exit
		fflush(stdout);
		exit(0);
	}
	if (port == 2) {
		return 0x02;
	}
	return 0;
}
#else

int i8080_hal_io_input(int port) {
//	printf("\nreading on port %02x\n", port);
	fcntl(0, F_SETFL, O_NONBLOCK);
#if 0
	struct termios setraw;
	tcgetattr(STDIN_FILENO, &setraw);
	setraw.c_lflag &= ~(/*ECHO| */ICANON);
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &setraw);
#endif

	// read keyboard status
	// asm uses port 0 for input status - 0x00 -> available
	// reference uses port 0x10 for input status - 0x01 -> available
	if (port == 0 || port == 0x10) {
		fd_set set;
		struct timeval tv;
		tv.tv_sec = 0;
		tv.tv_usec = 0;
		FD_ZERO(&set);
		FD_SET(STDIN_FILENO, &set);
		int res = select(2, &set, NULL, NULL, &tv);

		// bit 0x02 means always ready for output
		char ch;
		if (port == 0) {
			ch = 0x02 | 0x01;
			if (FD_ISSET(0, &set)) ch ^= 0x01;
		} else {
			ch = 0x02 | 0x00;
			if (FD_ISSET(0, &set)) ch ^= 0x01;
		}
#if 0
		// bit 0x02 is ready bit for output
		char ch = 3;
		if (port == 0) ch = 2;
		if (!FD_ISSET(0, &set)) ch ^= 1;
#endif
//		printf("port %02x returned status bits %02x\n", port, ch);
		return ch;
	}

	// asm reads port 1 for key press data
	// reference reads port 0x11 for key press data
	if (port == 1 || port == 0x11) {
		char ch = 0;
		if (feof(stdin) ) {
			exit(0);
		}

		int res = read(0, &ch, 1);
		if (res < 0 && errno != EAGAIN) {
			exit(0);
		}
		if (ch == 10) ch = 13;
//		if (ch) printf("returned keyboard byte %02x!\n", ch);
		return ch;
	}
	if (port == 2) {
		return 0x02;
	}
//	printf("returned 0x00!\n");
	return 0;
}

#endif

void i8080_hal_io_output(int port, int value) {
//	printf("out port: %02X data %02X ('%c')\n", port, value, value > 0x1F ? value: '.');
	char ch = value & 0x7f;
	// 0x01 is console out
	if (port == 0x01) {
		int res = fputc(value, stdout);
		fflush(stdout);
		return;
	}
	// 0x02 is a printer status/control port - value 0x01 is a newline
	if (port == 0x02 && (value & 0x01) ) {
		int res = fputc('\n', stdout);
		fflush(stdout);
		return;
	}
	// 0x03 is printer output
	if (port == 0x03) {
		int res = fputc(value, stdout);
		fflush(stdout);
		return;
	}
	// 0x10 is printer output for BASIC 4.1
	if (port == 0x10) {
		int res = fputc(value, stdout);
		fflush(stdout);
		return;
	}
	// 0x11 is teletype output for BASIC 4.1
	if (port == 0x11) {
		int res = fputc(value, stdout);
		fflush(stdout);
		return;
	}
}

void i8080_hal_iff(int on) {
    // Nothing.
}

unsigned char* i8080_hal_memory(void) {
    return &memory[0];
}
