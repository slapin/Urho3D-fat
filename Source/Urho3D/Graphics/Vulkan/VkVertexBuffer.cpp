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

#include "../../Precompiled.h"

#include "../../Graphics/Graphics.h"
#include "../../Graphics/GraphicsImpl.h"
#include "../../Graphics/VertexBuffer.h"
#include "../../IO/Log.h"

#include "../../DebugNew.h"

namespace Urho3D
{

// ----------------------------------------------------------------------------
void VertexBuffer::OnDeviceLost()
{
    GPUObject::OnDeviceLost();
}

// ----------------------------------------------------------------------------
void VertexBuffer::OnDeviceReset()
{
    if (!object_.name_)
    {
        Create();
        dataLost_ = !UpdateToGPU();
    }
    else if (dataPending_)
        dataLost_ = !UpdateToGPU();

    dataPending_ = false;
}

// ----------------------------------------------------------------------------
void VertexBuffer::Release()
{
}

// ----------------------------------------------------------------------------
bool VertexBuffer::SetData(const void* data)
{
}

// ----------------------------------------------------------------------------
bool VertexBuffer::SetDataRange(const void* data, unsigned start, unsigned count, bool discard)
{
}

// ----------------------------------------------------------------------------
void* VertexBuffer::Lock(unsigned start, unsigned count, bool discard)
{
}

// ----------------------------------------------------------------------------
void VertexBuffer::Unlock()
{
}

// ----------------------------------------------------------------------------
bool VertexBuffer::Create()
{
}

// ----------------------------------------------------------------------------
bool VertexBuffer::UpdateToGPU()
{
    if (object_.name_ && shadowData_)
        return SetData(shadowData_.Get());
    else
        return false;
}

// ----------------------------------------------------------------------------
void* VertexBuffer::MapBuffer(unsigned start, unsigned count, bool discard)
{
}

// ----------------------------------------------------------------------------
void VertexBuffer::UnmapBuffer()
{
}

}
