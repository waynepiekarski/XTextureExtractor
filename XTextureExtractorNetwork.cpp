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

#pragma comment (lib, "Ws2_32.lib")

#include "XTextureExtractor.h"
#include <vector>
using namespace std;
#include "lodepng/lodepng.h"

int last_cockpit_texture_seq = -1; // Track when the aircraft changes, and restart the connection so we can resend the updated header

DWORD WINAPI TCPListenerFunction(LPVOID lpParam)
{
	log_printf("Start of threaded TCP listener code\n");
	WSADATA wsaData;
	int iResult;

	SOCKET ListenSocket = INVALID_SOCKET;
	SOCKET ClientSocket = INVALID_SOCKET;

	struct addrinfo *result = NULL;
	struct addrinfo hints;

	int iSendResult;

	iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0) {
		log_printf("WSAStartup failed with error: %d\n", iResult);
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
		log_printf("getaddrinfo failed with error: %d\n", iResult);
		WSACleanup();
		return 1;
	}

	ListenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if (ListenSocket == INVALID_SOCKET) {
		printf("socket failed with error: %ld\n", WSAGetLastError());
		freeaddrinfo(result);
		WSACleanup();
		return 1;
	}

	iResult = bind(ListenSocket, result->ai_addr, (int)result->ai_addrlen);
	if (iResult == SOCKET_ERROR) {
		log_printf("bind failed with error: %d\n", WSAGetLastError());
		freeaddrinfo(result);
		closesocket(ListenSocket);
		WSACleanup();
		return 1;
	}

	freeaddrinfo(result);

	iResult = listen(ListenSocket, SOMAXCONN);
	if (iResult == SOCKET_ERROR) {
		log_printf("listen failed with error: %d\n", WSAGetLastError());
		closesocket(ListenSocket);
		WSACleanup();
		return 1;
	}
	log_printf("Waiting for incoming TCP connections on port %s\n", TCP_PLUGIN_PORT);

	while (1) {
		// Accept client connections, and handle just this one until it fails, then wait for the next one
		ClientSocket = accept(ListenSocket, NULL, NULL);
		if (ClientSocket == INVALID_SOCKET) {
			printf("accept failed with error: %d\n", WSAGetLastError());
			closesocket(ListenSocket);
			WSACleanup();
			return 1;
		}
		log_printf("Accepted new connection, will start transmitting images, texture seq is %d\n", cockpit_texture_seq);

		last_cockpit_texture_seq = cockpit_texture_seq;
		std::vector<unsigned char> png_data;

		char header[TCP_INTRO_HEADER];
		memset(header, 0x00, TCP_INTRO_HEADER);
		char *hptr = header;
		hptr += sprintf(hptr, "%s %s %s\n", TCP_PLUGIN_VERSION, __DATE__, __TIME__);
		hptr += sprintf(hptr, "%s\n", cockpit_aircraft_name);
		hptr += sprintf(hptr, "%d %d\n", cockpit_texture_width, cockpit_texture_height);
		for (int i = 0; i < cockpit_window_limit; i++) {
			hptr += sprintf(hptr, "%s %d %d %d %d\n", _g_window_name[i], _g_texture_lbrt[i][0], _g_texture_lbrt[i][1], _g_texture_lbrt[i][2], _g_texture_lbrt[i][3]);
		}
		hptr += sprintf(hptr, "__EOF__\n");

		// Keep transmitting until the connection fails
		do {
			if (last_cockpit_texture_seq != cockpit_texture_seq) {
				log_printf("Texture sequence number has increased from %d to %d, so restarting connection\n", last_cockpit_texture_seq, cockpit_texture_seq);
				closesocket(ClientSocket);
				break; // Exit the loop
			}
			if (cockpit_texture_id <= 0) {
				log_printf("No texture is currently found, cannot begin transmission ... sleeping for 1 second\n");
				Sleep(1000);
				continue;
			} else if (texture_pointer == NULL) {
				log_printf("Texture id is valid but not texture is ready, will wait 100 msec\n");
				Sleep(100);
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

			// Send the header data to the socket if we haven't sent it yet
			if (hptr) {
				const char *sendData = header;
				hptr = NULL;
				iSendResult = send(ClientSocket, sendData, TCP_INTRO_HEADER, 0);
				if (iSendResult == SOCKET_ERROR) {
					log_printf("Error: TCP send failed with code %d\n", WSAGetLastError());
					closesocket(ClientSocket);
					break; // Exit the loop
				}
				else if (iSendResult != TCP_INTRO_HEADER) {
					log_printf("Error: TCP transmission was %d bytes but expected to send %zu bytes\n", iSendResult, png_data.size());
				}
				else {
					log_printf("Successfully sent header of %d bytes\n", TCP_INTRO_HEADER);
				}
			}

			// Write out the RGBA image as an RGB image with lodepng
			lodepng::State state;
			state.info_raw.colortype = LCT_RGBA; // Input type
			state.info_raw.bitdepth = 8;
			state.info_png.color.colortype = LCT_RGB; // Output type
			state.info_png.color.bitdepth = 8;
			state.encoder.auto_convert = 0; // Must provide this or will ignore the input/output types
			png_data.clear();
			unsigned error = lodepng::encode(png_data, texture_pointer, cockpit_texture_width, cockpit_texture_height, state);

			// Now that the image is compressed, we can throw away the texture capture and tell the main thread to start capturing a new one immediately
			texture_pointer = NULL;

			// Send the compressed data to the socket
			iSendResult = send(ClientSocket, (const char *)png_data.data(), (int)png_data.size(), 0);
			if (iSendResult == SOCKET_ERROR) {
				log_printf("Error: TCP send failed with code %d\n", WSAGetLastError());
				closesocket(ClientSocket);
				break; // Exit the loop
			} else if (iSendResult != png_data.size()) {
				log_printf("Error: TCP transmission was %d bytes but expected to send %zu bytes\n", iSendResult, png_data.size());
			} else {
				// log_printf("Successfully sent PNG image of %dx%d with %d compressed bytes\n", cockpit_texture_width, cockpit_texture_height, iSendResult);
			}

			// Write to a disk file for debugging
			/*
			char fname[4096];
			static int incr = 0;
			incr++;
			sprintf(fname, "net_texture_%d.png", incr);
			FILE *fp = fopen(fname, "wb");
			if (fp == NULL) {
				log_printf("Could not save to file\n");
			}
			else {
				fwrite(png_data.data(), sizeof(unsigned char), png_data.size(), fp);
				fclose(fp);
				log_printf("Saved debugging image to %s\n", fname);
			}
			*/
		} while (iSendResult > 0);

		// Connection failed, so close this accepted socket
		iResult = shutdown(ClientSocket, SD_SEND);
		if (iResult == SOCKET_ERROR) {
			log_printf("shutdown failed with error: %d\n", WSAGetLastError());
			closesocket(ClientSocket);
		}
	}

	// Unreachable code - shut down everything
	closesocket(ListenSocket);
	closesocket(ClientSocket);
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
