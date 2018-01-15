// ---------------------------------------------------------------------
//
// X-Plane 11 Plugins
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


#include "XPLMDisplay.h"
#include "XPLMGraphics.h"
#include "XPLMUtilities.h"
#include "XPLMPlanes.h"
#include "XPLMDataAccess.h"
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


char *cdu_labels[] = {
	"PAGE LABEL LARGE FONT",
	"PAGE LABEL SMALL FONT",
	"LINE 1 LABEL SMALL FONT",
	"LINE 2 LABEL SMALL FONT",
	"LINE 3 LABEL SMALL FONT",
	"LINE 4 LABEL SMALL FONT",
	"LINE 5 LABEL SMALL FONT",
	"LINE 6 LABEL SMALL FONT",
	"LINE 1 LARGE FONT",
	"LINE 2 LARGE FONT",
	"LINE 3 LARGE FONT",
	"LINE 4 LARGE FONT",
	"LINE 5 LARGE FONT",
	"LINE 6 LARGE FONT",
	"LINE 1 LARGE FONT INVERSE",
	"LINE 2 LARGE FONT INVERSE",
	"LINE 3 LARGE FONT INVERSE",
	"LINE 4 LARGE FONT INVERSE",
	"LINE 5 LARGE FONT INVERSE",
	"LINE 6 LARGE FONT INVERSE",
	"LINE 1 SMALL FONT",
	"LINE 2 SMALL FONT",
	"LINE 3 SMALL FONT",
	"LINE 4 SMALL FONT",
	"LINE 5 SMALL FONT",
	"LINE 6 SMALL FONT",
	"LINE ENTRY LARGE FONT",
	"LINE ENTRY LARGE FONT INVERSE",
	"FMC EXEC LIGHT",
};


char* cdu_paths[] = {
	"laminar/B738/fmc1/Line00_L",
	"laminar/B738/fmc1/Line00_S",
	"laminar/B738/fmc1/Line01_X",
	"laminar/B738/fmc1/Line02_X",
	"laminar/B738/fmc1/Line03_X",
	"laminar/B738/fmc1/Line04_X",
	"laminar/B738/fmc1/Line05_X",
	"laminar/B738/fmc1/Line06_X",
	"laminar/B738/fmc1/Line01_L",
	"laminar/B738/fmc1/Line02_L",
	"laminar/B738/fmc1/Line03_L",
	"laminar/B738/fmc1/Line04_L",
	"laminar/B738/fmc1/Line05_L",
	"laminar/B738/fmc1/Line06_L",
	"laminar/B738/fmc1/Line01_I",
	"laminar/B738/fmc1/Line02_I",
	"laminar/B738/fmc1/Line03_I",
	"laminar/B738/fmc1/Line04_I",
	"laminar/B738/fmc1/Line05_I",
	"laminar/B738/fmc1/Line06_I",
	"laminar/B738/fmc1/Line01_S",
	"laminar/B738/fmc1/Line02_S",
	"laminar/B738/fmc1/Line03_S",
	"laminar/B738/fmc1/Line04_S",
	"laminar/B738/fmc1/Line05_S",
	"laminar/B738/fmc1/Line06_S",
	"laminar/B738/fmc1/Line_entry",
	"laminar/B738/fmc1/Line_entry_I",
	"laminar/B738/indicators/fms_exec_light_pilot",
};

int cdu_elements = sizeof(cdu_labels) / sizeof(char *);
XPLMDataRef cdu_refs[sizeof(cdu_labels) / sizeof(char *)] = { NULL };


static XPLMWindowID	g_window;

XPLMDataRef gAcfDescription = NULL;

char __log_printf_buffer[4096];
#define log_printf(fmt, ...) snprintf(__log_printf_buffer, 4096, fmt, __VA_ARGS__), XPLMDebugString(__log_printf_buffer)

void				draw(XPLMWindowID in_window_id, void * in_refcon);

int					dummy_mouse_handler(XPLMWindowID in_window_id, int x, int y, int is_down, void * in_refcon) { return 0; }
XPLMCursorStatus	dummy_cursor_status_handler(XPLMWindowID in_window_id, int x, int y, void * in_refcon) { return xplm_CursorDefault; }
int					dummy_wheel_handler(XPLMWindowID in_window_id, int x, int y, int wheel, int clicks, void * in_refcon) { return 0; }
void				dummy_key_handler(XPLMWindowID in_window_id, char key, XPLMKeyFlags flags, char virtual_key, void * in_refcon, int losing_focus) { }

PLUGIN_API int XPluginStart(
						char *		outName,
						char *		outSig,
						char *		outDesc)
{
	log_printf("XPluginStart: CDUExtract plugin\n");
	strcpy(outName, "CDUExtractPlugin");
	strcpy(outSig, "net.waynepiekarski.cduextractplugin");
	strcpy(outDesc, "Extracts out Zibo CDU display strings into a separate window.");

	// We're not guaranteed that the main monitor's lower left is at (0, 0)... we'll need to query for the global desktop bounds!
	int global_desktop_bounds[4]; // left, bottom, right, top
	XPLMGetScreenBoundsGlobal(&global_desktop_bounds[0], &global_desktop_bounds[3], &global_desktop_bounds[2], &global_desktop_bounds[1]);

	XPLMCreateWindow_t params;
	params.structSize = sizeof(params);
	params.left = global_desktop_bounds[0] + 50;
	params.bottom = global_desktop_bounds[1] + 150;
	params.right = global_desktop_bounds[0] + 700;
	params.top = global_desktop_bounds[1] + 800;
	params.visible = 1;
	params.drawWindowFunc = draw;
	params.handleMouseClickFunc = dummy_mouse_handler;
	params.handleRightClickFunc = dummy_mouse_handler;
	params.handleMouseWheelFunc = dummy_wheel_handler;
	params.handleKeyFunc = dummy_key_handler;
	params.handleCursorFunc = dummy_cursor_status_handler;
	params.refcon = NULL;
	params.layer = xplm_WindowLayerFloatingWindows;
	params.decorateAsFloatingWindow = 1;

	// Open windows in reverse order so the most important are at the top
	g_window = XPLMCreateWindowEx(&params);
	XPLMSetWindowPositioningMode(g_window, xplm_WindowPositionFree, -1);
	XPLMSetWindowGravity(g_window, 0, 1, 0, 1); // As the X-Plane window resizes, keep our size constant, and our left and top edges in the same place relative to the window's left/top
	// XPLMSetWindowResizingLimits(g_window, 200, 200, 1000, 1000); // Limit resizing our window: maintain a minimum width/height of 200 boxels and a max width/height of 500
	XPLMSetWindowTitle(g_window, "CDU Extract");

	gAcfDescription = XPLMFindDataRef("sim/aircraft/view/acf_descrip");

	return (g_window != NULL);
}

PLUGIN_API void	XPluginStop(void)
{
	log_printf("XPluginStop: CDUExtract plugin\n");
	XPLMDestroyWindow(g_window);
	g_window = NULL;
}

PLUGIN_API void XPluginDisable(void) { }
PLUGIN_API int  XPluginEnable(void)  { return 1; }
PLUGIN_API void XPluginReceiveMessage(XPLMPluginID inFrom, int inMsg, void * inParam)
{
	log_printf("XPluginReceiveMessage CDUExtract: inFrom=%d, inMsg=%d, inParam=%p\n", inFrom, inMsg, inParam);
}



void	draw(XPLMWindowID in_window_id, void * in_refcon)
{
	char scratch_buffer[150];
	float col_white[] = {1.0, 1.0, 1.0};

	XPLMSetGraphicsState(
			0 /* no fog */,
			1 /* 0 texture units */,
			0 /* no lighting */,
			0 /* no alpha testing */,
			0 /* do alpha blend */,
			0 /* do depth testing */,
			0 /* no depth writing */
	);

	// We draw our rudimentary button boxes based on the height of the button text
	int char_height;
	XPLMGetFontDimensions(xplmFont_Proportional, NULL, &char_height, NULL);

	int l, t, r, b;
	XPLMGetWindowGeometry(in_window_id, &l, &t, &r, &b);
	int y = t;

	// Dump out the CDU values
	sprintf(scratch_buffer, "CDU Dump (%d items)\n", cdu_elements);
	XPLMDrawString(col_white, l, y, scratch_buffer, NULL, xplmFont_Proportional);
	y -= 1.5 * char_height;

	char extracted[1000];
	// log_printf("Processing %d CDU elements ...\n", cdu_elements);
	for (int i = 0; i < cdu_elements; i++) {
		if (cdu_refs[i] == NULL) {
			// log_printf("Finding reference with path [%s] for [%s]\n", cdu_paths[i], cdu_labels[i]);
			cdu_refs[i] = XPLMFindDataRef(cdu_paths[i]);
			// log_printf("cdu_refs[%d]=%p\n", i, cdu_refs[i]);
		}
		int result = 0;
		if (cdu_refs[i] == NULL) {
			strcpy(extracted, "(null)");
		} else {
			result = XPLMGetDatab(cdu_refs[i], extracted, 0, 100);
			if (result < 0) {
				log_printf("Invalid result %d returned?", result);
			} else {
				// Need to terminate the Zibo strings (the acf_descrip doesn't need it though?)
				extracted[result] = '\0';
			}
		}
		// log_printf("Displaying label=[%s] path=[%s] extracted=[%s] result=%d\n", cdu_labels[i], cdu_paths[i], extracted, result);
		sprintf(scratch_buffer, "%.2d [%-24s] %30s [%s]\n", i, extracted, cdu_labels[i], cdu_paths[i]);
		XPLMDrawString(col_white, l, y, scratch_buffer, NULL, xplmFont_Led);
		y -= 1.5 * char_height;
	}
}
