/*  dssi-vst: a DSSI plugin wrapper for VST effects and instruments
    Copyright 2004-2007 Chris Cannam

    This file is part of linvst.

    linvst is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "pluginterfaces/vst2.x/aeffectx.h"

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <map>

#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <sys/un.h>
#include <sys/time.h>
#include <sys/poll.h>
#include <time.h>

#include <sched.h>

#define WIN32_LEAN_AND_MEAN

#include <windows.h>

#include "remotepluginserver.h"

#include "paths.h"

#define APPLICATION_CLASS_NAME "dssi_vst"
#define OLD_PLUGIN_ENTRY_POINT "main"
#define NEW_PLUGIN_ENTRY_POINT "VSTPluginMain"

#if VST_FORCE_DEPRECATED
#define DEPRECATED_VST_SYMBOL(x) __##x##Deprecated
#else
#define DEPRECATED_VST_SYMBOL(x) x
#endif

typedef AEffect *(VSTCALLBACK *VstEntry)(audioMasterCallback audioMaster);

HANDLE  ThreadHandle[3] = {0,0,0};

bool    exiting = false;
bool    inProcessThread = false;
bool    alive = false;
bool    threadrun = false;
bool    plugok = false;
int     bufferSize = 0;
int     sampleRate = 0;
bool    guiVisible = false;
int     parfin = 0;
int     audfin = 0;
int     getfin = 0;

RemotePluginDebugLevel  debugLevel = RemotePluginDebugNone;

using namespace std;

class RemoteVSTServer : public RemotePluginServer
{
public:
                        RemoteVSTServer(std::string fileIdentifiers, AEffect *plugin, std::string fallbackName);
    virtual             ~RemoteVSTServer();

    virtual std::string getName() { return m_name; }
    virtual std::string getMaker() { return m_maker; }
    virtual void        setBufferSize(int);
    virtual void        setSampleRate(int);
    virtual void        reset();
    virtual void        terminate();

    virtual int         getInputCount() { return m_plugin->numInputs; }
    virtual int         getOutputCount() { return m_plugin->numOutputs; }
    virtual int         getFlags() { return m_plugin->flags; }
    virtual int         getinitialDelay() { return m_plugin->initialDelay; }
    virtual int         getUID() { return m_plugin->uniqueID; }
    virtual int         getParameterCount() { return m_plugin->numParams; }
    virtual std::string getParameterName(int);
    virtual void        setParameter(int, float);
    virtual float       getParameter(int);
    virtual void        getParameters(int, int, float *);

    virtual int         getProgramCount() { return m_plugin->numPrograms; }
    virtual std::string getProgramNameIndexed(int);
    virtual std::string getProgramName();
    virtual void        setCurrentProgram(int);

    virtual void        showGUI();
    virtual void        hideGUI();

#ifdef EMBED
    virtual void        openGUI();
#endif

    virtual int         getEffInt(int opcode);
    virtual std::string getEffString(int opcode, int index);
    virtual void        effDoVoid(int opcode);

//    virtual int         getInitialDelay() {return m_plugin->initialDelay;}
//    virtual int         getUniqueID() { return m_plugin->uniqueID;}
//    virtual int         getVersion() { return m_plugin->version;}

    virtual int         processVstEvents();
    virtual void        getChunk();
    virtual void        setChunk();
//    virtual void        canBeAutomated();
    virtual void        getProgram();
    virtual void        EffectOpen();
//    virtual void        eff_mainsChanged(int v);

    virtual void        process(float **inputs, float **outputs, int sampleFrames);

    virtual void        setDebugLevel(RemotePluginDebugLevel level) { debugLevel = level; }

    virtual bool        warn(std::string);

    virtual void        waitForServer();
    virtual void        waitForServerexit();

    HWND                hWnd;
    WNDCLASSEX          wclass;
    UINT_PTR            timerval;
    bool                haveGui;

#ifdef EMBED
    HANDLE handlewin;

    struct winmessage
    {
        int handle;
        int width;
        int height;
    } winm;
#ifdef EMBEDRESIZE
    int guiupdate;
    int guiupdatecount;
    int guiresizewidth;
    int guiresizeheight;
#endif
#endif
    ERect               *rect;

    int                 setprogrammiss;
    int                 hostreaper;

    AEffect             *m_plugin;
    VstEvents           vstev[VSTSIZE];

private:
    std::string         m_name;
    std::string         m_maker;
};

RemoteVSTServer         *remoteVSTServerInstance = 0;


LRESULT WINAPI MainProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CLOSE:
#ifndef EMBED
         if (!exiting && guiVisible)
         remoteVSTServerInstance->hideGUI();
#endif
    break;
	
    default:
    return DefWindowProc(hWnd, msg, wParam, lParam);		   
    }
 return 0;
}

DWORD WINAPI AudioThreadMain(LPVOID parameter)
{
/*
    struct sched_param param;
    param.sched_priority = 1;

    int result = sched_setscheduler(0, SCHED_FIFO, &param);

    if (result < 0)
    {
        perror("Failed to set realtime priority for audio thread");
    }
*/
    while (!exiting)
    {
        if ((alive == true) && (threadrun == true))
        {
                remoteVSTServerInstance->dispatchProcess(50);
         }
        else
            usleep(10000);
    }
    // param.sched_priority = 0;
    // (void)sched_setscheduler(0, SCHED_OTHER, &param);
    audfin = 1;
    ExitThread(0);
    return 0;
}

DWORD WINAPI GetSetThreadMain(LPVOID parameter)
{
/*
    struct sched_param param;
    param.sched_priority = 1;

    int result = sched_setscheduler(0, SCHED_FIFO, &param);

    if (result < 0)
    {
        perror("Failed to set realtime priority for audio thread");
    }
*/
    while (!exiting)
    {
        if ((alive == true) && (threadrun == true))
        {
                remoteVSTServerInstance->dispatchGetSet(50);
         }
        else
            usleep(10000);
    }
    // param.sched_priority = 0;
    // (void)sched_setscheduler(0, SCHED_OTHER, &param);
    getfin = 1;
    ExitThread(0);
    return 0;
}

DWORD WINAPI ParThreadMain(LPVOID parameter)
{
/*
    struct sched_param param;
    param.sched_priority = 1;

    int result = sched_setscheduler(0, SCHED_FIFO, &param);

    if (result < 0)
    {
        perror("Failed to set realtime priority for audio thread");
    }
*/
    while (!exiting)
    {
        if (alive == true)
        {
                remoteVSTServerInstance->dispatchPar(50);
        }
        else
            usleep(10000);
    }
    // param.sched_priority = 0;
    // (void)sched_setscheduler(0, SCHED_OTHER, &param);
     parfin = 1;
     ExitThread(0);
    return 0;
}

RemoteVSTServer::RemoteVSTServer(std::string fileIdentifiers, AEffect *plugin, std::string fallbackName) :
    RemotePluginServer(fileIdentifiers),
    m_plugin(plugin),
    m_name(fallbackName),
    m_maker(""),
    setprogrammiss(0),
    hostreaper(0),
    haveGui(true),
    timerval(0),
    hWnd(0),
#ifdef EMBED
#ifdef EMBEDRESIZE
    guiupdate(0),
    guiupdatecount(0),
    guiresizewidth(500),
    guiresizeheight(200),
    hWnd(0)
#endif
#else
    hWnd(0)
#endif
{   
    if(starterror == 1)
    return;

    if (!(m_plugin->flags & effFlagsHasEditor))
    {
        cerr << "dssi-vst-server[1]: Plugin has no GUI" << endl;
        haveGui = false;
    }

    if (haveGui == true)
    {
	memset(&wclass, 0, sizeof(WNDCLASSEX));
        wclass.cbSize = sizeof(WNDCLASSEX);
        wclass.style = 0;
        wclass.lpfnWndProc = MainProc;
        wclass.cbClsExtra = 0;
        wclass.cbWndExtra = 0;
        wclass.hInstance = GetModuleHandle(0);
        wclass.hIcon = LoadIcon(GetModuleHandle(0), APPLICATION_CLASS_NAME);
        wclass.hCursor = LoadCursor(0, IDI_APPLICATION);
        // wclass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wclass.lpszMenuName = "MENU_DSSI_VST";
        wclass.lpszClassName = APPLICATION_CLASS_NAME;
        wclass.hIconSm = 0;

        if (!RegisterClassEx(&wclass))
        {
            cerr << "dssi-vst-server: ERROR: Failed to register Windows application class!\n" << endl;
            haveGui = false;
        }
	timerval = SetTimer(0, 0, 20, 0);
    }

#ifndef EMBED
    if (haveGui == true)
    {
	#ifdef DRAG
        hWnd = CreateWindowEx(WS_EX_ACCEPTFILES, APPLICATION_CLASS_NAME, "LinVst", WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
                            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, GetModuleHandle(0), 0);	    
	#else    
        hWnd = CreateWindow(APPLICATION_CLASS_NAME, "LinVst", WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
                            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, GetModuleHandle(0), 0);
	#endif    
        if (!hWnd)
        {
            cerr << "dssi-vst-server: ERROR: Failed to create window!\n" << endl;
            haveGui = false;
        }
    }
#endif
}

void RemoteVSTServer::EffectOpen()
{
    if (debugLevel > 0)
        cerr << "dssi-vst-server[1]: opening plugin" << endl;

    m_plugin->dispatcher(m_plugin, effOpen, 0, 0, NULL, 0);
    m_plugin->dispatcher(m_plugin, effMainsChanged, 0, 0, NULL, 0);

    if (m_plugin->dispatcher(m_plugin, effGetVstVersion, 0, 0, NULL, 0) < 2)
    {
        if (debugLevel > 0)
            cerr << "dssi-vst-server[1]: plugin is VST 1.x" << endl;
    }
    else
    {
        if (debugLevel > 0)
            cerr << "dssi-vst-server[1]: plugin is VST 2.0 or newer" << endl;
    }

    char buffer[512];
    memset(buffer, 0, sizeof(buffer));

    m_plugin->dispatcher(m_plugin, effGetEffectName, 0, 0, buffer, 0);
    if (debugLevel > 0)
        cerr << "dssi-vst-server[1]: plugin name is \"" << buffer << "\"" << endl;
    if (buffer[0]) m_name = buffer;

/*
    if (strncmp(buffer, "Guitar Rig 5", 12) == 0)
        setprogrammiss = 1;
    if (strncmp(buffer, "T-Rack", 6) == 0)
        setprogrammiss = 1;
*/

    memset(buffer, 0, sizeof(buffer));

    m_plugin->dispatcher(m_plugin, effGetVendorString, 0, 0, buffer, 0);
    if (debugLevel > 0)
        cerr << "dssi-vst-server[1]: vendor string is \"" << buffer << "\"" << endl;
    if (buffer[0]) m_maker = buffer;

/*
    if (strncmp(buffer, "IK", 2) == 0)
        setprogrammiss = 1;
*/

    m_plugin->dispatcher(m_plugin, effMainsChanged, 0, 1, NULL, 0);
	
#ifndef EMBED
    if (haveGui == true)
    {
    if(hWnd)
    SetWindowText(hWnd, m_name.c_str());
    }
#endif   	

    plugok = true;
}

RemoteVSTServer::~RemoteVSTServer()
{
    if (haveGui == true)
    {
  //  KillTimer(0, timerval);
   
    if (guiVisible)
    {
    if(m_plugin)
    m_plugin->dispatcher(m_plugin, effEditClose, 0, 0, 0, 0);
    }
    }
	
    if(m_plugin)
    {
    m_plugin->dispatcher(m_plugin, effMainsChanged, 0, 0, NULL, 0);
    m_plugin->dispatcher(m_plugin, effClose, 0, 0, NULL, 0);
    }
}

void RemoteVSTServer::process(float **inputs, float **outputs, int sampleFrames)
{
    inProcessThread = true;
    m_plugin->processReplacing(m_plugin, inputs, outputs, sampleFrames);
    inProcessThread = false;
}

int RemoteVSTServer::getEffInt(int opcode)
{
    return m_plugin->dispatcher(m_plugin, opcode, 0, 0, NULL, 0);
}

void RemoteVSTServer::effDoVoid(int opcode)
{
/*
    if (opcode == effCanDo)
    {
        hostreaper = 1;
         return;
    }
*/
    if (opcode == effClose)
    {
         // usleep(500000);
        waitForServerexit();
        terminate();
    }
    else
    {
        m_plugin->dispatcher(m_plugin, opcode, 0, 0, NULL, 0);
    }
}

std::string RemoteVSTServer::getEffString(int opcode, int index)
{
    char name[512];
    memset(name, 0, sizeof(name));

    m_plugin->dispatcher(m_plugin, opcode, index, 0, name, 0);
    return name;
}

void RemoteVSTServer::setBufferSize(int sz)
{
    if (bufferSize != sz)
    {
        m_plugin->dispatcher(m_plugin, effMainsChanged, 0, 0, NULL, 0);
        m_plugin->dispatcher(m_plugin, effSetBlockSize, 0, sz, NULL, 0);
        m_plugin->dispatcher(m_plugin, effMainsChanged, 0, 1, NULL, 0);
        bufferSize = sz;
        threadrun = true;
    }
   
    if (debugLevel > 0)
        cerr << "dssi-vst-server[1]: set buffer size to " << sz << endl;
}

void RemoteVSTServer::setSampleRate(int sr)
{
    if (sampleRate != sr)
    {
        m_plugin->dispatcher(m_plugin, effMainsChanged, 0, 0, NULL, 0);
        m_plugin->dispatcher(m_plugin, effSetSampleRate, 0, 0, NULL, (float)sr);
        m_plugin->dispatcher(m_plugin, effMainsChanged, 0, 1, NULL, 0);
        sampleRate = sr;
    }

    if (debugLevel > 0)
        cerr << "dssi-vst-server[1]: set sample rate to " << sr << endl;
}

void RemoteVSTServer::reset()
{
    cerr << "dssi-vst-server[1]: reset" << endl;

    m_plugin->dispatcher(m_plugin, effMainsChanged, 0, 0, NULL, 0);
    m_plugin->dispatcher(m_plugin, effMainsChanged, 0, 1, NULL, 0);
}

void RemoteVSTServer::terminate()
{
    cerr << "RemoteVSTServer::terminate: setting exiting flag" << endl;

    exiting = true;
}

std::string RemoteVSTServer::getParameterName(int p)
{
    char name[512];
    memset(name, 0, sizeof(name));

    m_plugin->dispatcher(m_plugin, effGetParamName, p, 0, name, 0);
    return name;
}

void RemoteVSTServer::setParameter(int p, float v)
{
    m_plugin->setParameter(m_plugin, p, v);
}

float RemoteVSTServer::getParameter(int p)
{
    return m_plugin->getParameter(m_plugin, p);
}

void RemoteVSTServer::getParameters(int p0, int pn, float *v)
{
    for (int i = p0; i <= pn; ++i)
        v[i - p0] = m_plugin->getParameter(m_plugin, i);
}

std::string RemoteVSTServer::getProgramNameIndexed(int p)
{
    if (debugLevel > 1)
        cerr << "dssi-vst-server[2]: getProgramName(" << p << ")" << endl;

    char name[512];
    memset(name, 0, sizeof(name));

    m_plugin->dispatcher(m_plugin, effGetProgramNameIndexed, p, 0, name, 0);
    return name;
}

std::string
RemoteVSTServer::getProgramName()
{
    if (debugLevel > 1)
        cerr << "dssi-vst-server[2]: getProgramName()" << endl;

    char name[512];
    memset(name, 0, sizeof(name));

    m_plugin->dispatcher(m_plugin, effGetProgramName, 0, 0, name, 0);
    return name;
}

void
RemoteVSTServer::setCurrentProgram(int p)
{
    if (debugLevel > 1)
        cerr << "dssi-vst-server[2]: setCurrentProgram(" << p << ")" << endl;

/*
    if ((hostreaper == 1) && (setprogrammiss == 1))
    {
        writeIntring(&m_shmControl5->ringBuffer, 1);
        return;
    }
*/

    if (p < m_plugin->numPrograms)
        m_plugin->dispatcher(m_plugin, effSetProgram, 0, p, 0, 0);
}

bool RemoteVSTServer::warn(std::string warning)
{
    if (hWnd)
        MessageBox(hWnd, warning.c_str(), "Error", 0);
    return true;
}

void RemoteVSTServer::showGUI()
{
#ifdef EMBED
        winm.handle = 0;
        winm.width = 0;
        winm.height = 0;

        handlewin = 0; 

    if (debugLevel > 0)
        cerr << "RemoteVSTServer::showGUI(" << "): guiVisible is " << guiVisible << endl;

    if (haveGui == false)
    {
        winm.handle = 0;
        winm.width = 0;
        winm.height = 0;
        tryWrite(&m_shm[FIXED_SHM_SIZE], &winm, sizeof(winm));
        return;
    }

    if (guiVisible)
    {
        winm.handle = 0;
        winm.width = 0;
        winm.height = 0;
        tryWrite(&m_shm[FIXED_SHM_SIZE], &winm, sizeof(winm));
        return;
    }

    if(threadrun == false)
    {
        winm.handle = 0;
        winm.width = 0;
        winm.height = 0;
        tryWrite(&m_shm[FIXED_SHM_SIZE], &winm, sizeof(winm));
        return;
    }

   // if (hWnd)
   //     DestroyWindow(hWnd);
    hWnd = 0;


#ifdef EMBEDDRAG
#ifndef XEMBED
    hWnd = CreateWindowEx(WS_EX_TOOLWINDOW | WS_EX_ACCEPTFILES, APPLICATION_CLASS_NAME, "LinVst", WS_POPUP, 0, 0, 200, 200, 0, 0, GetModuleHandle(0), 0);
#endif
#else
    hWnd = CreateWindowEx(WS_EX_TOOLWINDOW, APPLICATION_CLASS_NAME, "LinVst", WS_POPUP, 0, 0, 200, 200, 0, 0, GetModuleHandle(0), 0);
#endif
    if (!hWnd)
    {
        // cerr << "dssi-vst-server: ERROR: Failed to create window!\n" << endl;
        guiVisible = false;
        winm.handle = 0;
        winm.width = 0;
        winm.height = 0;
        tryWrite(&m_shm[FIXED_SHM_SIZE], &winm, sizeof(winm));
        return;
    }
    else if (debugLevel > 0)
        cerr << "dssi-vst-server[1]: created main window" << endl;

    m_plugin->dispatcher(m_plugin, effEditOpen, 0, 0, hWnd, 0);
    rect = 0;
    m_plugin->dispatcher(m_plugin, effEditGetRect, 0, 0, &rect, 0);
    if (!rect)
    {
        // cerr << "dssi-vst-server: ERROR: Plugin failed to report window size\n" << endl;
        DestroyWindow(hWnd);
        guiVisible = false;
        winm.handle = 0;
        winm.width = 0;
        winm.height = 0;
        tryWrite(&m_shm[FIXED_SHM_SIZE], &winm, sizeof(winm));
        return;
    }
    else
    {
        SetWindowPos(hWnd, HWND_TOP, 0, 0, rect->right - rect->left, rect->bottom - rect->top, 0);

        if (debugLevel > 0)
            cerr << "dssi-vst-server[1]: sized window" << endl;

        winm.width = rect->right - rect->left;
        winm.height = rect->bottom - rect->top;
        handlewin = GetPropA(hWnd, "__wine_x11_whole_window");
        winm.handle = (long int) handlewin;
        tryWrite(&m_shm[FIXED_SHM_SIZE], &winm, sizeof(winm));
        guiVisible = true;
    }
#else

    if (debugLevel > 0)
        cerr << "RemoteVSTServer::showGUI(" << "): guiVisible is " << guiVisible << endl;

    if (haveGui == false)
    {
        guiVisible = false;
        return;
    }

    if (!hWnd)
    {
        guiVisible = false;
        return;
    }

    if (guiVisible)
    {
        return;
    }

    m_plugin->dispatcher(m_plugin, effEditOpen, 0, 0, hWnd, 0);
    rect = 0;
    m_plugin->dispatcher(m_plugin, effEditGetRect, 0, 0, &rect, 0);
    if (!rect)
    {
        // cerr << "dssi-vst-server: ERROR: Plugin failed to report window size\n" << endl;
        guiVisible = false;
        return;
    }
    else
    {
	    
#ifdef WINONTOP
        SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, rect->right - rect->left + 6, rect->bottom - rect->top + 25, SWP_NOMOVE);
#else	    
        SetWindowPos(hWnd, HWND_TOP, 0, 0, rect->right - rect->left + 6, rect->bottom - rect->top + 25, SWP_NOMOVE);
#endif
        if (debugLevel > 0)
            cerr << "dssi-vst-server[1]: sized window" << endl;

        ShowWindow(hWnd, SW_SHOWNORMAL);
        UpdateWindow(hWnd);
        guiVisible = true;
    }
#endif
}

void RemoteVSTServer::hideGUI()
{
    // if (!hWnd)
        // return;

    if ((haveGui == false) || (guiVisible == false))
    {
         return;
    }

#ifndef EMBED
    ShowWindow(hWnd, SW_HIDE);
    UpdateWindow(hWnd);
#endif
    m_plugin->dispatcher(m_plugin, effEditClose, 0, 0, 0, 0);
    guiVisible = false;

    if (!exiting)
        usleep(50000);
}


#ifdef EMBED
void RemoteVSTServer::openGUI()
{
    ShowWindow(hWnd, SW_SHOWNORMAL);
    // ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);
}
#endif

int RemoteVSTServer::processVstEvents()
{
    int         els;
    int         *ptr;
    int         sizeidx = 0;
    int         size;
    VstEvents   *evptr;

    ptr = (int *) m_shm2;
    els = *ptr;
    sizeidx = sizeof(int);

    if (els > VSTSIZE)
        els = VSTSIZE;

    evptr = &vstev[0];
    evptr->numEvents = els;
    evptr->reserved = 0;

    for (int i = 0; i < els; i++)
    {
        VstEvent* bsize = (VstEvent*) &m_shm2[sizeidx];
        size = bsize->byteSize + (2*sizeof(VstInt32));
        evptr->events[i] = bsize;
        sizeidx += size;
    }

    m_plugin->dispatcher(m_plugin, effProcessEvents, 0, 0, evptr, 0);

    return 1;
}

void RemoteVSTServer::getChunk()
{
    void *ptr;
    int bnk_prg = readIntring(&m_shmControl5->ringBuffer);
    int sz = m_plugin->dispatcher(m_plugin, effGetChunk, bnk_prg, 0, &ptr, 0);
    writeInt(&m_shm[FIXED_SHM_SIZE], sz);
    tryWrite(&m_shm[FIXED_SHM_SIZE / 2], ptr, sz);
    return;
}

void RemoteVSTServer::setChunk()
{
    int sz = readIntring(&m_shmControl5->ringBuffer);
    int bnk_prg = readIntring(&m_shmControl5->ringBuffer);
    void *ptr = malloc(sz);
    tryRead(&m_shm[FIXED_SHM_SIZE / 2], ptr, sz);
    int r = m_plugin->dispatcher(m_plugin, effSetChunk, bnk_prg, sz, ptr, 0);
    free(ptr);
    writeInt(&m_shm[FIXED_SHM_SIZE], r);
    return;
}

/*
void RemoteVSTServer::canBeAutomated()
{
    int param = readIntring(&m_shmControl5->ringBuffer);
    int r = m_plugin->dispatcher(m_plugin, effCanBeAutomated, param, 0, 0, 0);
    writeInt(&m_shm[FIXED_SHM_SIZE], r);
}
*/

void RemoteVSTServer::getProgram()
{
    int r = m_plugin->dispatcher(m_plugin, effGetProgram, 0, 0, 0, 0);
    writeInt(&m_shm[FIXED_SHM_SIZE], r);
}

#ifdef SEM

void
RemoteVSTServer::waitForServer()
{
    if(remoteVSTServerInstance->m_386run == 0)
    {
    sem_post(&m_shmControl->runServer);

    timespec ts_timeout;
    clock_gettime(CLOCK_REALTIME, &ts_timeout);
    ts_timeout.tv_sec += 5;
    if (sem_timedwait(&m_shmControl->runClient, &ts_timeout) != 0) {
         if(m_inexcept == 0)
         RemotePluginClosedException();
    }
    }
    else
    {
    fpost(&m_shmControl->runServer386);

    if (fwait(&m_shmControl->runClient386, 5000)) {
         if(m_inexcept == 0)
	 RemotePluginClosedException();
    }
   }
}


void
RemoteVSTServer::waitForServerexit()
{
    if(remoteVSTServerInstance->m_386run == 0)
    {
    sem_post(&m_shmControl->runServer);
    }
    else
    {
    fpost(&m_shmControl->runServer386);
   }
}


#else

void
RemoteVSTServer::waitForServer()
{
    fpost(&m_shmControl->runServer);

    if (fwait(&m_shmControl->runClient, 5000)) {
         if(m_inexcept == 0)
	 RemotePluginClosedException();
    }
}

void
RemoteVSTServer::waitForServerexit()
{
    fpost(&m_shmControl->runServer);
}

#endif

#if VST_2_4_EXTENSIONS
VstIntPtr VSTCALLBACK hostCallback(AEffect *plugin, VstInt32 opcode, VstInt32 index, VstIntPtr value, void *ptr, float opt)
#else
long VSTCALLBACK hostCallback(AEffect *plugin, long opcode, long index, long value, void *ptr, float opt)
#endif
{
    VstTimeInfo timeInfo;
    int rv = 0;

    switch (opcode)
    {
    case audioMasterAutomate:
        plugin->setParameter(plugin, index, opt);
        break;

    case audioMasterVersion:
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterVersion requested" << endl;
        rv = 2300;
        break;

    case audioMasterCurrentId:
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterCurrentId requested" << endl;
        rv = 0;
        break;

    case audioMasterIdle:
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterIdle requested " << endl;
        // plugin->dispatcher(plugin, effEditIdle, 0, 0, 0, 0);
        break;

    case DEPRECATED_VST_SYMBOL(audioMasterPinConnected):
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterPinConnected requested" << endl;
        break;

    case DEPRECATED_VST_SYMBOL(audioMasterWantMidi):
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterWantMidi requested" << endl;
        // happy to oblige
        rv = 1;
        break;

    case audioMasterGetTime:
        if (alive && !exiting && threadrun)
        {

    remoteVSTServerInstance->writeOpcodering(&remoteVSTServerInstance->m_shmControl->ringBuffer, (RemotePluginOpcode)opcode);
    remoteVSTServerInstance->writeIntring(&remoteVSTServerInstance->m_shmControl->ringBuffer, value);
   
    remoteVSTServerInstance->commitWrite(&remoteVSTServerInstance->m_shmControl->ringBuffer);
    remoteVSTServerInstance->waitForServer();

    if(remoteVSTServerInstance->timeinfo)
    {
    memcpy(remoteVSTServerInstance->timeinfo, &remoteVSTServerInstance->m_shm3[FIXED_SHM_SIZE3 - 8192], sizeof(VstTimeInfo));

    // printf("%f\n", timeInfo.sampleRate);

    rv = (long)remoteVSTServerInstance->timeinfo;
    }
        }
        break;

    case audioMasterProcessEvents:
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterProcessEvents requested" << endl;
        {
            VstEvents   *evnts;
            int         eventnum;
            int         *ptr2;
            int         sizeidx = 0;
            int         ok;

            if (alive && !exiting && threadrun)
            {
                evnts = (VstEvents*)ptr;

                if ((!evnts) || (evnts->numEvents <= 0))
                    break;

                eventnum = evnts->numEvents;

                ptr2 = (int *)remoteVSTServerInstance->m_shm3;

                sizeidx = sizeof(int);

                if (eventnum > VSTSIZE)
                    eventnum = VSTSIZE;            

                for (int i = 0; i < evnts->numEvents; i++)
                {

                    VstEvent *pEvent = evnts->events[i];
                    if (pEvent->type == kVstSysExType)
                        eventnum--;
                    else
                    {
                        unsigned int size = (2*sizeof(VstInt32)) + evnts->events[i]->byteSize;
                        memcpy(&remoteVSTServerInstance->m_shm3[sizeidx], evnts->events[i], size);
                        sizeidx += size;
                    }
                }
                *ptr2 = eventnum;

    remoteVSTServerInstance->writeOpcodering(&remoteVSTServerInstance->m_shmControl->ringBuffer, (RemotePluginOpcode)opcode);
    remoteVSTServerInstance->writeIntring(&remoteVSTServerInstance->m_shmControl->ringBuffer, value);
   
    remoteVSTServerInstance->commitWrite(&remoteVSTServerInstance->m_shmControl->ringBuffer);
    remoteVSTServerInstance->waitForServer();

            }
        }
        break;

    case DEPRECATED_VST_SYMBOL(audioMasterSetTime):
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterSetTime requested" << endl;
        break;

    case DEPRECATED_VST_SYMBOL(audioMasterTempoAt):
        // if (debugLevel > 1)
            // cerr << "dssi-vst-server[2]: audioMasterTempoAt requested" << endl;
        // can't support this, return 120bpm
        rv = 120 * 10000;
        break;

    case DEPRECATED_VST_SYMBOL(audioMasterGetNumAutomatableParameters):
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterGetNumAutomatableParameters requested" << endl;
        rv = 5000;
        break;

    case DEPRECATED_VST_SYMBOL(audioMasterGetParameterQuantization):
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterGetParameterQuantization requested" << endl;
        rv = 1;
        break;

    case audioMasterIOChanged:
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterIOChanged requested" << endl;
    struct amessage
    {
        int flags;
        int pcount;
        int parcount;
        int incount;
        int outcount;
        int delay;
    } am;

    if (alive && !exiting && threadrun)
    {
        am.flags = plugin->flags;
        am.pcount = plugin->numPrograms;
        am.parcount = plugin->numParams;
        am.incount = plugin->numInputs;
        am.outcount = plugin->numOutputs;
        am.delay = plugin->initialDelay;
        am.flags &= ~effFlagsCanDoubleReplacing;

        memcpy(&remoteVSTServerInstance->m_shm3[FIXED_SHM_SIZE3], &am, sizeof(am));

    remoteVSTServerInstance->writeOpcodering(&remoteVSTServerInstance->m_shmControl->ringBuffer, (RemotePluginOpcode)opcode);
   
    remoteVSTServerInstance->commitWrite(&remoteVSTServerInstance->m_shmControl->ringBuffer);
    remoteVSTServerInstance->waitForServer();

/*
        AEffect* update = remoteVSTServerInstance->m_plugin;
        update->flags = am.flags;
        update->numPrograms = am.pcount;
        update->numParams = am.parcount;
        update->numInputs = am.incount;
        update->numOutputs = am.outcount;
        update->initialDelay = am.delay;
*/
    }
        break;

    case DEPRECATED_VST_SYMBOL(audioMasterNeedIdle):
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterNeedIdle requested" << endl;
        // might be nice to handle this better
        rv = 1;
        break;

    case audioMasterSizeWindow:
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterSizeWindow requested" << endl;
{
#ifdef EMBED
#ifdef EMBEDRESIZE
   int opcodegui = 123456789;

        if (remoteVSTServerInstance->hWnd && !exiting && guiVisible)
	{	
    remoteVSTServerInstance->guiresizewidth = index;
    remoteVSTServerInstance->guiresizeheight = value;

   ShowWindow(remoteVSTServerInstance->hWnd, SW_HIDE);
   SetWindowPos(remoteVSTServerInstance->hWnd, HWND_TOP, 0, 0, remoteVSTServerInstance->guiresizewidth, remoteVSTServerInstance->guiresizeheight, 0);
	    
    remoteVSTServerInstance->writeOpcodering(&remoteVSTServerInstance->m_shmControl->ringBuffer, (RemotePluginOpcode)opcodegui);
    remoteVSTServerInstance->writeIntring(&remoteVSTServerInstance->m_shmControl->ringBuffer, index);
    remoteVSTServerInstance->writeIntring(&remoteVSTServerInstance->m_shmControl->ringBuffer, value);
    remoteVSTServerInstance->commitWrite(&remoteVSTServerInstance->m_shmControl->ringBuffer);
    remoteVSTServerInstance->waitForServer();
    remoteVSTServerInstance->guiupdate = 1;
    rv = 1;	
	}
#endif
#else
        if (remoteVSTServerInstance->hWnd && !exiting && guiVisible)
	{	
            SetWindowPos(remoteVSTServerInstance->hWnd, 0, 0, 0, index + 6, value + 25, SWP_NOMOVE | SWP_HIDEWINDOW);		    
	    ShowWindow(remoteVSTServerInstance->hWnd, SW_SHOWNORMAL);
            UpdateWindow(remoteVSTServerInstance->hWnd);	      
        rv = 1;
        }
#endif
}
        break;

    case audioMasterGetSampleRate:
        //  if (debugLevel > 1)
            // cerr << "dssi-vst-server[2]: audioMasterGetSampleRate requested" << endl;

        if (!sampleRate)
        {
            //  cerr << "WARNING: Sample rate requested but not yet set" << endl;
            break;
        }
        plugin->dispatcher(plugin, effSetSampleRate, 0, 0, NULL, (float)sampleRate);
        break;

    case audioMasterGetBlockSize:
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterGetBlockSize requested" << endl;
        if (!bufferSize)
        {
            // cerr << "WARNING: Buffer size requested but not yet set" << endl;
            break;
        }
        plugin->dispatcher(plugin, effSetBlockSize, 0, bufferSize, NULL, 0);
        break;

    case audioMasterGetInputLatency:
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterGetInputLatency requested" << endl;
        break;

    case audioMasterGetOutputLatency:
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterGetOutputLatency requested" << endl;
        break;

    case DEPRECATED_VST_SYMBOL(audioMasterGetPreviousPlug):
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterGetPreviousPlug requested" << endl;
        break;

    case DEPRECATED_VST_SYMBOL(audioMasterGetNextPlug):
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterGetNextPlug requested" << endl;
        break;

    case DEPRECATED_VST_SYMBOL(audioMasterWillReplaceOrAccumulate):
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterWillReplaceOrAccumulate requested" << endl;
        // 0 -> unsupported, 1 -> replace, 2 -> accumulate
        rv = 1;
        break;

    case audioMasterGetCurrentProcessLevel:
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterGetCurrentProcessLevel requested (level is " << (inProcessThread ? 2 : 1) << ")" << endl;
        // 0 -> unsupported, 1 -> gui, 2 -> process, 3 -> midi/timer, 4 -> offline
        if (inProcessThread)
            rv = 2;
        else
            rv = 1;
        break;

    case audioMasterGetAutomationState:
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterGetAutomationState requested" << endl;
        rv = 4; // read/write
        break;

    case audioMasterOfflineStart:
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterOfflineStart requested" << endl;
        break;

    case audioMasterOfflineRead:
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterOfflineRead requested" << endl;
        break;

    case audioMasterOfflineWrite:
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterOfflineWrite requested" << endl;
        break;

    case audioMasterOfflineGetCurrentPass:
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterOfflineGetCurrentPass requested" << endl;
        break;

    case audioMasterOfflineGetCurrentMetaPass:
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterOfflineGetCurrentMetaPass requested" << endl;
        break;

    case DEPRECATED_VST_SYMBOL(audioMasterSetOutputSampleRate):
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterSetOutputSampleRate requested" << endl;
        break;

/* Deprecated in VST 2.4 and also (accidentally?) renamed in the SDK header,
   so we won't retain it here
    case audioMasterGetSpeakerArrangement:
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterGetSpeakerArrangement requested" << endl;
        break;
*/
    case audioMasterGetVendorString:
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterGetVendorString requested" << endl;
        strcpy((char *)ptr, "Chris Cannam");
        break;

    case audioMasterGetProductString:
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterGetProductString requested" << endl;
        strcpy((char *)ptr, "DSSI VST Wrapper Plugin");
        break;

    case audioMasterGetVendorVersion:
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterGetVendorVersion requested" << endl;
        rv = long(RemotePluginVersion * 100);
        break;

    case audioMasterVendorSpecific:
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterVendorSpecific requested" << endl;
        break;

    case DEPRECATED_VST_SYMBOL(audioMasterSetIcon):
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterSetIcon requested" << endl;
        break;

    case audioMasterCanDo:
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterCanDo(" << (char *)ptr << ") requested" << endl;
        if (!strcmp((char*)ptr, "sendVstEvents")
                    || !strcmp((char*)ptr, "sendVstMidiEvent")
                    || !strcmp((char*)ptr, "sendVstTimeInfo")
#ifdef EMBED
#ifdef EMBEDRESIZE
                    || !strcmp((char*)ptr, "sizeWindow")
#endif
#else
                    || !strcmp((char*)ptr, "sizeWindow")
#endif
                    // || !strcmp((char*)ptr, "supplyIdle")
                    )
            rv = 1;
        break;

    case audioMasterGetLanguage:
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterGetLanguage requested" << endl;
        rv = kVstLangEnglish;
        break;

    case DEPRECATED_VST_SYMBOL(audioMasterOpenWindow):
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterOpenWindow requested" << endl;
        break;

    case DEPRECATED_VST_SYMBOL(audioMasterCloseWindow):
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterCloseWindow requested" << endl;
        break;

    case audioMasterGetDirectory:
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterGetDirectory requested" << endl;
        break;

    case audioMasterUpdateDisplay:
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterUpdateDisplay requested" << endl;
        break;

    case audioMasterBeginEdit:
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterBeginEdit requested" << endl;
        break;

    case audioMasterEndEdit:
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterEndEdit requested" << endl;
        break;

    case audioMasterOpenFileSelector:
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterOpenFileSelector requested" << endl;
        break;

    case audioMasterCloseFileSelector:
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterCloseFileSelector requested" << endl;
        break;

    case DEPRECATED_VST_SYMBOL(audioMasterEditFile):
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterEditFile requested" << endl;
        break;

    case DEPRECATED_VST_SYMBOL(audioMasterGetChunkFile):
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterGetChunkFile requested" << endl;
        break;

    case DEPRECATED_VST_SYMBOL(audioMasterGetInputSpeakerArrangement):
        if (debugLevel > 1)
            cerr << "dssi-vst-server[2]: audioMasterGetInputSpeakerArrangement requested" << endl;
        break;

    default:
        if (debugLevel > 0)
            cerr << "dssi-vst-server[0]: unsupported audioMaster callback opcode " << opcode << endl;
    }
    return rv;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR cmdline, int cmdshow)
{
    char *libname = 0;
    char *libname2 = 0;
    char *fileInfo = 0;

    cout << "DSSI VST plugin server v" << RemotePluginVersion << endl;
    cout << "Copyright (c) 2012-2013 Filipe Coelho" << endl;
    cout << "Copyright (c) 2010-2011 Kristian Amlie" << endl;
    cout << "Copyright (c) 2004-2006 Chris Cannam" << endl;

    if (cmdline)
    {
        int offset = 0;
        if (cmdline[0] == '"' || cmdline[0] == '\'') offset = 1;
            for (int ci = offset; cmdline[ci]; ++ci)
            {
                if (cmdline[ci] == ',')
                {
                    libname2 = strndup(cmdline + offset, ci - offset);
                    ++ci;
                    if (cmdline[ci])
                    {
                        fileInfo = strdup(cmdline + ci);
                        int l = strlen(fileInfo);
                        if (fileInfo[l-1] == '"' || fileInfo[l-1] == '\'')
                            fileInfo[l-1] = '\0';
                    }
                }
            }
    }

    if (libname2 != NULL)
    {
        if ((libname2[0] == '/') && (libname2[1] == '/'))
            libname = strdup(&libname2[1]);
        else
            libname = strdup(libname2);
    }
    else
    {
        cerr << "Usage: dssi-vst-server <vstname.dll>,<tmpfilebase>" << endl;
        cerr << "(Command line was: " << cmdline << ")" << endl;
        return 1;
    }

    if (!libname || !libname[0] || !fileInfo || !fileInfo[0])
    {
        cerr << "Usage: dssi-vst-server <vstname.dll>,<tmpfilebase>" << endl;
        cerr << "(Command line was: " << cmdline << ")" << endl;
        return 1;
    }

    cout << "Loading  " << libname << endl;

    HINSTANCE libHandle = 0;
    libHandle = LoadLibrary(libname);
    if (!libHandle)
    {
        cerr << "dssi-vst-server: ERROR: Couldn't load VST DLL \"" << libname << "\"" << endl;
        return 1;
    }

    VstEntry getinstance = 0;

    getinstance = (VstEntry)GetProcAddress(libHandle, NEW_PLUGIN_ENTRY_POINT);

    if (!getinstance) {
    getinstance = (VstEntry)GetProcAddress(libHandle, OLD_PLUGIN_ENTRY_POINT);
    if (!getinstance) {
       cerr << "dssi-vst-server: ERROR: VST entrypoints \"" << NEW_PLUGIN_ENTRY_POINT << "\" or \""
                << OLD_PLUGIN_ENTRY_POINT << "\" not found in DLL \"" << libname << "\"" << endl;
        FreeLibrary(libHandle);
        return 1;
      }
    }

    AEffect *plugin = getinstance(hostCallback);
    if (!plugin)
    {
        cerr << "dssi-vst-server: ERROR: Failed to instantiate plugin in VST DLL \"" << libname << "\"" << endl;
        FreeLibrary(libHandle);
        return 1;
    }

    if (plugin->magic != kEffectMagic)
    {
        cerr << "dssi-vst-server: ERROR: Not a VST plugin in DLL \"" << libname << "\"" << endl;
        FreeLibrary(libHandle);
        return 1;
    }

    if (!(plugin->flags & effFlagsCanReplacing))
    {
        cerr << "dssi-vst-server: ERROR: Plugin does not support processReplacing (required)" << endl;
        FreeLibrary(libHandle);
        return 1;
    }

    DWORD threadIdp = 0;
    ThreadHandle[0] = CreateThread(0, 0, AudioThreadMain, 0, 0, &threadIdp);
    if (!ThreadHandle[0])
    {
        cerr << "Failed to create par thread!" << endl;
        FreeLibrary(libHandle);
        return 1;
    }

    DWORD threadIdp2 = 0;
    ThreadHandle[1] = CreateThread(0, 0, GetSetThreadMain, 0, 0, &threadIdp2);
    if (!ThreadHandle[1])
    {
        cerr << "Failed to create par thread!" << endl;
        TerminateThread(ThreadHandle[0], 0);
        FreeLibrary(libHandle);
        return 1;
    }

    DWORD threadIdp3 = 0;
    ThreadHandle[2] = CreateThread(0, 0, ParThreadMain, 0, 0, &threadIdp3);
    if (!ThreadHandle[2])
    {
        cerr << "Failed to create par thread!" << endl;
        TerminateThread(ThreadHandle[0], 0);
        TerminateThread(ThreadHandle[1], 0);
        FreeLibrary(libHandle);
        return 1;
    }

        remoteVSTServerInstance = new RemoteVSTServer(fileInfo, plugin, libname);
    
        if(!remoteVSTServerInstance)
        {
        cerr << "ERROR: Remote VST startup failed" << endl;
        TerminateThread(ThreadHandle[0], 0);
        TerminateThread(ThreadHandle[1], 0);
        TerminateThread(ThreadHandle[2], 0);
        FreeLibrary(libHandle);
        return 1; 
        }

        if(remoteVSTServerInstance->starterror == 1)
        {
        cerr << "ERROR: Remote VST startup failed" << endl;
        TerminateThread(ThreadHandle[0], 0);
        TerminateThread(ThreadHandle[1], 0);
        TerminateThread(ThreadHandle[2], 0);
        FreeLibrary(libHandle);
        return 1; 
        }

    alive = true;

    MSG msg;
    exiting = false;
    while (!exiting)
    {
        while (!exiting && PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
        {
	    TranslateMessage(&msg);
            DispatchMessage(&msg);

            // this bit based on fst by Torben Hohn, patch worked out by Robert Jonsson - thanks!
              if (msg.message == WM_TIMER)
              {
                if (guiVisible == true)
                {
                plugin->dispatcher (plugin, effEditIdle, 0, 0, NULL, 0);
#ifdef EMBED
#ifdef EMBEDRESIZE
               if(remoteVSTServerInstance->guiupdate == 1)
                {
                 remoteVSTServerInstance->guiupdatecount += 1;

                 if(remoteVSTServerInstance->guiupdatecount == 10)
                  {
                  remoteVSTServerInstance->guiupdate = 0;
                  remoteVSTServerInstance->guiupdatecount = 0;
                  ShowWindow(remoteVSTServerInstance->hWnd, SW_SHOWNORMAL);
                  UpdateWindow(remoteVSTServerInstance->hWnd);
                   }
                  }
#endif
#endif
                 }
                }
               }

        if (exiting)
            break;

            remoteVSTServerInstance->dispatchControl(50);

    }

    // wait for audio thread to catch up
    // sleep(1);

    for (int i=0;i<1000;i++)
    {
        usleep(10000);
        if (parfin && audfin && getfin)
            break;
    }

    WaitForMultipleObjects(3, ThreadHandle, TRUE, 5000);

    if (debugLevel > 0)
        cerr << "dssi-vst-server[1]: cleaning up" << endl;

    if (ThreadHandle[0])
    {
        // TerminateThread(ThreadHandle[0], 0);
        CloseHandle(ThreadHandle[0]);
    }

    if (ThreadHandle[1])
    {
        // TerminateThread(ThreadHandle[1], 0);
        CloseHandle(ThreadHandle[1]);
    }

    if (ThreadHandle[2])
    {
        // TerminateThread(ThreadHandle[2], 0);
        CloseHandle(ThreadHandle[2]);
    }

    if (debugLevel > 0)
        cerr << "dssi-vst-server[1]: closed threads" << endl;

    delete remoteVSTServerInstance;
    remoteVSTServerInstance = 0;
    FreeLibrary(libHandle);

    if (debugLevel > 0)
        cerr << "dssi-vst-server[1]: freed dll" << endl;
    if (debugLevel > 0)
        cerr << "dssi-vst-server[1]: exiting" << endl;
    return 0;
}
