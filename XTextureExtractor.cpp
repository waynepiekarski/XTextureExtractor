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


#include "XTextureExtractor.h"
#include <vector>
using namespace std;
#include "lodepng/lodepng.h"


// Set some arbitrary limit on the number of windows that can be supported
static XPLMWindowID	g_window[COCKPIT_MAX_WINDOWS];
XPLMDataRef gAcfTailnum = NULL;
GLint cockpit_texture_id = 0;
GLint cockpit_texture_width = -1;
GLint cockpit_texture_height = -1;
GLint cockpit_texture_format = -1;
int   cockpit_texture_seq = -1;     // Increment this each time we change aircraft or textures, so we can restart the network connection
bool  cockpit_dirty = false;
bool  cockpit_aircraft_known = false;
char  cockpit_aircraft_name[256];
char  cockpit_aircraft_filename[256];
char  plugin_path[MAX_PATH];
int   cockpit_save_count = 0;
char  cockpit_save_string[32];
int   cockpit_window_limit = 0;


void detect_aircraft_filename(void) {
	char filename[256];
	char path[256];
	char tailnum[256];
	XPLMGetNthAircraftModel(XPLM_USER_AIRCRAFT, filename, path);
	_strlwr(filename);
	_strlwr(path);

	int result;
	if (gAcfTailnum == NULL) {
		strcpy(tailnum, "(null)");
	}
	else {
		result = XPLMGetDatab(gAcfTailnum, tailnum, 0, 256);
		if (result < 0) {
			strcpy(tailnum, "(error)");
		}
		else {
			// Need to terminate the strings and convert to lower case
			tailnum[result] = '\0';
			_strlwr(tailnum);
		}
	}

	if (strstr(tailnum, "zb73")) {
		strcpy(cockpit_aircraft_filename, "zibo-b738.acf");
		log_printf("Found ZiboMod 738 or Ultimate 739 based on acf_tailnum [%s], changing file name to [%s]\n", tailnum, cockpit_aircraft_filename);
	}
	else {
		strcpy(cockpit_aircraft_filename, filename);
		log_printf("Found aircraft file name [%s]\n", cockpit_aircraft_filename);
	}
}


int network_started = false;


GLint scan_candidate_texid = -1;

int draw_callback(XPLMDrawingPhase inPhase, int inIsBefore, void *inRefcon) {
	GLint bound_texture;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound_texture);
	// log_printf("draw_callback: phase=%d, before=%d, texid=%d, refcon=%p\n", inPhase, inIsBefore, bound_texture, inRefcon);
	// Phase information from http://www.xsquawkbox.net/xpsdk/mediawiki/XPLMDrawingPhase
	if ((inPhase == 40) && (inIsBefore == 0)) { // xplm_Phase_Panel=40
												// Phase 40 happens many times in a render pass, so always keep the latest value, this is not the texture
												// value but happens to always be one off from the correct value.
		scan_candidate_texid = bound_texture;
		log_printf("Candidate texture id = %d\n", scan_candidate_texid);
	}
	else if ((inPhase == 0) && (inIsBefore == 0) && (scan_candidate_texid > 0)) { // xplm_Phase_FirstScene=0
		// Phase 0 implies the last render has finished, so the last texture kept is the true texture id but off by one.
		// Note that we only enter this code once we have received at least one phase 40 callback!
		cockpit_texture_id = scan_candidate_texid + 1;
		log_printf("Found panel texture id +1 = %d\n", cockpit_texture_id);

		// Stop receiving phase callbacks
		XPLMUnregisterDrawCallback(draw_callback, 0, 0, NULL);
		XPLMUnregisterDrawCallback(draw_callback, 40, 0, NULL);
		
		// Tell the networking code that the texture has been changed
		cockpit_texture_seq++;
		if (!network_started) {
			log_printf("Texture is found, starting network thread to listen for XTextureExtractor clients\n");
			start_networking_thread();
			network_started = true;
		}

		// Double check this is correct
		int tw, th, tf;
		XPLMBindTexture2d(cockpit_texture_id, 0);
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tw);
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &th);
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &tf);
		if ((tw != cockpit_texture_width) || (th != cockpit_texture_height) || (tf != cockpit_texture_format)) {
			log_printf("Error! Mismatch texture id=%d, width=%d!=%d, height=%d!=%d, internal format=%d!=%d\n", cockpit_texture_id, tw, cockpit_texture_width, th, cockpit_texture_height, tf, cockpit_texture_format);
		}
	}
	return 1; // Allow X-Plane to keep drawing
}


static void scan_for_texture(void)
{
	// Register callbacks for drawing phase 40 and 0, so we can grab the texture id.
	// We will get a number of 40s, and then a 0 to indicate the next render.
	scan_candidate_texid = -1;
	XPLMRegisterDrawCallback(draw_callback,  0, 0, NULL);
	XPLMRegisterDrawCallback(draw_callback, 40, 0, NULL);
	log_printf("Scheduling texture scan on next rendering pass\n");
}


void load_window_state();
void				draw(XPLMWindowID in_window_id, void * in_refcon);
int					handle_mouse(XPLMWindowID in_window_id, int x, int y, int is_down, void * in_refcon);
int                 handle_command(XPLMCommandRef cmd_id, XPLMCommandPhase phase, void * in_refcon);

int					dummy_mouse_handler(XPLMWindowID in_window_id, int x, int y, int is_down, void * in_refcon) { return 0; }
XPLMCursorStatus	dummy_cursor_status_handler(XPLMWindowID in_window_id, int x, int y, void * in_refcon) { return xplm_CursorDefault; }
int					dummy_wheel_handler(XPLMWindowID in_window_id, int x, int y, int wheel, int clicks, void * in_refcon) { return 0; }
void				dummy_key_handler(XPLMWindowID in_window_id, char key, XPLMKeyFlags flags, char virtual_key, void * in_refcon, int losing_focus) { }

static int _g_pop_button_lbrt[COCKPIT_MAX_WINDOWS][4]; // left, bottom, right, top
static int _g_texture_button_lbrt[COCKPIT_MAX_WINDOWS][4]; // left, bottom, right, top
static int _g_load_button_lbrt[COCKPIT_MAX_WINDOWS][4]; // left, bottom, right, top
static int _g_save_button_lbrt[COCKPIT_MAX_WINDOWS][4]; // left, bottom, right, top
static int _g_clear_button_lbrt[COCKPIT_MAX_WINDOWS][4]; // left, bottom, right, top
static int _g_hide_button_lbrt[COCKPIT_MAX_WINDOWS][4]; // left, bottom, right, top
static int _g_dump_button_lbrt[COCKPIT_MAX_WINDOWS][4]; // left, bottom, right, top

XPLMCommandRef cmd_texture_button = NULL;
XPLMCommandRef cmd_load_button = NULL;
XPLMCommandRef cmd_save_button = NULL;
XPLMCommandRef cmd_clear_button = NULL;
XPLMCommandRef cmd_hide_button = NULL;
XPLMCommandRef cmd_dump_button = NULL;
XPLMCommandRef cmd_next_button = NULL;
XPLMCommandRef cmd_prev_button = NULL;

char _g_window_name[COCKPIT_MAX_WINDOWS][256];  // titles of each window
int _g_texture_lbrt[COCKPIT_MAX_WINDOWS][4]; // left, bottom, right, top

bool decorateWindows = true;

static int	coord_in_rect(int x, int y, int * bounds_lbrt)  { return ((x >= bounds_lbrt[0]) && (x < bounds_lbrt[2]) && (y < bounds_lbrt[3]) && (y >= bounds_lbrt[1])); }

#undef DEBUG_KEYPRESS
// Activate this code when you need to have a key event control a variable
#ifdef DEBUG_KEYPRESS
int debug_key_state = 0;
void process_debug_key(void *refCon) {
	debug_key_state = 1 - debug_key_state;
	log_printf("Debug key press detected, change to %d state\n", debug_key_state);
}
#endif


PLUGIN_API int XPluginStart(
						char *		outName,
						char *		outSig,
						char *		outDesc)
{
	XPLMGetPluginInfo(XPLMGetMyID(), NULL, plugin_path, NULL, NULL);
	char *slash = strrchr(plugin_path, '\\'); // Chop off the filename so we can get the path
	if (slash != NULL) {
		*slash = '\0';
	} else {
		log_printf("The XPLMGetPluginInfo returned did not contain \\64\\win.xpl as expected [%s]\n", plugin_path);
	}
	strcat(plugin_path, "\\..\\");

	strcpy(outName, "XTextureExtractorPlugin");
	strcpy(outSig, "net.waynepiekarski.windowcockpitplugin");
	sprintf(outDesc, "%s - Extracts out cockpit textures into a separate window - compiled %s %s", TCP_PLUGIN_VERSION, __DATE__, __TIME__);
	log_printf("XPluginStart: XTextureExtractor plugin - %s - path %s\n", outDesc, plugin_path);

	// Register to listen for aircraft notes information, so we can detect Zibo 738 later
	gAcfTailnum = XPLMFindDataRef("sim/aircraft/view/acf_tailnum");

	// Global save button that increments each time
	strcpy(cockpit_save_string, "Sv");

	// Implement a debugging key if we need it
#ifdef DEBUG_KEYPRESS
#pragma message("DEBUG_KEYPRESS enabled")
	XPLMRegisterHotKey(XPLM_VK_F8, xplm_DownFlag, "F8", process_debug_key, NULL);
#endif

	// Implement commands for all the buttons we support
	XPLMRegisterCommandHandler(cmd_texture_button = XPLMCreateCommand("XTE/newscan", "XTextureExtractor New Scan"), handle_command, 1, "New Scan");
	XPLMRegisterCommandHandler(cmd_load_button  = XPLMCreateCommand("XTE/load", "XTextureExtractor Load"),  handle_command, 1, "Load");
	XPLMRegisterCommandHandler(cmd_save_button  = XPLMCreateCommand("XTE/save", "XTextureExtractor Save"),  handle_command, 1, "Save");
	XPLMRegisterCommandHandler(cmd_clear_button = XPLMCreateCommand("XTE/clear", "XTextureExtractor Clear"), handle_command, 1, "Clear");
	XPLMRegisterCommandHandler(cmd_hide_button  = XPLMCreateCommand("XTE/hide", "XTextureExtractor Hide"),  handle_command, 1, "Hide");
	XPLMRegisterCommandHandler(cmd_dump_button  = XPLMCreateCommand("XTE/dump", "XTextureExtractor Dump"),  handle_command, 1, "Dump");
	XPLMRegisterCommandHandler(cmd_next_button = XPLMCreateCommand("XTE/next", "XTextureExtractor Next"), handle_command, 1, "Next");
	XPLMRegisterCommandHandler(cmd_prev_button = XPLMCreateCommand("XTE/prev", "XTextureExtractor Prev"), handle_command, 1, "Prev");

	return 1;
}

PLUGIN_API void	XPluginStop(void)
{
	log_printf("XPluginStop: XTextureExtractor plugin\n");

	// Destroy the windows if they exist
	for (int i = 0; i < COCKPIT_MAX_WINDOWS; i++) {
		if (g_window[i] != NULL) {
			XPLMDestroyWindow(g_window[i]);
			g_window[i] = NULL;
		}
	}

	if (cmd_texture_button != NULL) XPLMUnregisterCommandHandler(cmd_texture_button, handle_command, 0, 0); cmd_texture_button = NULL;
	if (cmd_load_button != NULL) XPLMUnregisterCommandHandler(cmd_load_button, handle_command, 0, 0); cmd_load_button = NULL;
	if (cmd_save_button != NULL) XPLMUnregisterCommandHandler(cmd_save_button, handle_command, 0, 0); cmd_save_button = NULL;
	if (cmd_clear_button != NULL) XPLMUnregisterCommandHandler(cmd_clear_button, handle_command, 0, 0); cmd_clear_button = NULL;
	if (cmd_hide_button != NULL) XPLMUnregisterCommandHandler(cmd_hide_button, handle_command, 0, 0); cmd_hide_button = NULL;
	if (cmd_dump_button != NULL) XPLMUnregisterCommandHandler(cmd_dump_button, handle_command, 0, 0); cmd_dump_button = NULL;
	if (cmd_next_button != NULL) XPLMUnregisterCommandHandler(cmd_next_button, handle_command, 0, 0); cmd_next_button = NULL;
	if (cmd_prev_button != NULL) XPLMUnregisterCommandHandler(cmd_prev_button, handle_command, 0, 0); cmd_prev_button = NULL;
}

bool plugin_disabled = false;
PLUGIN_API void XPluginDisable(void) {
	log_printf("XPluginDisable\n");
	plugin_disabled = true;
	XPluginStop();
}

PLUGIN_API int XPluginEnable(void) {
	if (plugin_disabled) {
		log_printf("Plugin was previously disabled, re-enabling it\n");
		plugin_disabled = false;
		cockpit_dirty = true;
		load_window_state();
	}
	return 1;
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID inFrom, int inMsg, void * inParam)
{
	// log_printf("XPluginReceiveMessage XTextureExtractor: inFrom=%d, inMsg=%d, inParam=%p\n", inFrom, inMsg, inParam);
	if (inMsg == 103) {
		// Seems like 103 is sent just when the aircraft is finished loading, we can try grabbing the texture here
		log_printf("XPluginReceiveMessage: Found event inMsg=103, aircraft is loaded, lets try to load window properties\n");
		cockpit_dirty = true;
		load_window_state();
	}
}


void save_png(GLint texId)
{
	int tw, th, tf;
	XPLMBindTexture2d(texId, 0);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tw);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &th);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &tf);

	// Read the entire texture into a buffer
	unsigned char * pixels = new unsigned char[tw * th * 4];
	glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

	// Flip the texture so it is upright when we write to the PNG file
	unsigned char * flipped = new unsigned char[tw * th * 4];
	int in_stride = tw * 4;
	int out_stride = tw * 4;
	int out_rows = th;
	unsigned char *src = pixels + ((th - 1) * in_stride);
	unsigned char *dest = flipped;
	for (int r = 0; r < out_rows; r++) {
		memcpy(dest, src, out_stride);
		src -= in_stride;
		dest += out_stride;
	}

	// Write out the RGBA image as an RGB image with lodepng
	std::vector<unsigned char> png;
	lodepng::State state;
	state.info_raw.colortype = LCT_RGBA; // Input type
	state.info_raw.bitdepth = 8;
	state.info_png.color.colortype = LCT_RGB; // Output type
	state.info_png.color.bitdepth = 8;
	state.encoder.auto_convert = 0; // Must provide this or will ignore the input/output types
	unsigned error = lodepng::encode(png, flipped, tw, th, state);

	FILE *fp = fopen("texture_save.png", "wb");
	if (fp == NULL) {
		log_printf("Could not save to file\n");
		delete[] pixels;
		delete[] flipped;
		return;
	}
	fwrite(png.data(), sizeof(unsigned char), png.size(), fp);
	delete[] pixels;
	delete[] flipped;
	if (fclose(fp) == 0) {
		log_printf("PNG save of %zu bytes is complete to file texture_save.png in X-Plane main directory\n", png.size());
	}
	else {
		log_printf("Failed to save PNG file\n");
	}
}

void dump_debug()
{
	log_printf("Dumping out texture for id %d\n", cockpit_texture_id);
	save_png(cockpit_texture_id);
}


void draw_texture_rect(int leftX, int topY, int rightX, int botY, int maxX, int maxY, int l, int t, int r, int b) {
	XPLMBindTexture2d(cockpit_texture_id, 0);
	glBegin(GL_QUADS);
	glTexCoord2f( leftX / (float)maxX, (maxY - topY) / (float)maxY);  glVertex2i(l, t); // Top left
	glTexCoord2f(rightX / (float)maxX, (maxY - topY) / (float)maxY);  glVertex2i(r, t); // Top right
	glTexCoord2f(rightX / (float)maxX, (maxY - botY) / (float)maxY);  glVertex2i(r, b); // Bottom right
	glTexCoord2f( leftX / (float)maxX, (maxY - botY) / (float)maxY);  glVertex2i(l, b); // Bottom left
	glEnd();
}


unsigned char texture_buffer[MAX_TEXTURE_WIDTH * MAX_TEXTURE_HEIGHT * 4]; // Ensure we definitely have enough space for 4-byte RGBA
unsigned char *texture_pointer = NULL; // When this is null, the image has been sent and we need to capture a new one


void draw(XPLMWindowID in_window_id, void * in_refcon)
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
	int *g_pop_button_lbrt = &_g_pop_button_lbrt[win_num][0];
	int *g_texture_button_lbrt = &_g_texture_button_lbrt[win_num][0];
	int *g_save_button_lbrt = &_g_save_button_lbrt[win_num][0];
	int *g_load_button_lbrt = &_g_load_button_lbrt[win_num][0];
	int *g_clear_button_lbrt = &_g_clear_button_lbrt[win_num][0];
	int *g_hide_button_lbrt = &_g_hide_button_lbrt[win_num][0];
	int *g_dump_button_lbrt = &_g_dump_button_lbrt[win_num][0];

	// Draw our buttons
	if (decorateWindows)
	{
		// Position the pop-in/pop-out button in the upper left of the window
		g_pop_button_lbrt[0] = l + 0;
		g_pop_button_lbrt[3] = t - 0;
		g_pop_button_lbrt[2] = g_pop_button_lbrt[0] + (int)XPLMMeasureString(xplmFont_Proportional, pop_label, (int)strlen(pop_label)); // *just* wide enough to fit the button text
		g_pop_button_lbrt[1] = g_pop_button_lbrt[3] - (int)(1.25f * char_height); // a bit taller than the button text
		
		// Position the "move to lower left" button just to the right of the pop-in/pop-out button
		char texture_info_text[128];
		sprintf(texture_info_text, "GL%d [%s]", cockpit_texture_id, cockpit_aircraft_name);

#define DEFINE_BOX(_array, _left, _string) _array[0] = _left[2] + 10, _array[1] = _left[1], _array[2] = _array[0] + (int)XPLMMeasureString(xplmFont_Proportional, _string, (int)strlen(_string)), _array[3] = _left[3]
		DEFINE_BOX(g_texture_button_lbrt, g_pop_button_lbrt, texture_info_text);
		DEFINE_BOX(g_load_button_lbrt, g_texture_button_lbrt, "Ld");
		DEFINE_BOX(g_save_button_lbrt, g_load_button_lbrt, cockpit_save_string);
		DEFINE_BOX(g_clear_button_lbrt, g_save_button_lbrt, "Clr");
		DEFINE_BOX(g_hide_button_lbrt, g_clear_button_lbrt, "H");
		DEFINE_BOX(g_dump_button_lbrt, g_hide_button_lbrt, "Dbg");
#undef DEFINE_BOX

		// Draw the boxes around our rudimentary buttons
		float green[] = {0.0, 1.0, 0.0, 1.0};
		glColor4fv(green);
#define DRAW_BOX(_array) glBegin(GL_LINE_LOOP), glVertex2i(_array[0], _array[3]), glVertex2i(_array[2], _array[3]), glVertex2i(_array[2], _array[1]), glVertex2i(_array[0], _array[1]), glEnd()
		DRAW_BOX(g_pop_button_lbrt);
		DRAW_BOX(g_texture_button_lbrt);
		DRAW_BOX(g_load_button_lbrt);
		DRAW_BOX(g_save_button_lbrt);
		DRAW_BOX(g_clear_button_lbrt);
		DRAW_BOX(g_hide_button_lbrt);
		DRAW_BOX(g_dump_button_lbrt);
#undef DRAW_BOX

		// Draw the button text (pop in/pop out)
		XPLMDrawString(col_white, g_pop_button_lbrt[0], g_pop_button_lbrt[1] + 4, (char *)pop_label, NULL, xplmFont_Proportional);

		// Draw the button text (texture info)
		XPLMDrawString(col_white, g_texture_button_lbrt[0], g_texture_button_lbrt[1] + 4, (char *)texture_info_text, NULL, xplmFont_Proportional);

		// Draw the load/save/clear text
		XPLMDrawString(col_white, g_load_button_lbrt[0],  g_load_button_lbrt[1]  + 4, (char *)"Ld", NULL, xplmFont_Proportional);
		XPLMDrawString(col_white, g_save_button_lbrt[0],  g_save_button_lbrt[1]  + 4, (char *)cockpit_save_string, NULL, xplmFont_Proportional);
		XPLMDrawString(col_white, g_clear_button_lbrt[0], g_clear_button_lbrt[1] + 4, (char *)"Clr", NULL, xplmFont_Proportional);
		XPLMDrawString(col_white, g_hide_button_lbrt[0],  g_hide_button_lbrt[1] + 4, (char *)"H", NULL, xplmFont_Proportional);
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
		log_printf("Detected aircraft dirty flag set, so finding texture\n");
		scan_for_texture();
		cockpit_dirty = false;
	}

	int *g_texture_lbrt = &_g_texture_lbrt[win_num][0];
	int topInset = 20;
	int sideInset = 0;
	if (!decorateWindows) {
		topInset = -40;
		sideInset = 10;
	}

	if (cockpit_aircraft_known) {
		// Draw the texture to the window
		draw_texture_rect(g_texture_lbrt[0], g_texture_lbrt[1], g_texture_lbrt[2], g_texture_lbrt[3], cockpit_texture_width, cockpit_texture_height, l - sideInset, t - topInset, r + sideInset, b - sideInset);

		// Check to see if we need to prepare a new texture image to send, only capture a new one if the previous has been consumed
		if (texture_pointer == NULL) {
			// GL_RGBA is the fastest (52 -> 37 fps), then GL_RGB (52 -> 31 fps), and GL_BGR_EXT is the slowest (52 -> 28 fps).
			// glTexSubImage2D doesn't seem to work, always returns a black image
			// glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, cockpit_texture_width, cockpit_texture_height, GL_RGBA, GL_UNSIGNED_BYTE, texture_buffer);
			glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, texture_buffer);

			// Image is now captured, so set the pointer and we wait for the network thread to compress and send it
			texture_pointer = texture_buffer;
			// log_printf("Captured texture buffer, ready for transmission\n");
		}
	} else {
		// Draw an X on the window for unknown aircraft
		// Note that it shows up as black since the texture units are active, but I don't want to change this
		float red[] = { 1.0, 0.0, 0.0, 1.0 };
		glColor4fv(red);
		glBegin(GL_LINES);
		glVertex2i(l, t - topInset); // Top left
		glVertex2i(r, b);            // Bottom right
		glVertex2i(r, t - topInset); // Top right
		glVertex2i(l, b);            // Bottom left
		glEnd();
	}
}

void clear_window_state() {
	char filename[256];
	sprintf(filename, "windowcockpit-%s.txt", cockpit_aircraft_name);
	log_printf("Removing window save file %s\n", filename);
	_unlink(filename);
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
	for (intptr_t i = 0; i < cockpit_window_limit; i++) {
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

	// Destroy the windows if they exist
	for (int i = 0; i < COCKPIT_MAX_WINDOWS; i++) {
		if (g_window[i] != NULL) {
			XPLMDestroyWindow(g_window[i]);
			g_window[i] = NULL;
		}
	}

	// Detect the type of aircraft and load in the name to cockpit_aircraft_filename
	detect_aircraft_filename();

	// Read in the new aircraft configuration file
	cockpit_window_limit = 0;
	cockpit_aircraft_known = false;
	char texturefile[256];
	char buffer[1024];
	sprintf(texturefile, "%s\\%s.tex", plugin_path, cockpit_aircraft_filename);
	FILE *fp = fopen(texturefile, "rb");
	if (fp == NULL) {
		log_printf("Could not load texture data from file %s, this aircraft is unknown\n", texturefile);
		sprintf(texturefile, "%s\\%s.tex", plugin_path, "unknown");
		fp = fopen(texturefile, "rb");
		if (fp == NULL) {
			log_printf("Could not open generic data file %s, giving up\n", texturefile);
			return;
		}
	}
	log_printf("Loading XTextureExtractor state from %s\n", texturefile);

	char *result = fgets(buffer, 1024, fp);
	if (result == NULL) {
		log_printf("Failed to read first line from file, aborting reading\n");
		fclose(fp);
		return;
	}
	if (sscanf(buffer, "%s %d %d %d", cockpit_aircraft_name, &cockpit_texture_width, &cockpit_texture_height, &cockpit_texture_format) != 4) {
		log_printf("Failed to read texture description from file, aborting reading\n");
		fclose(fp);
		return;
	} else {
		log_printf("Read in [%s] = max(%d,%d) format(%d)\n", cockpit_aircraft_name, cockpit_texture_width, cockpit_texture_height, cockpit_texture_format);
	}
	if ((cockpit_texture_width < 0 || cockpit_texture_width > MAX_TEXTURE_WIDTH || cockpit_texture_height < 0 || cockpit_texture_height > MAX_TEXTURE_HEIGHT)) {
		log_printf("Read texture dimensions %dx%d is out of bounds 0x0..%dx%d\n", cockpit_texture_width, cockpit_texture_height, MAX_TEXTURE_WIDTH, MAX_TEXTURE_HEIGHT);
		fclose(fp);
		return;
	}
	cockpit_aircraft_known = true;

	int i = 0;
	while (i < COCKPIT_MAX_WINDOWS) {
		char *result = fgets(buffer, 1024, fp);
		if (result == NULL) {
			log_printf("fgets returned NULL, finished reading with %d successful textures\n", cockpit_window_limit);
			break;
		}
		if (result[0] == '#') {
			// Skip commented out line
			log_printf("Skipping line [%s]\n", buffer);
			continue;
		}
		
		if (sscanf(buffer, "%s %d %d %d %d", _g_window_name[i], &_g_texture_lbrt[i][0], &_g_texture_lbrt[i][1], &_g_texture_lbrt[i][2], &_g_texture_lbrt[i][3]) != 5) {
			log_printf("Reached failed sscanf read [%s], finished reading with %d successful textures\n", buffer, cockpit_window_limit);
			break;
		}
		log_printf("Read in %d = [%s] LBRT=%d, %d, %d, %d\n", i, _g_window_name[i], _g_texture_lbrt[i][0], _g_texture_lbrt[i][1], _g_texture_lbrt[i][2], _g_texture_lbrt[i][3]);
		cockpit_window_limit = i + 1;
		i++;
	}
	fclose(fp);

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
	params.decorateAsFloatingWindow = decorateWindows;

	// Open windows in reverse order so the most important are at the top
	for (intptr_t i = cockpit_window_limit - 1; i >= 0; i--) {
		params.refcon = (void *)i; // Store the window id
		params.left += 20; // Stagger the windows
		params.bottom -= 20;
		params.right += 20;
		params.top -= 20;
		g_window[i] = XPLMCreateWindowEx(&params);
		XPLMSetWindowPositioningMode(g_window[i], xplm_WindowPositionFree, -1);
		XPLMSetWindowGravity(g_window[i], 0, 1, 0, 1); // As the X-Plane window resizes, keep our size constant, and our left and top edges in the same place relative to the window's left/top
													   // XPLMSetWindowResizingLimits(g_window[i], 200, 200, 1000, 1000); // Limit resizing our window: maintain a minimum width/height of 200 boxels and a max width/height of 500
		char winname[1024];
		sprintf(winname, "XTextureExtractor: %s", _g_window_name[i]);
		XPLMSetWindowTitle(g_window[i], winname);
	}


	// Configure the window based on a local configuration file (if present)
	char filename[256];
	sprintf(filename, "windowcockpit-%s.txt", cockpit_aircraft_name);
	fp = fopen(filename, "rb");
	if (fp == NULL) {
		log_printf("Could not load from file %s\n", filename);
		return;
	} else {
		log_printf("Loading XTextureExtractor state from %s\n", filename);
	}
	for (intptr_t i = 0; i < COCKPIT_MAX_WINDOWS; i++) {
		int is_popped_out, l, t, r, b;
		if (fscanf(fp, "%d %d %d %d %d", &is_popped_out, &l, &t, &r, &b) != 5) {
			log_printf("Reached EOF from file %s\n", filename);
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
		const int is_popped_out = XPLMWindowIsPoppedOut(in_window_id);
		if (!XPLMIsWindowInFront(in_window_id))
		{
			XPLMBringWindowToFront(in_window_id);
		}

		if(coord_in_rect(x, y, _g_pop_button_lbrt[win_num])) // user clicked the pop-in/pop-out button
		{
			XPLMSetWindowPositioningMode(in_window_id, is_popped_out ? xplm_WindowPositionFree : xplm_WindowPopOut, 0);
		}
		else if(coord_in_rect(x, y, _g_texture_button_lbrt[win_num])) // user clicked the "texture info button" button
		{
			// Shouldn't ever need to manually initiate a scan
			scan_for_texture();
		}
		else if (coord_in_rect(x, y, _g_load_button_lbrt[win_num])) {
			load_window_state();
		}
		else if (coord_in_rect(x, y, _g_save_button_lbrt[win_num])) {
			save_window_state();
		}
		else if (coord_in_rect(x, y, _g_clear_button_lbrt[win_num])) {
			clear_window_state();
		}
		else if (coord_in_rect(x, y, _g_hide_button_lbrt[win_num])) {
			decorateWindows = !decorateWindows;
			log_printf("Inverting window decorations to %d\n", decorateWindows);
			load_window_state();
		}
		else if (coord_in_rect(x, y, _g_dump_button_lbrt[win_num])) {
			dump_debug();
		}
		else {
			// Make the whole window clickable if the buttons are hidden
			if (!decorateWindows) {
				decorateWindows = !decorateWindows;
				log_printf("Inverting window decorations to %d\n", decorateWindows);
				load_window_state();
			}
		}
	}
	return 1;
}

int handle_command(XPLMCommandRef cmd_id, XPLMCommandPhase phase, void * in_refcon)
{
	// Only do the command when it is being released
	if (phase == xplm_CommandEnd)
	{
		log_printf("Found incoming command %p with reference [%s]\n", cmd_id, (char *)in_refcon);
		if (cmd_id == cmd_texture_button) {
			// Shouldn't ever need to manually initiate a scan
			scan_for_texture();
		}
		else if (cmd_id == cmd_load_button) {
			load_window_state();
		}
		else if (cmd_id == cmd_save_button) {
			save_window_state();
		}
		else if (cmd_id == cmd_clear_button) {
			clear_window_state();
		}
		else if (cmd_id == cmd_hide_button) {
			decorateWindows = !decorateWindows;
			log_printf("Inverting window decorations to %d\n", decorateWindows);
			load_window_state();
		}
		else if (cmd_id == cmd_dump_button) {
			dump_debug();
		}
		else if (cmd_id == cmd_next_button) {
			cockpit_texture_id++;
			log_printf("Set cockpit_texture_id to %d\n", cockpit_texture_id);
		}
		else if (cmd_id == cmd_prev_button) {
			cockpit_texture_id--;
			if (cockpit_texture_id < 0) {
				cockpit_texture_id = 0;
			}
			log_printf("Set cockpit_texture_id to %d\n", cockpit_texture_id);
		}
		else {
			log_printf("Ignoring unknown command\n");
		}
	}
	return 1;
}
