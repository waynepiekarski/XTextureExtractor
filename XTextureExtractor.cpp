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

char __log_printf_buffer[4096];
#define log_printf(fmt, ...) snprintf(__log_printf_buffer, 4096, fmt, __VA_ARGS__), XPLMDebugString(__log_printf_buffer)

#define COCKPIT_MAX_WINDOWS 5
static XPLMWindowID	g_window[COCKPIT_MAX_WINDOWS];
XPLMDataRef gAcfNotes = NULL;


#define AIRCRAFT_FF767 1
#define AIRCRAFT_XP737 2
#define AIRCRAFT_ZB738 3
#define AIRCRAFT_FF757 4
#define AIRCRAFT_UNKNOWN -1
int get_aircraft_type(void) {
	char filename[256];
	char path[256];
	char notes[256];
	XPLMGetNthAircraftModel(XPLM_USER_AIRCRAFT, filename, path);
	_strlwr(filename);
	_strlwr(path);

	int result;
	if (gAcfNotes == NULL) {
		strcpy(notes, "(null)");
	}
	else {
		result = XPLMGetDatab(gAcfNotes, notes, 0, 256);
		if (result < 0) {
			strcpy(notes, "(error)");
		}
		else {
			// Need to terminate the strings
			notes[result] = '\0';
			_strlwr(notes);
		}
	}

	// Note all comparisons are forced to lower case since Windows is case insensitive
	log_printf("Detecting aircraft, found filename [%s], path [%s], acf_notes [%s]\n", filename, path, notes);
	if (!strcmp(filename, "767-300er_xp11.acf")) {
		log_printf("Found Flight Factor 767 match based on filename\n");
		return AIRCRAFT_FF767;
	} else if (!strcmp(filename, "757-200_xp11.acf")) {
		log_printf("Found Flight Factor 757 match based on filename\n");
		return AIRCRAFT_FF757;
	} else if (strstr(notes, "zibomod") != NULL) {
		log_printf("Found ZiboMod 738 based on acf_notes\n");
		return AIRCRAFT_ZB738;
	} else if (!strcmp(filename, "b738.acf") && (strstr(path, "\\laminar research\\boeing b737-800\\b738.acf") != NULL)) {
		log_printf("Matching Laminar original 738 based on full path match\n");
		return AIRCRAFT_XP737;
    } else if (!strcmp(filename, "b738.acf")) {
		log_printf("Assuming XP737 even though path was not an exact match and it is not from Zibo, probably an edited 737\n");
		return AIRCRAFT_XP737;
	} else {
		log_printf("Could not find suitable match for aircraft information\n");
		return AIRCRAFT_UNKNOWN;
	}
}

GLint cockpit_texture_id = 0;
GLint cockpit_texture_width = -1;
GLint cockpit_texture_height = -1;
GLint cockpit_texture_format = -1;
GLint cockpit_texture_last = 3000; // This is the starting point for our texture search, it needs to be higher than any texture id and I can't query for it
GLint cockpit_texture_jump = 1000; // Whenever the aircraft switches, bump up the last texture id, because this plugin seems to cause texture ids to exceed, not sure what the limit is here
int   cockpit_aircraft = -1;
bool  cockpit_dirty = false;
char  cockpit_aircraft_name[128];
int   cockpit_save_count = 0;
char  cockpit_save_string[32];

static void find_last_match_in_texture(GLint start_texture_id)
{
	int fw, fh, ff;
	cockpit_aircraft = get_aircraft_type();
	switch (cockpit_aircraft) {
	case AIRCRAFT_FF767:
		fw = 2048;
		fh = 2048;
		ff = 32856;
		strcpy(cockpit_aircraft_name, "FF767");
		log_printf("FF767 Detected 2048x2048\n");
		break;
	case AIRCRAFT_FF757:
		fw = 2048;
		fh = 2048;
		ff = 32856;
		strcpy(cockpit_aircraft_name, "FF757");
		log_printf("FF757 Detected 2048x2048\n");
		break;
	case AIRCRAFT_XP737:
		fw = 2048;
		fh = 1024;
		ff = 32856;
		strcpy(cockpit_aircraft_name, "XP737");
		log_printf("XP737 Detected 2048x1024\n");
		break;
	case AIRCRAFT_ZB738:
		fw = 2048;
		fh = 2048;
		ff = 32856;
		strcpy(cockpit_aircraft_name, "ZB738");
		log_printf("ZB738 Detected 2048x2048\n");
		break;
	default:
		fw = 2048;
		fh = 2048;
		ff = 0;
		strcpy(cockpit_aircraft_name, "UNKNOWN");
		log_printf("Unknown aircraft!\n");
		break;
	}

	if (start_texture_id <= 0)
		start_texture_id = cockpit_texture_last;
	log_printf("Finding last texture from %d (max=%d) that matches fw=%d, fh=%d, ff=%d for aircraft %s\n", start_texture_id, cockpit_texture_last, fw, fh, ff, cockpit_aircraft_name);
	int tw, th, tf;
	for (int i = start_texture_id; i >= 0; i--) {
		XPLMBindTexture2d(i, 0);
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tw);
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &th);
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &tf);
		if ((tw == fw) && (th == fh) && (tf == ff)) {
			log_printf("Found texture id=%d, width=%d, height=%d, internal format == %d\n", i, tw, th, tf);
			cockpit_texture_id = i;
			cockpit_texture_width = tw;
			cockpit_texture_height = th;
			cockpit_texture_format = tf;
			return;
		}
	}
	cockpit_texture_id = 0;
	cockpit_texture_width = 0;
	cockpit_texture_height = 0;
	cockpit_texture_format = 0;
	log_printf("Did not find matching texture, using id 0 instead\n");
}

void load_window_state();
void				draw(XPLMWindowID in_window_id, void * in_refcon);
int					handle_mouse(XPLMWindowID in_window_id, int x, int y, int is_down, void * in_refcon);

int					dummy_mouse_handler(XPLMWindowID in_window_id, int x, int y, int is_down, void * in_refcon) { return 0; }
XPLMCursorStatus	dummy_cursor_status_handler(XPLMWindowID in_window_id, int x, int y, void * in_refcon) { return xplm_CursorDefault; }
int					dummy_wheel_handler(XPLMWindowID in_window_id, int x, int y, int wheel, int clicks, void * in_refcon) { return 0; }
void				dummy_key_handler(XPLMWindowID in_window_id, char key, XPLMKeyFlags flags, char virtual_key, void * in_refcon, int losing_focus) { }

static float _g_pop_button_lbrt[COCKPIT_MAX_WINDOWS][4]; // left, bottom, right, top
static float _g_texture_button_lbrt[COCKPIT_MAX_WINDOWS][4]; // left, bottom, right, top
static float _g_scan_button_lbrt[COCKPIT_MAX_WINDOWS][4]; // left, bottom, right, top
static float _g_load_button_lbrt[COCKPIT_MAX_WINDOWS][4]; // left, bottom, right, top
static float _g_save_button_lbrt[COCKPIT_MAX_WINDOWS][4]; // left, bottom, right, top
static float _g_clear_button_lbrt[COCKPIT_MAX_WINDOWS][4]; // left, bottom, right, top
static float _g_dump_button_lbrt[COCKPIT_MAX_WINDOWS][4]; // left, bottom, right, top

static int	coord_in_rect(float x, float y, float * bounds_lbrt)  { return ((x >= bounds_lbrt[0]) && (x < bounds_lbrt[2]) && (y < bounds_lbrt[3]) && (y >= bounds_lbrt[1])); }


PLUGIN_API int XPluginStart(
						char *		outName,
						char *		outSig,
						char *		outDesc)
{
	strcpy(outName, "XTextureExtractorPlugin");
	strcpy(outSig, "net.waynepiekarski.windowcockpitplugin");
	sprintf(outDesc, "Extracts out cockpit textures into a separate window - compiled %s %s", __DATE__, __TIME__);
	log_printf("XPluginStart: XTextureExtractor plugin - %s\n", outDesc);

	// Register to listen for aircraft notes information, so we can detect Zibo 738 later
	gAcfNotes = XPLMFindDataRef("sim/aircraft/view/acf_notes");

	strcpy(cockpit_save_string, "Sv");

	// We're not guaranteed that the main monitor's lower left is at (0, 0)... we'll need to query for the global desktop bounds!
	int global_desktop_bounds[4]; // left, bottom, right, top
	XPLMGetScreenBoundsGlobal(&global_desktop_bounds[0], &global_desktop_bounds[3], &global_desktop_bounds[2], &global_desktop_bounds[1]);

	XPLMCreateWindow_t params;
	params.structSize = sizeof(params);
	params.left = global_desktop_bounds[0] + 50;
	params.bottom = global_desktop_bounds[1] + 150;
	params.right = global_desktop_bounds[0] + 350;
	params.top = global_desktop_bounds[1] + 450;
	params.visible = 1;
	params.drawWindowFunc = draw;
	params.handleMouseClickFunc = handle_mouse;
	params.handleRightClickFunc = dummy_mouse_handler;
	params.handleMouseWheelFunc = dummy_wheel_handler;
	params.handleKeyFunc = dummy_key_handler;
	params.handleCursorFunc = dummy_cursor_status_handler;
	params.refcon = NULL;
	params.layer = xplm_WindowLayerFloatingWindows;
	params.decorateAsFloatingWindow = 1;

	// Open windows in reverse order so the most important are at the top
	for (intptr_t i = COCKPIT_MAX_WINDOWS-1; i >= 0; i--) {
		params.refcon = (void *)i; // Store the window id
		params.left += 20; // Stagger the windows
		params.bottom -= 20;
		params.right += 20;
		params.top -= 20;
		g_window[i] = XPLMCreateWindowEx(&params);
		XPLMSetWindowPositioningMode(g_window[i], xplm_WindowPositionFree, -1);
		XPLMSetWindowGravity(g_window[i], 0, 1, 0, 1); // As the X-Plane window resizes, keep our size constant, and our left and top edges in the same place relative to the window's left/top
		// XPLMSetWindowResizingLimits(g_window[i], 200, 200, 1000, 1000); // Limit resizing our window: maintain a minimum width/height of 200 boxels and a max width/height of 500
		switch (i) {
		case 0: XPLMSetWindowTitle(g_window[i], "NAV: Window Cockpit"); break;
		case 1: XPLMSetWindowTitle(g_window[i], "HSI: Window Cockpit"); break;
		case 2: XPLMSetWindowTitle(g_window[i], "EICAS1: Window Cockpit"); break;
		case 3: XPLMSetWindowTitle(g_window[i], "EICAS2: Window Cockpit"); break;
		case 4: XPLMSetWindowTitle(g_window[i], "MISC: Window Cockpit"); break;
		default: break;
		}
	}

	return (g_window[0] != NULL);
}

PLUGIN_API void	XPluginStop(void)
{
	log_printf("XPluginStop: XTextureExtractor plugin\n");

	// Since we created the window, we'll be good citizens and clean it up
	for (int i = 0; i < COCKPIT_MAX_WINDOWS; i++) {
		XPLMDestroyWindow(g_window[i]);
		g_window[i] = NULL;
	}
}

PLUGIN_API void XPluginDisable(void) { }
PLUGIN_API int  XPluginEnable(void)  { return 1; }
PLUGIN_API void XPluginReceiveMessage(XPLMPluginID inFrom, int inMsg, void * inParam)
{
	log_printf("XPluginReceiveMessage XTextureExtractor: inFrom=%d, inMsg=%d, inParam=%p\n", inFrom, inMsg, inParam);
	if (inMsg == 103) {
		// Seems like 103 is sent just when the aircraft is finished loading, we can try grabbing the texture here
		log_printf("Found event inMsg=103, lets try to find the texture right now, increasing last texture id by %d to %d\n", cockpit_texture_jump, cockpit_texture_last + cockpit_texture_jump);
		cockpit_dirty = true;
		cockpit_texture_last += cockpit_texture_jump;
	}
}


void save_tga(GLint texId)
{
	int tw, th, tf;
	XPLMBindTexture2d(texId, 0);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tw);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &th);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &tf);

	unsigned char * pixels = new unsigned char[tw * th * 3];

	glGetTexImage(GL_TEXTURE_2D, 0, GL_BGR_EXT, GL_UNSIGNED_BYTE, pixels);

	FILE *fp = fopen("texture_save.tga", "wb");
	if (fp == NULL) {
		log_printf("Could not save to file\n");
		return;
	}

	unsigned char tga_header[12] = { 0,0,2,0,0,0,0,0,0,0,0,0 };

	unsigned char image_header[6] = {
		((int)(tw % 256)),
		((int)(tw / 256)),
		((int)(th % 256)),
		((int)(th / 256)), 24, 0 };
	
	fwrite(tga_header, sizeof(unsigned char), 12, fp);
	fwrite(image_header, sizeof(unsigned char), 6, fp);
	fwrite(pixels, sizeof(unsigned char), tw * th * 3, fp);

	fclose(fp);
	delete[] pixels;
}

void dump_debug()
{
	log_printf("Dump request\n");

	int tw, th, tf;
	int fw, fh;

	switch (get_aircraft_type()) {
	case AIRCRAFT_FF767:
		fw = 2048;
		fh = 2048;
		log_printf("FF767 Detected 2048x2048\n");
		break;
	case AIRCRAFT_FF757:
		fw = 2048;
		fh = 2048;
		log_printf("FF757 Detected 2048x2048\n");
		break;
	case AIRCRAFT_XP737:
		fw = 2048;
		fh = 1024;
		log_printf("XP737 Detected 2048x1024\n");
	case AIRCRAFT_ZB738:
		fw = 2048;
		fh = 2048;
		log_printf("ZB738 Detected 2048x2048\n");
	default:
		fw = 2048;
		fh = 2048;
		log_printf("Unknown Detected\n");
	}

	for (int i = 0; i < cockpit_texture_last; i++) {
		XPLMBindTexture2d(i, 0);
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tw);
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &th);
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &tf);

		// XP737 panel is "fbo-list panel texture 1-fbo" at 2048x1024
		// FF7*7 panel is "fbo-list panel texture 1-fbo" at 2048x2048
		// ZB738 panel is "fbo-list panel texture 1-fbo" at 2048x2048
		if ((tw == fw) && (th == fh)) {
			log_printf("texture id=%d, width=%d, height=%d iformat=%d\n", i, tw, th, tf);
		}
	}

	log_printf("Saving texture for id %d\n", cockpit_texture_id);
	save_tga(cockpit_texture_id);
	log_printf("TGA save is complete\n");
}


void	draw(XPLMWindowID in_window_id, void * in_refcon)
{
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

	// We'll change the text of the pop-in/pop-out button based on our current state
	int is_popped_out = XPLMWindowIsPoppedOut(in_window_id);
	const char * pop_label = is_popped_out ? "Pop-In" : "Pop-Out";

	int l, t, r, b;
	XPLMGetWindowGeometry(in_window_id, &l, &t, &r, &b);

	intptr_t win_num = (intptr_t)in_refcon;
	float *g_pop_button_lbrt = &_g_pop_button_lbrt[win_num][0];
	float *g_texture_button_lbrt = &_g_texture_button_lbrt[win_num][0];
	float *g_scan_button_lbrt = &_g_scan_button_lbrt[win_num][0];
	float *g_save_button_lbrt = &_g_save_button_lbrt[win_num][0];
	float *g_load_button_lbrt = &_g_load_button_lbrt[win_num][0];
	float *g_clear_button_lbrt = &_g_clear_button_lbrt[win_num][0];
	float *g_dump_button_lbrt = &_g_dump_button_lbrt[win_num][0];

	// Draw our buttons
	{
		// Position the pop-in/pop-out button in the upper left of the window
		g_pop_button_lbrt[0] = l + 0;
		g_pop_button_lbrt[3] = t - 0;
		g_pop_button_lbrt[2] = g_pop_button_lbrt[0] + XPLMMeasureString(xplmFont_Proportional, pop_label, strlen(pop_label)); // *just* wide enough to fit the button text
		g_pop_button_lbrt[1] = g_pop_button_lbrt[3] - (1.25f * char_height); // a bit taller than the button text
		
		// Position the "move to lower left" button just to the right of the pop-in/pop-out button
		char texture_info_text[128];
		sprintf(texture_info_text, "GL%d [%s]", cockpit_texture_id, cockpit_aircraft_name);

#define DEFINE_BOX(_array, _left, _string) _array[0] = _left[2] + 10, _array[1] = _left[1], _array[2] = _array[0] + XPLMMeasureString(xplmFont_Proportional, _string, strlen(_string)), _array[3] = _left[3]
		DEFINE_BOX(g_texture_button_lbrt, g_pop_button_lbrt, texture_info_text);
		DEFINE_BOX(g_scan_button_lbrt, g_texture_button_lbrt, "<<");
		DEFINE_BOX(g_load_button_lbrt, g_scan_button_lbrt, "Ld");
		DEFINE_BOX(g_save_button_lbrt, g_load_button_lbrt, cockpit_save_string);
		DEFINE_BOX(g_clear_button_lbrt, g_save_button_lbrt, "Clr");
		DEFINE_BOX(g_dump_button_lbrt, g_clear_button_lbrt, "Dbg");
#undef DEFINE_BOX

		// Draw the boxes around our rudimentary buttons
		float green[] = {0.0, 1.0, 0.0, 1.0};
		glColor4fv(green);
#define DRAW_BOX(_array) glBegin(GL_LINE_LOOP), glVertex2i(_array[0], _array[3]), glVertex2i(_array[2], _array[3]), glVertex2i(_array[2], _array[1]), glVertex2i(_array[0], _array[1]), glEnd()
		DRAW_BOX(g_pop_button_lbrt);
		DRAW_BOX(g_texture_button_lbrt);
		DRAW_BOX(g_scan_button_lbrt);
		DRAW_BOX(g_load_button_lbrt);
		DRAW_BOX(g_save_button_lbrt);
		DRAW_BOX(g_clear_button_lbrt);
		DRAW_BOX(g_dump_button_lbrt);
#undef DRAW_BOX

		// Draw the button text (pop in/pop out)
		XPLMDrawString(col_white, g_pop_button_lbrt[0], g_pop_button_lbrt[1] + 4, (char *)pop_label, NULL, xplmFont_Proportional);

		// Draw the button text (texture info)
		XPLMDrawString(col_white, g_texture_button_lbrt[0], g_texture_button_lbrt[1] + 4, (char *)texture_info_text, NULL, xplmFont_Proportional);

		// Draw the load/save/clear text
		XPLMDrawString(col_white, g_scan_button_lbrt[0],  g_scan_button_lbrt[1] + 4, (char *)"<<", NULL, xplmFont_Proportional);
		XPLMDrawString(col_white, g_load_button_lbrt[0],  g_load_button_lbrt[1]  + 4, (char *)"Ld", NULL, xplmFont_Proportional);
		XPLMDrawString(col_white, g_save_button_lbrt[0],  g_save_button_lbrt[1]  + 4, (char *)cockpit_save_string, NULL, xplmFont_Proportional);
		XPLMDrawString(col_white, g_clear_button_lbrt[0], g_clear_button_lbrt[1] + 4, (char *)"Clr", NULL, xplmFont_Proportional);
		XPLMDrawString(col_white, g_dump_button_lbrt[0],  g_dump_button_lbrt[1]  + 4, (char *)"Dbg", NULL, xplmFont_Proportional);
	}

	XPLMSetGraphicsState(
		0 /* no fog */,
		1 /* 0 texture units */,
		0 /* no lighting */,
		0 /* no alpha testing */,
		0 /* do alpha blend */,
		0 /* do depth testing */,
		0 /* no depth writing */
	);

	if (cockpit_dirty) {
		log_printf("Detected aircraft dirty flag set, so finding texture and loading window layouts\n");
		find_last_match_in_texture(-1);
		load_window_state();
		cockpit_dirty = false;
	}

	const int topInset = 20;
	switch (cockpit_aircraft) {
	case AIRCRAFT_FF767:
	case AIRCRAFT_FF757:
		if (in_window_id == g_window[0]) {
			// Navigation display
			XPLMBindTexture2d(cockpit_texture_id, 0);
			glBegin(GL_QUADS);
			float leftX = 18;
			float topY = 372;
			float rightX = 412;
			float botY = 841;
			float maxX = 2048;
			float maxY = 2048;
			glTexCoord2f(leftX / maxX, (maxY - topY) / maxY);  glVertex2i(l, t - topInset); // Top left
			glTexCoord2f(rightX / maxX, (maxY - topY) / maxY);  glVertex2i(r, t - topInset); // Top right
			glTexCoord2f(rightX / maxX, (maxY - botY) / maxY);  glVertex2i(r, b);    // Bottom right
			glTexCoord2f(leftX / maxX, (maxY - botY) / maxY);  glVertex2i(l, b);    // Bottom left
			glEnd();
		}
		else if (in_window_id == g_window[1]) {
			// HSI display
			XPLMBindTexture2d(cockpit_texture_id, 0);
			glBegin(GL_QUADS);
			float leftX = 18;
			float topY = 11;
			float rightX = 418;
			float botY = 319;
			float maxX = 2048;
			float maxY = 2048;
			glTexCoord2f(leftX / maxX, (maxY - topY) / maxY);  glVertex2i(l, t - topInset); // Top left
			glTexCoord2f(rightX / maxX, (maxY - topY) / maxY);  glVertex2i(r, t - topInset); // Top right
			glTexCoord2f(rightX / maxX, (maxY - botY) / maxY);  glVertex2i(r, b);    // Bottom right
			glTexCoord2f(leftX / maxX, (maxY - botY) / maxY);  glVertex2i(l, b);    // Bottom left
			glEnd();
		}
		else if (in_window_id == g_window[2]) {
			// EICAS display
			XPLMBindTexture2d(cockpit_texture_id, 0);
			glBegin(GL_QUADS);
			float leftX = 442;
			float topY = 7;
			float rightX = 926;
			float botY = 412;
			float maxX = 2048;
			float maxY = 2048;
			glTexCoord2f(leftX / maxX, (maxY - topY) / maxY);  glVertex2i(l, t - topInset); // Top left
			glTexCoord2f(rightX / maxX, (maxY - topY) / maxY);  glVertex2i(r, t - topInset); // Top right
			glTexCoord2f(rightX / maxX, (maxY - botY) / maxY);  glVertex2i(r, b);    // Bottom right
			glTexCoord2f(leftX / maxX, (maxY - botY) / maxY);  glVertex2i(l, b);    // Bottom left
			glEnd();
		}
		else if (in_window_id == g_window[3]) {
			// 2nd extra display (not shown by default in FF767)
			XPLMBindTexture2d(cockpit_texture_id, 0);
			glBegin(GL_QUADS);
			float leftX = 943;
			float topY = 9;
			float rightX = 1427;
			float botY = 414;
			float maxX = 2048;
			float maxY = 2048;
			glTexCoord2f(leftX / maxX, (maxY - topY) / maxY);  glVertex2i(l, t - topInset); // Top left
			glTexCoord2f(rightX / maxX, (maxY - topY) / maxY);  glVertex2i(r, t - topInset); // Top right
			glTexCoord2f(rightX / maxX, (maxY - botY) / maxY);  glVertex2i(r, b);    // Bottom right
			glTexCoord2f(leftX / maxX, (maxY - botY) / maxY);  glVertex2i(l, b);    // Bottom left
			glEnd();
		}
		else if (in_window_id == g_window[4]) {
			// Misc display
			XPLMBindTexture2d(cockpit_texture_id, 0);
			glBegin(GL_QUADS);
			float leftX = 434;
			float topY = 420;
			float rightX = 1576;
			float botY = 854;
			float maxX = 2048;
			float maxY = 2048;
			glTexCoord2f(leftX / maxX, (maxY - topY) / maxY);  glVertex2i(l, t - topInset); // Top left
			glTexCoord2f(rightX / maxX, (maxY - topY) / maxY);  glVertex2i(r, t - topInset); // Top right
			glTexCoord2f(rightX / maxX, (maxY - botY) / maxY);  glVertex2i(r, b);    // Bottom right
			glTexCoord2f(leftX / maxX, (maxY - botY) / maxY);  glVertex2i(l, b);    // Bottom left
			glEnd();
		}
		break;
	case AIRCRAFT_ZB738:
		if (in_window_id == g_window[0]) {
			// Navigation display
			XPLMBindTexture2d(cockpit_texture_id, 0);
			glBegin(GL_QUADS);
			float leftX = 524;
			float topY = 1550;
			float rightX = 1018;
			float botY = 2038;
			float maxX = 2048;
			float maxY = 2048;
			glTexCoord2f(leftX / maxX, (maxY - topY) / maxY);  glVertex2i(l, t - topInset); // Top left
			glTexCoord2f(rightX / maxX, (maxY - topY) / maxY);  glVertex2i(r, t - topInset); // Top right
			glTexCoord2f(rightX / maxX, (maxY - botY) / maxY);  glVertex2i(r, b);    // Bottom right
			glTexCoord2f(leftX / maxX, (maxY - botY) / maxY);  glVertex2i(l, b);    // Bottom left
			glEnd();
		}
		else if (in_window_id == g_window[1]) {
			// HSI display
			XPLMBindTexture2d(cockpit_texture_id, 0);
			glBegin(GL_QUADS);
			float leftX = 9;
			float topY = 1538;
			float rightX = 516;
			float botY = 2043;
			float maxX = 2048;
			float maxY = 2048;
			glTexCoord2f(leftX / maxX, (maxY - topY) / maxY);  glVertex2i(l, t - topInset); // Top left
			glTexCoord2f(rightX / maxX, (maxY - topY) / maxY);  glVertex2i(r, t - topInset); // Top right
			glTexCoord2f(rightX / maxX, (maxY - botY) / maxY);  glVertex2i(r, b);    // Bottom right
			glTexCoord2f(leftX / maxX, (maxY - botY) / maxY);  glVertex2i(l, b);    // Bottom left
			glEnd();
		}
		else if (in_window_id == g_window[2]) {
			// EICAS display
			XPLMBindTexture2d(cockpit_texture_id, 0);
			glBegin(GL_QUADS);
			float leftX = 1040;
			float topY = 1550;
			float rightX = 1527;
			float botY = 2028;
			float maxX = 2048;
			float maxY = 2048;
			glTexCoord2f(leftX / maxX, (maxY - topY) / maxY);  glVertex2i(l, t - topInset); // Top left
			glTexCoord2f(rightX / maxX, (maxY - topY) / maxY);  glVertex2i(r, t - topInset); // Top right
			glTexCoord2f(rightX / maxX, (maxY - botY) / maxY);  glVertex2i(r, b);    // Bottom right
			glTexCoord2f(leftX / maxX, (maxY - botY) / maxY);  glVertex2i(l, b);    // Bottom left
			glEnd();
		}
		else if (in_window_id == g_window[3]) {
			// Extra EICAS display
			XPLMBindTexture2d(cockpit_texture_id, 0);
			glBegin(GL_QUADS);
			float leftX = 1533;
			float topY = 1551;
			float rightX = 2038;
			float botY = 2037;
			float maxX = 2048;
			float maxY = 2048;
			glTexCoord2f(leftX / maxX, (maxY - topY) / maxY);  glVertex2i(l, t - topInset); // Top left
			glTexCoord2f(rightX / maxX, (maxY - topY) / maxY);  glVertex2i(r, t - topInset); // Top right
			glTexCoord2f(rightX / maxX, (maxY - botY) / maxY);  glVertex2i(r, b);    // Bottom right
			glTexCoord2f(leftX / maxX, (maxY - botY) / maxY);  glVertex2i(l, b);    // Bottom left
			glEnd();
		}
		else if (in_window_id == g_window[4]) {
			// CDU Display
			XPLMBindTexture2d(cockpit_texture_id, 0);
			glBegin(GL_QUADS);
			float leftX = 10;
			float topY = 544;
			float rightX = 540;
			float botY = 1023;
			float maxX = 2048;
			float maxY = 2048;
			glTexCoord2f(leftX / maxX, (maxY - topY) / maxY);  glVertex2i(l, t - topInset); // Top left
			glTexCoord2f(rightX / maxX, (maxY - topY) / maxY);  glVertex2i(r, t - topInset); // Top right
			glTexCoord2f(rightX / maxX, (maxY - botY) / maxY);  glVertex2i(r, b);    // Bottom right
			glTexCoord2f(leftX / maxX, (maxY - botY) / maxY);  glVertex2i(l, b);    // Bottom left
			glEnd();
		}
		break;
	case AIRCRAFT_XP737:
		if (in_window_id == g_window[0]) {
			// Navigation display
			XPLMBindTexture2d(cockpit_texture_id, 0);
			glBegin(GL_QUADS);
			float leftX = 516;
			float topY = 521;
			float rightX = 1026;
			float botY = 968;
			float maxX = 2048;
			float maxY = 1024;
			glTexCoord2f(leftX / maxX, (maxY - topY) / maxY);  glVertex2i(l, t - topInset); // Top left
			glTexCoord2f(rightX / maxX, (maxY - topY) / maxY);  glVertex2i(r, t - topInset); // Top right
			glTexCoord2f(rightX / maxX, (maxY - botY) / maxY);  glVertex2i(r, b);    // Bottom right
			glTexCoord2f(leftX / maxX, (maxY - botY) / maxY);  glVertex2i(l, b);    // Bottom left
			glEnd();
		}
		else if (in_window_id == g_window[1]) {
			// HSI display
			XPLMBindTexture2d(cockpit_texture_id, 0);
			glBegin(GL_QUADS);
			float leftX = 18;
			float topY = 514;
			float rightX = 514;
			float botY = 1019;
			float maxX = 2048;
			float maxY = 1024;
			glTexCoord2f(leftX / maxX, (maxY - topY) / maxY);  glVertex2i(l, t - topInset); // Top left
			glTexCoord2f(rightX / maxX, (maxY - topY) / maxY);  glVertex2i(r, t - topInset); // Top right
			glTexCoord2f(rightX / maxX, (maxY - botY) / maxY);  glVertex2i(r, b);    // Bottom right
			glTexCoord2f(leftX / maxX, (maxY - botY) / maxY);  glVertex2i(l, b);    // Bottom left
			glEnd();
		}
		else if (in_window_id == g_window[2]) {
			// EICAS display
			XPLMBindTexture2d(cockpit_texture_id, 0);
			glBegin(GL_QUADS);
			float leftX = 1042;
			float topY = 524;
			float rightX = 1549;
			float botY = 1012;
			float maxX = 2048;
			float maxY = 1024;
			glTexCoord2f(leftX / maxX, (maxY - topY) / maxY);  glVertex2i(l, t - topInset); // Top left
			glTexCoord2f(rightX / maxX, (maxY - topY) / maxY);  glVertex2i(r, t - topInset); // Top right
			glTexCoord2f(rightX / maxX, (maxY - botY) / maxY);  glVertex2i(r, b);    // Bottom right
			glTexCoord2f(leftX / maxX, (maxY - botY) / maxY);  glVertex2i(l, b);    // Bottom left
			glEnd();
		}
		else if (in_window_id == g_window[3]) {
			// Extra EICAS display
			XPLMBindTexture2d(cockpit_texture_id, 0);
			glBegin(GL_QUADS);
			float leftX = 1558;
			float topY = 521;
			float rightX = 2041;
			float botY = 1013;
			float maxX = 2048;
			float maxY = 1024;
			glTexCoord2f(leftX / maxX, (maxY - topY) / maxY);  glVertex2i(l, t - topInset); // Top left
			glTexCoord2f(rightX / maxX, (maxY - topY) / maxY);  glVertex2i(r, t - topInset); // Top right
			glTexCoord2f(rightX / maxX, (maxY - botY) / maxY);  glVertex2i(r, b);    // Bottom right
			glTexCoord2f(leftX / maxX, (maxY - botY) / maxY);  glVertex2i(l, b);    // Bottom left
			glEnd();
		}
		else if (in_window_id == g_window[4]) {
			// Misc Display
			XPLMBindTexture2d(cockpit_texture_id, 0);
			glBegin(GL_QUADS);
			float leftX = 1557;
			float topY = 0;
			float rightX = 2046;
			float botY = 530;
			float maxX = 2048;
			float maxY = 1024;
			glTexCoord2f(leftX / maxX, (maxY - topY) / maxY);  glVertex2i(l, t - topInset); // Top left
			glTexCoord2f(rightX / maxX, (maxY - topY) / maxY);  glVertex2i(r, t - topInset); // Top right
			glTexCoord2f(rightX / maxX, (maxY - botY) / maxY);  glVertex2i(r, b);    // Bottom right
			glTexCoord2f(leftX / maxX, (maxY - botY) / maxY);  glVertex2i(l, b);    // Bottom left
			glEnd();
		}
		break;
	default:
		// Draw an X on each window for unknown aircraft
		// Note that it shows up as black since the texture units are active, but I don't want to change this
		float red[] = { 1.0, 0.0, 0.0, 1.0 };
		glColor4fv(red);
		glBegin(GL_LINES);
		glVertex2i(l, t - topInset); // Top left
		glVertex2i(r, b);            // Bottom right
		glVertex2i(r, t - topInset); // Top right
		glVertex2i(l, b);            // Bottom left
		glEnd();
		break;
	}
}

void clear_window_state() {
	char filename[256];
	sprintf(filename, "windowcockpit-%s.txt", cockpit_aircraft_name);
	log_printf("Removing window save file %s\n", filename);
	unlink(filename);
}

void save_window_state() {
	char filename[256];
	sprintf(filename, "windowcockpit-%s.txt", cockpit_aircraft_name);
	log_printf("Saving XTextureExtractor state to %s\n", filename);
	FILE *fp = fopen(filename, "wb");
	if (fp == NULL) {
		log_printf("Could not save to file %s\n", filename);
		return;
	}
	for (intptr_t i = 0; i < COCKPIT_MAX_WINDOWS; i++) {
		const int is_popped_out = XPLMWindowIsPoppedOut(g_window[i]);
		int l, t, r, b;
		if (is_popped_out)
			XPLMGetWindowGeometryOS(g_window[i], &l, &t, &r, &b);
		else
			XPLMGetWindowGeometry(g_window[i], &l, &t, &r, &b);
		fprintf(fp, "%d %d %d %d %d\n", is_popped_out, l, t, r, b);
		fflush(fp);
		log_printf("Writing to file Pop=%d L=%d T=%d R=%d B=%d\n", is_popped_out, l, t, r, b);
	}
	fclose(fp);

	// Used to provide feedback in the UI that we actually saved successfully, it can take multiple clicks to trigger properly
	cockpit_save_count++;
	sprintf(cockpit_save_string, "Sv%d", cockpit_save_count);
}

void load_window_state() {
	char filename[256];
	sprintf(filename, "windowcockpit-%s.txt", cockpit_aircraft_name);
	FILE *fp = fopen(filename, "rb");
	if (fp == NULL) {
		log_printf("Could not load from file %s\n", filename);
		return;
	} else {
		log_printf("Loading XTextureExtractor state from %s\n", filename);
	}
	for (intptr_t i = 0; i < COCKPIT_MAX_WINDOWS; i++) {
		int is_popped_out, l, t, r, b;
		if (fscanf(fp, "%d %d %d %d %d", &is_popped_out, &l, &t, &r, &b) != 5) {
			log_printf("Failed to read from file\n");
			break;
		}
		log_printf("Read from file Pop=%d L=%d T=%d R=%d B=%d\n", is_popped_out, l, t, r, b);
		XPLMSetWindowPositioningMode(g_window[i], is_popped_out ? xplm_WindowPopOut : xplm_WindowPositionFree, 0);
		if (is_popped_out)
			XPLMSetWindowGeometryOS(g_window[i], l, t, r, b);
		else
			XPLMSetWindowGeometry(g_window[i], l, t, r, b);
	}
	fclose(fp);
}


int	handle_mouse(XPLMWindowID in_window_id, int x, int y, XPLMMouseStatus is_down, void * in_refcon)
{
	if(is_down == xplm_MouseDown)
	{
		intptr_t win_num = (intptr_t)in_refcon;
		float *g_pop_button_lbrt     = &_g_pop_button_lbrt[win_num][0];
		float *g_texture_button_lbrt = &_g_texture_button_lbrt[win_num][0];
		float *g_scan_button_lbrt    = &_g_scan_button_lbrt[win_num][0];
		float *g_load_button_lbrt    = &_g_load_button_lbrt[win_num][0];
		float *g_save_button_lbrt    = &_g_save_button_lbrt[win_num][0];
		float *g_clear_button_lbrt   = &_g_clear_button_lbrt[win_num][0];
		float *g_dump_button_lbrt    = &_g_dump_button_lbrt[win_num][0];

		const int is_popped_out = XPLMWindowIsPoppedOut(in_window_id);
		if (!XPLMIsWindowInFront(in_window_id))
		{
			XPLMBringWindowToFront(in_window_id);
		}
		else if(coord_in_rect(x, y, g_pop_button_lbrt)) // user clicked the pop-in/pop-out button
		{
			XPLMSetWindowPositioningMode(in_window_id, is_popped_out ? xplm_WindowPositionFree : xplm_WindowPopOut, 0);
		}
		else if(coord_in_rect(x, y, g_texture_button_lbrt)) // user clicked the "texture info button" button
		{
			// Rescan from the top for the best texture, this usually works
			find_last_match_in_texture(-1);
		}
		else if (coord_in_rect(x, y, g_scan_button_lbrt)) {
			// Resume from the current texture, this is when the default algorithm fails but is very rare
			find_last_match_in_texture(cockpit_texture_id - 1);
		}
		else if (coord_in_rect(x, y, g_load_button_lbrt)) {
			load_window_state();
		}
		else if (coord_in_rect(x, y, g_save_button_lbrt)) {
			save_window_state();
		}
		else if (coord_in_rect(x, y, g_clear_button_lbrt)) {
			clear_window_state();
		}
		else if (coord_in_rect(x, y, g_dump_button_lbrt)) {
			dump_debug();
		}
		else
		{
			// log_printf("Ignored unknown click\n");
		}
	}
	return 1;
}
