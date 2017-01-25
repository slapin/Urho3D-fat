//
// Copyright (c) 2008-2017 the Urho3D project.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include "../IO/SharedLibrary.h"
#include "../IO/Log.h"

#if defined(_WIN32)
#   include <windows.h>
#elif defined (__unix__) || (defined (__APPLE__) && defined (__MACH__))
#   include <dlfcn.h>
#endif

namespace Urho3D
{


// ----------------------------------------------------------------------------
// TODO Should be moved into Util/ perhaps?
#ifdef _WIN32
static String
GetLastErrorString()
{
    LPSTR messageBuffer = NULL;

    /* Get the error message, if any. */
    DWORD errorMessageID = GetLastError();
    if(errorMessageID == 0)
        return String("No error message has been recorded");

    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);

    String errorMsg((char*)messageBuffer);

    /* Free the buffer. */
    LocalFree(messageBuffer);

    return errorMsg;
}
#endif

// ----------------------------------------------------------------------------
SharedLibrary::SharedLibrary(Context* context) :
    Object(context),
    handle_(NULL)
{
}

// ----------------------------------------------------------------------------
SharedLibrary::SharedLibrary(Context* context, const String& fileName) :
    Object(context),
    handle_(NULL),
    fileName_(fileName)
{
    Open(fileName);
}

// ----------------------------------------------------------------------------
SharedLibrary::~SharedLibrary()
{
    Close();
}

// ----------------------------------------------------------------------------
const String& SharedLibrary::GetName() const
{
    return fileName_;
}

// ----------------------------------------------------------------------------
bool SharedLibrary::Open(const String& fileName)
{
#ifdef URHO3D_LOGGING
    URHO3D_LOGDEBUGF("Loading shared library \"%s\"", fileName.CString());
#endif

#if defined(_WIN32)
    handle_= (void*)LoadLibrary(fileName.CString());
    if(handle_ == NULL)
    {
#ifdef URHO3D_LOGGING
        URHO3D_LOGERRORF("Error loading \"%s\": %s", fileName.CString(), GetLastErrorString().CString());
#endif
        return false;
    }
#elif defined (__unix__) || (defined (__APPLE__) && defined (__MACH__))
    handle_ = dlopen(fileName.CString(), RTLD_LAZY);
    if(handle_ == NULL)
    {
#ifdef URHO3D_LOGGING
        URHO3D_LOGERRORF("Error loading \"%s\": %s", fileName.CString(), dlerror());
#endif
        return false;
    }
#else
#   error This needs implementing :)
#endif

    fileName_ = fileName;

    return true;
}

// ----------------------------------------------------------------------------
void SharedLibrary::Close()
{
    if(handle_ == NULL)
        return;

#ifdef URHO3D_LOGGING
    URHO3D_LOGDEBUGF("Unloading shared library \"%s\"", fileName_.CString());
#endif

#if defined(_WIN32)
    FreeLibrary((HINSTANCE)handle_);
#elif defined (__unix__) || (defined (__APPLE__) && defined (__MACH__))
    dlclose(handle_);
#else
#   error This needs implementing :)
#endif

    handle_ = NULL;
}

// ----------------------------------------------------------------------------
bool SharedLibrary::IsOpen() const
{
    return (handle_ != NULL);
}

// ----------------------------------------------------------------------------
void* SharedLibrary::LoadSymbol(const String& symbolName)
{
    if (handle_ == NULL)
    {
#ifdef URHO3D_LOGGING
    URHO3D_LOGERRORF("Can't load symbol \"%s\" because the shared library isn't loaded.", symbolName.CString());
#endif
        return NULL;
    }

#if defined(_WIN32)
    FARPROC ptr = GetProcAddress((HINSTANCE)handle_, symbolName.CString());
    if(ptr == NULL)
    {
#ifdef URHO3D_LOGGING
        URHO3D_LOGERRORF("Error loading symbol \"%s\": %s", symbolName.CString(), GetLastErrorString().CString());
#endif
    }
    return *(void**)&ptr;
#elif defined (__unix__) || (defined (__APPLE__) && defined (__MACH__))
    void* ptr = dlsym(handle_, symbolName.CString());
    if(ptr == NULL)
    {
#ifdef URHO3D_LOGGING
        URHO3D_LOGERRORF("Error loading symbol \"%s\": %s", symbolName.CString(), dlerror());
#endif
    }
    return ptr;
#else
#   error This needs implementing :)
#endif
}

}
