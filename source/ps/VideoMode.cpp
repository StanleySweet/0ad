/* Copyright (C) 2010 Wildfire Games.
 * This file is part of 0 A.D.
 *
 * 0 A.D. is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * 0 A.D. is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with 0 A.D.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "precompiled.h"

#include "VideoMode.h"

#include "graphics/Camera.h"
#include "graphics/GameView.h"
#include "gui/GUIManager.h"
#include "lib/ogl.h"
#include "lib/external_libraries/sdl.h"
#include "lib/sysdep/gfx.h"
#include "ps/CConsole.h"
#include "ps/CLogger.h"
#include "ps/ConfigDB.h"
#include "ps/Game.h"
#include "ps/GameSetup/Config.h"
#include "renderer/Renderer.h"

static int DEFAULT_WINDOW_W = 1024;
static int DEFAULT_WINDOW_H = 768;

static int DEFAULT_FULLSCREEN_W = 1024;
static int DEFAULT_FULLSCREEN_H = 768;

CVideoMode g_VideoMode;

CVideoMode::CVideoMode() :
	m_IsInitialised(false),
	m_PreferredW(0), m_PreferredH(0), m_PreferredBPP(0), m_PreferredFreq(0),
	m_ConfigW(0), m_ConfigH(0), m_ConfigBPP(0), m_ConfigFullscreen(false),
	m_WindowedW(DEFAULT_WINDOW_W), m_WindowedH(DEFAULT_WINDOW_H)
{
	// (m_ConfigFullscreen defaults to false, so users don't get stuck if
	// e.g. half the filesystem is missing and the config files aren't loaded)
}

void CVideoMode::ReadConfig()
{
	bool windowed = !m_ConfigFullscreen;
	CFG_GET_USER_VAL("windowed", Bool, windowed);
	m_ConfigFullscreen = !windowed;

	CFG_GET_USER_VAL("xres", Int, m_ConfigW);
	CFG_GET_USER_VAL("yres", Int, m_ConfigH);
	CFG_GET_USER_VAL("bpp", Int, m_ConfigBPP);
}

bool CVideoMode::SetVideoMode(int w, int h, int bpp, bool fullscreen)
{
	Uint32 flags = SDL_OPENGL;
	if (fullscreen)
		flags |= SDL_FULLSCREEN;
	else
		flags |= SDL_RESIZABLE;

	if (!SDL_SetVideoMode(w, h, bpp, flags))
	{
		LOGERROR(L"SetVideoMode failed: %dx%d:%d %d (\"%hs\")", w, h, bpp, fullscreen ? 1 : 0, SDL_GetError());
		return false;
	}

	m_IsFullscreen = fullscreen;
	m_CurrentW = w;
	m_CurrentH = h;
	m_CurrentBPP = bpp;

	g_xres = w;
	g_yres = h;

	return true;
}

bool CVideoMode::InitSDL()
{
	debug_assert(!m_IsInitialised);

	ReadConfig();

	// preferred video mode = current desktop settings
	// (command line params may override these)
	gfx_get_video_mode(&m_PreferredW, &m_PreferredH, &m_PreferredBPP, &m_PreferredFreq);

	int w = m_ConfigW;
	int h = m_ConfigH;

	if (m_ConfigFullscreen)
	{
		// If fullscreen and no explicit size set, default to the desktop resolution
		if (w == 0 || h == 0)
		{
			w = m_PreferredW;
			h = m_PreferredH;
		}
	}

	// If no size determined, default to something sensible
	if (w == 0 || h == 0)
	{
		w = DEFAULT_WINDOW_W;
		h = DEFAULT_WINDOW_H;
	}

	int bpp = GetBestBPP();

	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_SWAP_CONTROL, g_VSync ? 1 : 0);

	if (!SetVideoMode(w, h, bpp, m_ConfigFullscreen))
	{
		// Fall back to a smaller depth buffer
		// (The rendering may be ugly but this helps when running in VMware)
		SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);

		if (!SetVideoMode(w, h, bpp, m_ConfigFullscreen))
			return false;
	}

	// Work around a bug in the proprietary Linux ATI driver (at least versions 8.16.20 and 8.14.13).
	// The driver appears to register its own atexit hook on context creation.
	// If this atexit hook is called before SDL_Quit destroys the OpenGL context,
	// some kind of double-free problem causes a crash and lockup in the driver.
	// Calling SDL_Quit twice appears to be harmless, though, and avoids the problem
	// by destroying the context *before* the driver's atexit hook is called.
	// (Note that atexit hooks are guaranteed to be called in reverse order of their registration.)
	atexit(SDL_Quit);
	// End work around.

	ogl_Init(); // required after each mode change
	// (TODO: does that mean we need to call this when toggling fullscreen later?)

	if (SDL_SetGamma(g_Gamma, g_Gamma, g_Gamma) < 0)
		LOGWARNING(L"SDL_SetGamma failed");

	m_IsInitialised = true;

	if (!m_ConfigFullscreen)
	{
		m_WindowedW = w;
		m_WindowedH = h;
	}

	return true;
}

bool CVideoMode::ResizeWindow(int w, int h)
{
	debug_assert(m_IsInitialised);

	// Ignore if not windowed
	if (m_IsFullscreen)
		return true;

	// Ignore if the size hasn't changed
	if (w == m_WindowedW && h == m_WindowedH)
		return true;

	int bpp = GetBestBPP();

	if (!SetVideoMode(w, h, bpp, false))
		return false;

	m_WindowedW = w;
	m_WindowedH = h;

	UpdateRenderer(w, h);

	return true;
}

bool CVideoMode::SetFullscreen(bool fullscreen)
{
	debug_assert(m_IsInitialised);

	// Check whether this is actually a change
	if (fullscreen == m_IsFullscreen)
		return true;

	if (!m_IsFullscreen)
	{
		// Windowed -> fullscreen:

		int w = 0, h = 0;

		// If a fullscreen size was configured, use that; else use the desktop size; else use a default
		if (m_ConfigFullscreen)
		{
			w = m_ConfigW;
			h = m_ConfigH;
		}
		if (w == 0 || h == 0)
		{
			w = m_PreferredW;
			h = m_PreferredH;
		}
		if (w == 0 || h == 0)
		{
			w = DEFAULT_FULLSCREEN_W;
			h = DEFAULT_FULLSCREEN_H;
		}

		int bpp = GetBestBPP();

		if (!SetVideoMode(w, h, bpp, fullscreen))
			return false;

		UpdateRenderer(w, h);

		return true;
	}
	else
	{
		// Fullscreen -> windowed:

		// Go back to whatever the previous window size was
		int w = m_WindowedW, h = m_WindowedH;

		int bpp = GetBestBPP();

		if (!SetVideoMode(w, h, bpp, fullscreen))
			return false;

		UpdateRenderer(w, h);

		return true;
	}
}

bool CVideoMode::ToggleFullscreen()
{
	return SetFullscreen(!m_IsFullscreen);
}

void CVideoMode::UpdateRenderer(int w, int h)
{
	if (w < 2) w = 2; // avoid GL errors caused by invalid sizes
	if (h < 2) h = 2;

	g_xres = w;
	g_yres = h;

	SViewPort vp = { 0, 0, w, h };

	g_Renderer.SetViewport(vp);
	g_Renderer.Resize(w, h);

	g_GUI->UpdateResolution();

	g_Console->UpdateScreenSize(w, h);

	if (g_Game)
		g_Game->GetView()->SetViewport(vp);
}

int CVideoMode::GetBestBPP()
{
	if (m_ConfigBPP)
		return m_ConfigBPP;
	if (m_PreferredBPP)
		return m_PreferredBPP;
	return 32;
}

int CVideoMode::GetXRes()
{
	debug_assert(m_IsInitialised);
	return m_CurrentW;
}

int CVideoMode::GetYRes()
{
	debug_assert(m_IsInitialised);
	return m_CurrentH;
}

int CVideoMode::GetBPP()
{
	debug_assert(m_IsInitialised);
	return m_CurrentBPP;
}
