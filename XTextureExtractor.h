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

#pragma once

#include "XPLMDisplay.h"
#include "XPLMGraphics.h"
#include "XPLMUtilities.h"
#include "XPLMPlanes.h"
#include "XPLMDataAccess.h"
#include "XPLMPlugin.h"
#include <string.h>
#include <stdio.h>
#if IBM
#include <windows.h>
#endif
#if LIN
#include <GL/gl.h>
#elif __GNUC__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#ifndef XPLM300
#error This is made to be compiled against the XPLM300 SDK
#endif

static char __log_printf_buffer[4096];
#define log_printf(fmt, ...) snprintf(__log_printf_buffer, 4096, "XTextureExtractor: " fmt, __VA_ARGS__), XPLMDebugString(__log_printf_buffer)

#define COCKPIT_MAX_WINDOWS 20
#define TCP_PLUGIN_PORT    "51500"
#define MAX_TEXTURE_WIDTH  2048
#define MAX_TEXTURE_HEIGHT 2048
#define TCP_INTRO_HEADER   4096
#define TCP_PLUGIN_VERSION "XTEv1"

extern void start_networking_thread(void);
extern unsigned char *texture_pointer;
extern GLint cockpit_texture_id;
extern GLint cockpit_texture_width;
extern GLint cockpit_texture_height;
extern int   cockpit_texture_seq;

extern char cockpit_aircraft_name[];
extern char cockpit_aircraft_filename[];
extern int cockpit_window_limit;
extern char _g_window_name[COCKPIT_MAX_WINDOWS][256]; // titles of each window
extern int _g_texture_lbrt[COCKPIT_MAX_WINDOWS][4]; // left, bottom, right, top
