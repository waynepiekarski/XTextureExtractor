// ---------------------------------------------------------------------
//
// XTextureExtractor
//
// Copyright (C) 2018 Wayne Piekarski
// wayne@tinmith.net http://tinmith.net/wayne
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// ---------------------------------------------------------------------

#undef UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include <list>

#pragma comment (lib, "Ws2_32.lib")

#include "XTextureExtractor.h"
#include <vector>
using namespace std;
#include "lodepng/lodepng.h"

int last_cockpit_texture_seq = -2; // Track when the aircraft changes, and restart the connection so we can resend the updated header

unsigned char sub_buffer[MAX_TEXTURE_WIDTH * MAX_TEXTURE_HEIGHT * 4]; // Ensure we definitely have enough space for 4-byte RGBA images of any supported size

std::list<SOCKET> connections;
char header[TCP_INTRO_HEADER];

void recompute_header() {
	log_printf("Recomputing TCP header for [%s] at %dx%d\n", cockpit_aircraft_name, cockpit_texture_width, cockpit_texture_height);
	memset(header, 0x00, TCP_INTRO_HEADER);
	char *hptr = header;
	hptr += sprintf(hptr, "%s %s %s\n", TCP_PLUGIN_VERSION, __DATE__, __TIME__);
	hptr += sprintf(hptr, "%s\n", cockpit_aircraft_name);
	hptr += sprintf(hptr, "%d %d\n", cockpit_texture_width, cockpit_texture_height);
	for (int i = 0; i < cockpit_window_limit; i++) {
		hptr += sprintf(hptr, "%s %d %d %d %d\n", _g_window_name[i], _g_texture_lbrt[i][0], _g_texture_lbrt[i][1], _g_texture_lbrt[i][2], _g_texture_lbrt[i][3]);
	}
	hptr += sprintf(hptr, "__EOF__\n");
}

DWORD WINAPI TCPListenerFunction(LPVOID lpParam)
{
	log_printf("Start of threaded TCP listener code\n");
	WSADATA wsaData;
	int iResult;

	SOCKET ListenSocket = INVALID_SOCKET;

	struct addrinfo *result = NULL;
	struct addrinfo hints;

	int iSendResult;

	// This thread was spawned by the main plugin, so recompute the header now, we know it is valid
	recompute_header();
	last_cockpit_texture_seq = cockpit_texture_seq;

	iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0) {
		log_printf("Fatal: WSAStartup failed with error: %d\n", iResult);
		return 1;
	}

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;
	
	log_printf("Opening up socket to listen on port %s\n", TCP_PLUGIN_PORT);
	iResult = getaddrinfo(NULL, TCP_PLUGIN_PORT, &hints, &result);
	if (iResult != 0) {
		log_printf("Fatal: getaddrinfo failed with error: %d\n", iResult);
		WSACleanup();
		return 1;
	}

	ListenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if (ListenSocket == INVALID_SOCKET) {
		log_printf("Fatal: socket failed with error: %ld\n", WSAGetLastError());
		freeaddrinfo(result);
		WSACleanup();
		return 1;
	}

	iResult = bind(ListenSocket, result->ai_addr, (int)result->ai_addrlen);
	if (iResult == SOCKET_ERROR) {
		log_printf("Fatal: bind failed with error: %d\n", WSAGetLastError());
		freeaddrinfo(result);
		closesocket(ListenSocket);
		WSACleanup();
		return 1;
	}

	freeaddrinfo(result);

	iResult = listen(ListenSocket, SOMAXCONN);
	if (iResult == SOCKET_ERROR) {
		log_printf("Fatal: listen failed with error: %d\n", WSAGetLastError());
		closesocket(ListenSocket);
		WSACleanup();
		return 1;
	}

	u_long non_block = 1;
	iResult = ioctlsocket(ListenSocket, FIONBIO, &non_block);
	if (iResult == SOCKET_ERROR) {
		log_printf("Fatal: failed to set non-blocking mode on listen socket: %d\n", WSAGetLastError());
		closesocket(ListenSocket);
		WSACleanup();
		return 1;
	}

	log_printf("Waiting for incoming TCP connections on port %s\n", TCP_PLUGIN_PORT);

	while (1) {
		// Check if there are any new connections, it is ok to not have any new ones and just maintain what we have
		SOCKET newClientSocket = INVALID_SOCKET;
		newClientSocket = accept(ListenSocket, NULL, NULL);
		if (newClientSocket == INVALID_SOCKET) {
			// See if the error was fatal or no new connection
			if (WSAGetLastError() != WSAEWOULDBLOCK) {
				log_printf("Fatal: accept failed with error: %d\n", WSAGetLastError());
				closesocket(ListenSocket);
				WSACleanup();
				return 1;
			}

			// If we don't have any connections, then lets sleep to rate limit this thread and loop around
			if (connections.size() == 0) {
				// log_printf("No incoming connection found, sleeping for 1 second ...\n");
				Sleep(1000);
				continue;
			}
		}
		else {
			int iOptVal = TCP_SEND_BUFFER; // Make sure this is very large to prevent WSAEWOULDBLOCK=10035 when the network gets clogged up
			int iOptLen = sizeof(int);
			iResult = setsockopt(newClientSocket, SOL_SOCKET, SO_SNDBUF, (char *)&iOptVal, iOptLen);
			if (iResult == SOCKET_ERROR) {
				log_printf("Fatal: failed to set SO_SNDBUF to %d: %d\n", TCP_SEND_BUFFER, WSAGetLastError());
				closesocket(newClientSocket);
				for (auto s : connections)
					closesocket(s);
				WSACleanup();
				return 1;
			}
			iResult = getsockopt(newClientSocket, SOL_SOCKET, SO_SNDBUF, (char *)&iOptVal, &iOptLen);
			if (iResult == SOCKET_ERROR) {
				log_printf("Fatal: failed to query SO_SNDBUF: %d\n", WSAGetLastError());
				closesocket(newClientSocket);
				for (auto s : connections)
					closesocket(s);
				WSACleanup();
				return 1;
			}

			u_long non_block = 0;
			iResult = ioctlsocket(newClientSocket, FIONBIO, &non_block);
			if (iResult == SOCKET_ERROR) {
				log_printf("Fatal: failed to set blocking mode on socket: %d\n", WSAGetLastError());
				closesocket(newClientSocket);
				for (auto s : connections)
					closesocket(s);
				WSACleanup();
				return 1;
			}

			log_printf("Accepted new connection, texture seq is %d, total connections is %zu, SO_SNDBUF is %d from %d\n", cockpit_texture_seq, connections.size(), iOptVal, TCP_SEND_BUFFER);

			iSendResult = send(newClientSocket, header, TCP_INTRO_HEADER, 0);
			if (iSendResult == SOCKET_ERROR) {
				log_printf("TCP header send failed with code %d for %d bytes, closing this socket down\n", WSAGetLastError(), TCP_INTRO_HEADER);
				closesocket(newClientSocket);
			}
			else if (iSendResult != TCP_INTRO_HEADER) {
				log_printf("Fatal: TCP transmission was %d bytes but expected to send %d bytes\n", iSendResult, TCP_INTRO_HEADER);
				for (auto s : connections)
					closesocket(s);
				WSACleanup();
				return 1;
			}
			else {
				// log_printf("Successfully sent header of %d bytes\n", TCP_INTRO_HEADER);
				connections.push_back(newClientSocket);
			}
		}

		std::vector<unsigned char> out_data;
		std::vector<unsigned char> png_data;

		if (last_cockpit_texture_seq != cockpit_texture_seq) {
			log_printf("Network: Texture sequence number has increased from %d to %d, so closing all connections to force restart\n", last_cockpit_texture_seq, cockpit_texture_seq);
			for (auto s : connections)
				closesocket(s);
			connections.clear();
			recompute_header();
		}
		last_cockpit_texture_seq = cockpit_texture_seq;

		if (cockpit_texture_id <= 0) {
			log_printf("No texture is currently found, nothing to transmit ... sleeping for 1 second\n");
			Sleep(1000);
			continue;
		}
		else if (texture_pointer == NULL) {
			// log_printf("Texture id is valid but no texture is ready, will wait 10 msec\n");
			Sleep(10); // Cannot ever exceed 100 fps
			continue;
		}

		// Debug code that draws a grid to the buffer to check if it is working
		/*
		for (int y = 0; y < cockpit_texture_height; y += 16)
			for (int x = 0; x < cockpit_texture_width; x += 16) {
				if (y % 16 == 0 || x % 16 == 0) {
					int ofs = (y * cockpit_texture_width + x) * 4;
					texture_pointer[ofs] = 0xFF; // Set red pixel, draw a grid
				}
			}
		*/

		// Reset the output buffer
		out_data.clear();

		// Encode each window in the texture as a separate image
		for (int i = 0; i < cockpit_window_limit; i++) {

			// Compute sub-image dimensions
			int x1 = _g_texture_lbrt[i][0]; // L
			int y1 = cockpit_texture_height - _g_texture_lbrt[i][1]; // B
			int x2 = _g_texture_lbrt[i][2]; // R
			int y2 = cockpit_texture_height - _g_texture_lbrt[i][3]; // T
			int in_stride = cockpit_texture_width * 4;
			int out_stride = (x2 - x1) * 4;
			int out_rows = -(y2 - y1);
			if (out_stride < 0)
				log_printf("Error! Negative out_stride value %d\n", out_stride);
			if (out_rows < 0)
				log_printf("Error! Negative out_rows value %d\n", out_rows);

			// Copy the sub-image into a temporary buffer, flip the image since it is inverted
			unsigned char *src = texture_pointer + (y1 * in_stride) + (x1 * 4);
			unsigned char *dest = &sub_buffer[0];
			for (int r = 0; r < out_rows; r++) {
				memcpy(dest, src, out_stride);
				src -= in_stride;
				dest += out_stride;
			}

			// Compress the RGBA image as an RGB image with lodepng
			png_data.clear();
			lodepng::State state;
			state.info_raw.colortype = LCT_RGBA; // Input type
			state.info_raw.bitdepth = 8;
			state.info_png.color.colortype = LCT_RGB; // Output type
			state.info_png.color.bitdepth = 8;
			state.encoder.auto_convert = 0; // Must provide this or will ignore the input/output types
			unsigned error = lodepng::encode(png_data, &sub_buffer[0], x2 - x1, -(y2 - y1), state);

			// 7 char header and 1 byte for the window id (8 bytes total)
			out_data.insert(out_data.end(), '!');
			for (int ch = 0; ch < 5; ch++)
				out_data.insert(out_data.end(), '_');
			out_data.insert(out_data.end(), (unsigned char)i);
			out_data.insert(out_data.end(), '_');

			// 4 bytes for the number of bytes of PNG data so we can easily skip it if necessary
			unsigned int num_png_bytes = (unsigned int)png_data.size();
			for (int ch = 0; ch < 4; ch++) {
				out_data.insert(out_data.end(), *(((unsigned char *)&num_png_bytes)+ch));
			}

			// Pad the header out with another 4 bytes of nothing
			for (int ch = 0; ch < 4; ch++)
				out_data.insert(out_data.end(), '_');

			// We have written out 16 bytes of header, now write out the PNG data
			out_data.insert(out_data.end(), png_data.begin(), png_data.end());

			// Add null bytes to the end to pad the PNG data out to 1024 byte blocks
			int pad = 1024 - png_data.size() % 1024;
			for (int ch = 0; ch < pad; ch++)
				out_data.insert(out_data.end(), 0x00);
		}

		// Now that the image is compressed, we can throw away the texture capture and tell the main thread to start capturing a new one immediately
		texture_pointer = NULL;

		// Send the compressed data to the socket
		for (auto s = connections.begin(); s != connections.end(); ) {
			// log_printf("Sending PNG data to socket %zu\n", *s);
			iSendResult = send(*s, (const char *)out_data.data(), (int)out_data.size(), 0);
			if (iSendResult == SOCKET_ERROR) {
				log_printf("Connection closed: TCP PNG send of %zu bytes failed with code %d\n", out_data.size(), WSAGetLastError());
				closesocket(*s);
				s = connections.erase(s); // Increment iterator
			}
			else if (iSendResult != out_data.size()) {
				log_printf("Fatal: TCP transmission was %d bytes but expected to send %zu bytes\n", iSendResult, out_data.size());
				for (auto s : connections)
					closesocket(s);
				WSACleanup();
				return 1;
			}
			else {
				// log_printf("Successfully sent PNG image of %dx%d with %d compressed bytes\n", cockpit_texture_width, cockpit_texture_height, iSendResult);
				++s; // Increment iterator
			}
		}
	}

	// Unreachable code - shut down everything
	closesocket(ListenSocket);
	WSACleanup();

	return 0;
}

void start_networking_thread(void) {
	CreateThread(
		NULL,                   // default security attributes
		0,                      // use default stack size  
		TCPListenerFunction,       // thread function name
		NULL,          // argument to thread function 
		0,                      // use default creation flags 
		NULL);   // returns the thread identifier 
}
