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

#include "../../Core/Context.h"
#include "../../Core/Mutex.h"
#include "../../Core/ProcessUtils.h"
#include "../../Core/Profiler.h"
#include "../../Graphics/ConstantBuffer.h"
#include "../../Graphics/Graphics.h"
#include "../../Graphics/GraphicsEvents.h"
#include "../../Graphics/GraphicsImpl.h"
#include "../../Graphics/IndexBuffer.h"
#include "../../Graphics/RenderSurface.h"
#include "../../Graphics/Shader.h"
#include "../../Graphics/ShaderPrecache.h"
#include "../../Graphics/ShaderProgram.h"
#include "../../Graphics/ShaderVariation.h"
#include "../../Graphics/Texture2D.h"
#include "../../Graphics/TextureCube.h"
#include "../../Graphics/VertexBuffer.h"
#include "../../IO/File.h"
#include "../../IO/Log.h"
#include "../../Resource/ResourceCache.h"

#include "../../DebugNew.h"

#ifdef _WIN32
// Prefer the high-performance GPU on switchable GPU systems
#include <windows.h>
extern "C"
{
    __declspec(dllexport) DWORD NvOptimusEnablement = 1;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

namespace Urho3D
{

// ----------------------------------------------------------------------------
const Vector2 Graphics::pixelUVOffset(0.0f, 0.0f);
bool Graphics::gl3Support = false;

Graphics::Graphics(Context* context_) :
    Object(context_),
    impl_(new GraphicsImpl()),
    window_(0),
    externalWindow_(0),
    width_(0),
    height_(0),
    position_(0, 0),
    multiSample_(1),
    fullscreen_(false),
    borderless_(false),
    resizable_(false),
    highDPI_(false),
    vsync_(false),
    tripleBuffer_(false),
    sRGB_(false),
    forceGL2_(false),
    instancingSupport_(false),
    lightPrepassSupport_(false),
    deferredSupport_(false),
    anisotropySupport_(false),
    dxtTextureSupport_(false),
    etcTextureSupport_(false),
    pvrtcTextureSupport_(false),
    hardwareShadowSupport_(false),
    sRGBSupport_(false),
    sRGBWriteSupport_(false),
    numPrimitives_(0),
    numBatches_(0),
    maxScratchBufferRequest_(0),
    dummyColorFormat_(0),
    shadowMapFormat_(0),
    hiresShadowMapFormat_(0),
    defaultTextureFilterMode_(FILTER_TRILINEAR),
    defaultTextureAnisotropy_(4),
    shaderPath_("Shaders/GLSL/"),  // glsl can be compiled to Spir-V
    shaderExtension_(".glsl"),
    orientations_("LandscapeLeft LandscapeRight"),
    apiName_("GL2")
{
    SetTextureUnitMappings();
    ResetCachedState();
}

// ----------------------------------------------------------------------------
Graphics::~Graphics()
{
    Close();

    delete impl_;
    impl_ = 0;
}

// ----------------------------------------------------------------------------
bool Graphics::SetMode(int width, int height, bool fullscreen, bool borderless, bool resizable, bool highDPI, bool vsync,
    bool tripleBuffer, int multiSample)
{
    URHO3D_PROFILE(SetScreenMode);

    bool maximize = false;

    // Fullscreen or Borderless can not be resizable
    if (fullscreen || borderless)
        resizable = false;

    // Borderless cannot be fullscreen, they are mutually exclusive
    if (borderless)
        fullscreen = false;

    multiSample = Clamp(multiSample, 1, 16);

    if (IsInitialized() && width == width_ && height == height_ && fullscreen == fullscreen_ && borderless == borderless_ &&
        resizable == resizable_ && vsync == vsync_ && tripleBuffer == tripleBuffer_ && multiSample == multiSample_)
        return true;

    // If only vsync changes, do not destroy/recreate the context
    if (IsInitialized() && width == width_ && height == height_ && fullscreen == fullscreen_ && borderless == borderless_ &&
        resizable == resizable_ && tripleBuffer == tripleBuffer_ && multiSample == multiSample_ && vsync != vsync_)
    {
        // TODO vsync change
        vsync_ = vsync;
        return true;
    }

    // If zero dimensions in windowed mode, set windowed mode to maximize and
    // set a predefined default restored window size. If zero in fullscreen,
    // use desktop mode
    if (!width || !height)
    {
        // TODO Maximize window
    }

    // Check fullscreen mode validity (desktop only). Use a closest match if not found
#ifdef DESKTOP_GRAPHICS
    if (fullscreen)
    {
        PODVector<IntVector2> resolutions = GetResolutions();
        if (resolutions.Size())
        {
            unsigned best = 0;
            unsigned bestError = M_MAX_UNSIGNED;

            for (unsigned i = 0; i < resolutions.Size(); ++i)
            {
                unsigned error = Abs(resolutions[i].x_ - width) + Abs(resolutions[i].y_ - height);
                if (error < bestError)
                {
                    best = i;
                    bestError = error;
                }
            }

            width = resolutions[best].x_;
            height = resolutions[best].y_;
        }
    }
#endif

    // With an external window, only the size can change after initial setup, so do not recreate context
    if (externalWindow_)
    {
        // TODO handle external window
    }

    fullscreen_ = fullscreen;
    borderless_ = borderless;
    resizable_ = resizable;
    highDPI_ = highDPI;
    vsync_ = vsync;
    tripleBuffer_ = tripleBuffer;
    multiSample_ = multiSample;

#ifdef URHO3D_LOGGING
    String msg;
    msg.AppendWithFormat("Set screen mode %dx%d %s", width_, height_, (fullscreen_ ? "fullscreen" : "windowed"));
    if (borderless_)
        msg.Append(" borderless");
    if (resizable_)
        msg.Append(" resizable");
    if (multiSample > 1)
        msg.AppendWithFormat(" multisample %d", multiSample);
    URHO3D_LOGINFO(msg);
#endif

    using namespace ScreenMode;

    VariantMap& eventData = GetEventDataMap();
    eventData[P_WIDTH] = width_;
    eventData[P_HEIGHT] = height_;
    eventData[P_FULLSCREEN] = fullscreen_;
    eventData[P_BORDERLESS] = borderless_;
    eventData[P_RESIZABLE] = resizable_;
    eventData[P_HIGHDPI] = highDPI_;
    SendEvent(E_SCREENMODE, eventData);

    return true;
}

// ----------------------------------------------------------------------------
bool Graphics::SetMode(int width, int height)
{
    return SetMode(width, height, fullscreen_, borderless_, resizable_, highDPI_, vsync_, tripleBuffer_, multiSample_);
}

// ----------------------------------------------------------------------------
void Graphics::SetSRGB(bool enable)
{
}

// ----------------------------------------------------------------------------
void Graphics::SetDither(bool enable)
{
}

// ----------------------------------------------------------------------------
void Graphics::SetFlushGPU(bool enable)
{
}

// ----------------------------------------------------------------------------
void Graphics::SetForceGL2(bool enable)
{
}

// ----------------------------------------------------------------------------
void Graphics::Close()
{
    if (!IsInitialized())
        return;

    // Actually close the window
    Release(true, true);
}

// ----------------------------------------------------------------------------
bool Graphics::TakeScreenShot(Image& destImage)
{
    URHO3D_PROFILE(TakeScreenShot);

    if (!IsInitialized())
        return false;

    if (IsDeviceLost())
    {
        URHO3D_LOGERROR("Can not take screenshot while device is lost");
        return false;
    }

    ResetRenderTargets();

    // On OpenGL we need to flip the image vertically after reading
    destImage.FlipVertical();

    return true;
}

// ----------------------------------------------------------------------------
bool Graphics::BeginFrame()
{
    if (!IsInitialized() || IsDeviceLost())
        return false;

    // If using an external window, check it for size changes, and reset screen mode if necessary
    if (externalWindow_)
    {
        // TODO SetMode() if size changed.
    }

    // Set default rendertarget and depth buffer
    ResetRenderTargets();

    // Cleanup textures from previous frame
    for (unsigned i = 0; i < MAX_TEXTURE_UNITS; ++i)
        SetTexture(i, 0);

    // Enable color and depth write
    SetColorWrite(true);
    SetDepthWrite(true);

    numPrimitives_ = 0;
    numBatches_ = 0;

    SendEvent(E_BEGINRENDERING);

    return true;
}

// ----------------------------------------------------------------------------
void Graphics::EndFrame()
{
    if (!IsInitialized())
        return;

    URHO3D_PROFILE(Present);

    SendEvent(E_ENDRENDERING);

    // TODO Swap chain

    // Clean up too large scratch buffers
    CleanupScratchBuffers();
}

// ----------------------------------------------------------------------------
void Graphics::Clear(unsigned flags, const Color& color, float depth, unsigned stencil)
{
    PrepareDraw();

    bool oldColorWrite = colorWrite_;
    bool oldDepthWrite = depthWrite_;

    if (flags & CLEAR_COLOR && !oldColorWrite)
        SetColorWrite(true);
    if (flags & CLEAR_DEPTH && !oldDepthWrite)
        SetDepthWrite(true);
}

// ----------------------------------------------------------------------------
bool Graphics::ResolveToTexture(Texture2D* destination, const IntRect& viewport)
{
    if (!destination || !destination->GetRenderSurface())
        return false;

    URHO3D_PROFILE(ResolveToTexture);

    IntRect vpCopy = viewport;
    if (vpCopy.right_ <= vpCopy.left_)
        vpCopy.right_ = vpCopy.left_ + 1;
    if (vpCopy.bottom_ <= vpCopy.top_)
        vpCopy.bottom_ = vpCopy.top_ + 1;
    vpCopy.left_ = Clamp(vpCopy.left_, 0, width_);
    vpCopy.top_ = Clamp(vpCopy.top_, 0, height_);
    vpCopy.right_ = Clamp(vpCopy.right_, 0, width_);
    vpCopy.bottom_ = Clamp(vpCopy.bottom_, 0, height_);

    // Make sure the FBO is not in use
    ResetRenderTargets();

    // Use Direct3D convention with the vertical coordinates ie. 0 is top
    SetTextureForUpdate(destination);
    //glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, vpCopy.left_, height_ - vpCopy.bottom_, vpCopy.Width(), vpCopy.Height());
    SetTexture(0, 0);

    return true;
}

// ----------------------------------------------------------------------------
bool Graphics::ResolveToTexture(Texture2D* texture)
{
    if (!texture)
        return false;
    RenderSurface* surface = texture->GetRenderSurface();
    if (!surface || !surface->GetRenderBuffer())
        return false;

    URHO3D_PROFILE(ResolveToTexture);

    texture->SetResolveDirty(false);
    surface->SetResolveDirty(false);

    // Use separate FBOs for resolve to not disturb the currently set rendertarget(s)
    if (!impl_->resolveSrcFBO_)
        impl_->resolveSrcFBO_ = CreateFramebuffer();
    if (!impl_->resolveDestFBO_)
        impl_->resolveDestFBO_ = CreateFramebuffer();

    // Restore previously bound FBO
    BindFramebuffer(impl_->boundFBO_);
    return true;
}

// ----------------------------------------------------------------------------
bool Graphics::ResolveToTexture(TextureCube* texture)
{
    if (!texture)
        return false;

    URHO3D_PROFILE(ResolveToTexture);

    texture->SetResolveDirty(false);

    // Use separate FBOs for resolve to not disturb the currently set rendertarget(s)
    if (!impl_->resolveSrcFBO_)
        impl_->resolveSrcFBO_ = CreateFramebuffer();
    if (!impl_->resolveDestFBO_)
        impl_->resolveDestFBO_ = CreateFramebuffer();

    // Restore previously bound FBO
    BindFramebuffer(impl_->boundFBO_);
    return true;
}

// ----------------------------------------------------------------------------
void Graphics::Draw(PrimitiveType type, unsigned vertexStart, unsigned vertexCount)
{
    if (!vertexCount)
        return;

    PrepareDraw();

    //numPrimitives_ += primitiveCount;
    ++numBatches_;
}

// ----------------------------------------------------------------------------
void Graphics::Draw(PrimitiveType type, unsigned indexStart, unsigned indexCount, unsigned minVertex, unsigned vertexCount)
{
    if (!indexCount || !indexBuffer_ || !indexBuffer_->GetGPUObjectName())
        return;

    PrepareDraw();

    unsigned indexSize = indexBuffer_->GetIndexSize();

    //numPrimitives_ += primitiveCount;
    ++numBatches_;
}

// ----------------------------------------------------------------------------
void Graphics::Draw(PrimitiveType type, unsigned indexStart, unsigned indexCount, unsigned baseVertexIndex, unsigned minVertex, unsigned vertexCount)
{
    if (!gl3Support || !indexCount || !indexBuffer_ || !indexBuffer_->GetGPUObjectName())
        return;

    PrepareDraw();

    unsigned indexSize = indexBuffer_->GetIndexSize();

    //numPrimitives_ += primitiveCount;
    ++numBatches_;
}

// ----------------------------------------------------------------------------
void Graphics::DrawInstanced(PrimitiveType type, unsigned indexStart, unsigned indexCount, unsigned minVertex, unsigned vertexCount,
    unsigned instanceCount)
{
    if (!indexCount || !indexBuffer_ || !indexBuffer_->GetGPUObjectName() || !instancingSupport_)
        return;

    PrepareDraw();

    //numPrimitives_ += instanceCount * primitiveCount;
    ++numBatches_;
}

// ----------------------------------------------------------------------------
void Graphics::DrawInstanced(PrimitiveType type, unsigned indexStart, unsigned indexCount, unsigned baseVertexIndex, unsigned minVertex,
        unsigned vertexCount, unsigned instanceCount)
{
    PrepareDraw();

    //numPrimitives_ += instanceCount * primitiveCount;
    ++numBatches_;
}

// ----------------------------------------------------------------------------
void Graphics::SetVertexBuffer(VertexBuffer* buffer)
{
    // Note: this is not multi-instance safe
    static PODVector<VertexBuffer*> vertexBuffers(1);
    vertexBuffers[0] = buffer;
    SetVertexBuffers(vertexBuffers);
}

// ----------------------------------------------------------------------------
bool Graphics::SetVertexBuffers(const PODVector<VertexBuffer*>& buffers, unsigned instanceOffset)
{
}

// ----------------------------------------------------------------------------
bool Graphics::SetVertexBuffers(const Vector<SharedPtr<VertexBuffer> >& buffers, unsigned instanceOffset)
{
    return SetVertexBuffers(reinterpret_cast<const PODVector<VertexBuffer*>&>(buffers), instanceOffset);
}

// ----------------------------------------------------------------------------
void Graphics::SetIndexBuffer(IndexBuffer* buffer)
{
    if (indexBuffer_ == buffer)
        return;

    indexBuffer_ = buffer;
}

// ----------------------------------------------------------------------------
void Graphics::SetShaders(ShaderVariation* vs, ShaderVariation* ps)
{
}

// ----------------------------------------------------------------------------
void Graphics::SetShaderParameter(StringHash param, const float* data, unsigned count)
{
}

// ----------------------------------------------------------------------------
void Graphics::SetShaderParameter(StringHash param, float value)
{
}

// ----------------------------------------------------------------------------
void Graphics::SetShaderParameter(StringHash param, int value)
{
}

// ----------------------------------------------------------------------------
void Graphics::SetShaderParameter(StringHash param, bool value)
{
}

// ----------------------------------------------------------------------------
void Graphics::SetShaderParameter(StringHash param, const Color& color)
{
    SetShaderParameter(param, color.Data(), 4);
}

// ----------------------------------------------------------------------------
void Graphics::SetShaderParameter(StringHash param, const Vector2& vector)
{
}

// ----------------------------------------------------------------------------
void Graphics::SetShaderParameter(StringHash param, const Matrix3& matrix)
{
}

// ----------------------------------------------------------------------------
void Graphics::SetShaderParameter(StringHash param, const Vector3& vector)
{
}

// ----------------------------------------------------------------------------
void Graphics::SetShaderParameter(StringHash param, const Matrix4& matrix)
{
}

// ----------------------------------------------------------------------------
void Graphics::SetShaderParameter(StringHash param, const Vector4& vector)
{
}

// ----------------------------------------------------------------------------
void Graphics::SetShaderParameter(StringHash param, const Matrix3x4& matrix)
{
}

// ----------------------------------------------------------------------------
bool Graphics::NeedParameterUpdate(ShaderParameterGroup group, const void* source)
{
    return impl_->shaderProgram_ ? impl_->shaderProgram_->NeedParameterUpdate(group, source) : false;
}

// ----------------------------------------------------------------------------
bool Graphics::HasShaderParameter(StringHash param)
{
    return impl_->shaderProgram_ && impl_->shaderProgram_->HasParameter(param);
}

// ----------------------------------------------------------------------------
bool Graphics::HasTextureUnit(TextureUnit unit)
{
    return impl_->shaderProgram_ && impl_->shaderProgram_->HasTextureUnit(unit);
}

// ----------------------------------------------------------------------------
void Graphics::ClearParameterSource(ShaderParameterGroup group)
{
    if (impl_->shaderProgram_)
        impl_->shaderProgram_->ClearParameterSource(group);
}

// ----------------------------------------------------------------------------
void Graphics::ClearParameterSources()
{
    ShaderProgram::ClearParameterSources();
}

// ----------------------------------------------------------------------------
void Graphics::ClearTransformSources()
{
    if (impl_->shaderProgram_)
    {
        impl_->shaderProgram_->ClearParameterSource(SP_CAMERA);
        impl_->shaderProgram_->ClearParameterSource(SP_OBJECT);
    }
}

// ----------------------------------------------------------------------------
void Graphics::SetTexture(unsigned index, Texture* texture)
{
}

// ----------------------------------------------------------------------------
void Graphics::SetTextureForUpdate(Texture* texture)
{
}

// ----------------------------------------------------------------------------
void Graphics::SetDefaultTextureFilterMode(TextureFilterMode mode)
{
}

// ----------------------------------------------------------------------------
void Graphics::SetDefaultTextureAnisotropy(unsigned level)
{
}

// ----------------------------------------------------------------------------
void Graphics::SetTextureParametersDirty()
{
}

// ----------------------------------------------------------------------------
void Graphics::ResetRenderTargets()
{
    for (unsigned i = 0; i < MAX_RENDERTARGETS; ++i)
        SetRenderTarget(i, (RenderSurface*)0);
    SetDepthStencil((RenderSurface*)0);
    SetViewport(IntRect(0, 0, width_, height_));
}

// ----------------------------------------------------------------------------
void Graphics::ResetRenderTarget(unsigned index)
{
    SetRenderTarget(index, (RenderSurface*)0);
}

// ----------------------------------------------------------------------------
void Graphics::ResetDepthStencil()
{
    SetDepthStencil((RenderSurface*)0);
}

// ----------------------------------------------------------------------------
void Graphics::SetRenderTarget(unsigned index, RenderSurface* renderTarget)
{
}

// ----------------------------------------------------------------------------
void Graphics::SetRenderTarget(unsigned index, Texture2D* texture)
{
    RenderSurface* renderTarget = 0;
    if (texture)
        renderTarget = texture->GetRenderSurface();

    SetRenderTarget(index, renderTarget);
}

// ----------------------------------------------------------------------------
void Graphics::SetDepthStencil(RenderSurface* depthStencil)
{
}

// ----------------------------------------------------------------------------
void Graphics::SetDepthStencil(Texture2D* texture)
{
    RenderSurface* depthStencil = 0;
    if (texture)
        depthStencil = texture->GetRenderSurface();

    SetDepthStencil(depthStencil);
}

// ----------------------------------------------------------------------------
void Graphics::SetViewport(const IntRect& rect)
{
}

// ----------------------------------------------------------------------------
void Graphics::SetBlendMode(BlendMode mode, bool alphaToCoverage)
{
}

// ----------------------------------------------------------------------------
void Graphics::SetColorWrite(bool enable)
{
}

// ----------------------------------------------------------------------------
void Graphics::SetCullMode(CullMode mode)
{
}

// ----------------------------------------------------------------------------
void Graphics::SetDepthBias(float constantBias, float slopeScaledBias)
{
}

// ----------------------------------------------------------------------------
void Graphics::SetDepthTest(CompareMode mode)
{
}

// ----------------------------------------------------------------------------
void Graphics::SetDepthWrite(bool enable)
{
}

// ----------------------------------------------------------------------------
void Graphics::SetFillMode(FillMode mode)
{
}

// ----------------------------------------------------------------------------
void Graphics::SetLineAntiAlias(bool enable)
{
}

// ----------------------------------------------------------------------------
void Graphics::SetScissorTest(bool enable, const Rect& rect, bool borderInclusive)
{
}

// ----------------------------------------------------------------------------
void Graphics::SetScissorTest(bool enable, const IntRect& rect)
{
}

// ----------------------------------------------------------------------------
void Graphics::SetClipPlane(bool enable, const Plane& clipPlane, const Matrix3x4& view, const Matrix4& projection)
{
}

// ----------------------------------------------------------------------------
void Graphics::SetStencilTest(bool enable, CompareMode mode, StencilOp pass, StencilOp fail, StencilOp zFail, unsigned stencilRef,
    unsigned compareMask, unsigned writeMask)
{
}

// ----------------------------------------------------------------------------
bool Graphics::IsInitialized() const
{
    return window_ != 0;
}

// ----------------------------------------------------------------------------
bool Graphics::GetDither() const
{
}

// ----------------------------------------------------------------------------
bool Graphics::IsDeviceLost() const
{
}

// ----------------------------------------------------------------------------
PODVector<int> Graphics::GetMultiSampleLevels() const
{
}

// ----------------------------------------------------------------------------
unsigned Graphics::GetFormat(CompressedFormat format) const
{
}

// ----------------------------------------------------------------------------
unsigned Graphics::GetMaxBones()
{
#ifdef RPI
    // At the moment all RPI GPUs are low powered and only have limited number of uniforms
    return 32;
#else
    return gl3Support ? 128 : 64;
#endif
}

// ----------------------------------------------------------------------------
bool Graphics::GetGL3Support()
{
    return gl3Support;
}

// ----------------------------------------------------------------------------
ShaderVariation* Graphics::GetShader(ShaderType type, const String& name, const String& defines) const
{
    return GetShader(type, name.CString(), defines.CString());
}

// ----------------------------------------------------------------------------
ShaderVariation* Graphics::GetShader(ShaderType type, const char* name, const char* defines) const
{
    if (lastShaderName_ != name || !lastShader_)
    {
        ResourceCache* cache = GetSubsystem<ResourceCache>();

        String fullShaderName = shaderPath_ + name + shaderExtension_;
        // Try to reduce repeated error log prints because of missing shaders
        if (lastShaderName_ == name && !cache->Exists(fullShaderName))
            return 0;

        lastShader_ = cache->GetResource<Shader>(fullShaderName);
        lastShaderName_ = name;
    }

    return lastShader_ ? lastShader_->GetVariation(type, defines) : (ShaderVariation*)0;
}

// ----------------------------------------------------------------------------
VertexBuffer* Graphics::GetVertexBuffer(unsigned index) const
{
    return index < MAX_VERTEX_STREAMS ? vertexBuffers_[index] : 0;
}

// ----------------------------------------------------------------------------
ShaderProgram* Graphics::GetShaderProgram() const
{
    return impl_->shaderProgram_;
}

// ----------------------------------------------------------------------------
TextureUnit Graphics::GetTextureUnit(const String& name)
{
    HashMap<String, TextureUnit>::Iterator i = textureUnits_.Find(name);
    if (i != textureUnits_.End())
        return i->second_;
    else
        return MAX_TEXTURE_UNITS;
}

// ----------------------------------------------------------------------------
const String& Graphics::GetTextureUnitName(TextureUnit unit)
{
    for (HashMap<String, TextureUnit>::Iterator i = textureUnits_.Begin(); i != textureUnits_.End(); ++i)
    {
        if (i->second_ == unit)
            return i->first_;
    }
    return String::EMPTY;
}

// ----------------------------------------------------------------------------
Texture* Graphics::GetTexture(unsigned index) const
{
    return index < MAX_TEXTURE_UNITS ? textures_[index] : 0;
}

// ----------------------------------------------------------------------------
RenderSurface* Graphics::GetRenderTarget(unsigned index) const
{
    return index < MAX_RENDERTARGETS ? renderTargets_[index] : 0;
}

// ----------------------------------------------------------------------------
IntVector2 Graphics::GetRenderTargetDimensions() const
{
    int width, height;

    if (renderTargets_[0])
    {
        width = renderTargets_[0]->GetWidth();
        height = renderTargets_[0]->GetHeight();
    }
    else if (depthStencil_)
    {
        width = depthStencil_->GetWidth();
        height = depthStencil_->GetHeight();
    }
    else
    {
        width = width_;
        height = height_;
    }

    return IntVector2(width, height);
}

// ----------------------------------------------------------------------------
void Graphics::OnWindowResized()
{
}

// ----------------------------------------------------------------------------
void Graphics::OnWindowMoved()
{
}

// ----------------------------------------------------------------------------
void Graphics::CleanupRenderSurface(RenderSurface* surface)
{
}

// ----------------------------------------------------------------------------
void Graphics::CleanupShaderPrograms(ShaderVariation* variation)
{
    for (ShaderProgramMap::Iterator i = impl_->shaderPrograms_.Begin(); i != impl_->shaderPrograms_.End();)
    {
        if (i->second_->GetVertexShader() == variation || i->second_->GetPixelShader() == variation)
            i = impl_->shaderPrograms_.Erase(i);
        else
            ++i;
    }

    if (vertexShader_ == variation || pixelShader_ == variation)
        impl_->shaderProgram_ = 0;
}

// ----------------------------------------------------------------------------
ConstantBuffer* Graphics::GetOrCreateConstantBuffer(ShaderType /*type*/,  unsigned bindingIndex, unsigned size)
{
}

// ----------------------------------------------------------------------------
void Graphics::Release(bool clearGPUObjects, bool closeWindow)
{
}

// ----------------------------------------------------------------------------
void Graphics::Restore()
{
}

// ----------------------------------------------------------------------------
void Graphics::MarkFBODirty()
{
    impl_->fboDirty_ = true;
}

// ----------------------------------------------------------------------------
void Graphics::SetVBO(unsigned object)
{
}

// ----------------------------------------------------------------------------
void Graphics::SetUBO(unsigned object)
{
}

// ----------------------------------------------------------------------------
unsigned Graphics::GetAlphaFormat()
{
}

// ----------------------------------------------------------------------------
unsigned Graphics::GetLuminanceFormat()
{
}

// ----------------------------------------------------------------------------
unsigned Graphics::GetLuminanceAlphaFormat()
{
}

// ----------------------------------------------------------------------------
unsigned Graphics::GetRGBFormat()
{
}

// ----------------------------------------------------------------------------
unsigned Graphics::GetRGBAFormat()
{
}

// ----------------------------------------------------------------------------
unsigned Graphics::GetRGBA16Format()
{
}

// ----------------------------------------------------------------------------
unsigned Graphics::GetRGBAFloat16Format()
{
}

// ----------------------------------------------------------------------------
unsigned Graphics::GetRGBAFloat32Format()
{
}

// ----------------------------------------------------------------------------
unsigned Graphics::GetRG16Format()
{
}

// ----------------------------------------------------------------------------
unsigned Graphics::GetRGFloat16Format()
{
}

// ----------------------------------------------------------------------------
unsigned Graphics::GetRGFloat32Format()
{
}

// ----------------------------------------------------------------------------
unsigned Graphics::GetFloat16Format()
{
}

// ----------------------------------------------------------------------------
unsigned Graphics::GetFloat32Format()
{
}

// ----------------------------------------------------------------------------
unsigned Graphics::GetLinearDepthFormat()
{
}

// ----------------------------------------------------------------------------
unsigned Graphics::GetDepthStencilFormat()
{
}

// ----------------------------------------------------------------------------
unsigned Graphics::GetReadableDepthFormat()
{
}

// ----------------------------------------------------------------------------
unsigned Graphics::GetFormat(const String& formatName)
{
    String nameLower = formatName.ToLower().Trimmed();

    if (nameLower == "a")
        return GetAlphaFormat();
    if (nameLower == "l")
        return GetLuminanceFormat();
    if (nameLower == "la")
        return GetLuminanceAlphaFormat();
    if (nameLower == "rgb")
        return GetRGBFormat();
    if (nameLower == "rgba")
        return GetRGBAFormat();
    if (nameLower == "rgba16")
        return GetRGBA16Format();
    if (nameLower == "rgba16f")
        return GetRGBAFloat16Format();
    if (nameLower == "rgba32f")
        return GetRGBAFloat32Format();
    if (nameLower == "rg16")
        return GetRG16Format();
    if (nameLower == "rg16f")
        return GetRGFloat16Format();
    if (nameLower == "rg32f")
        return GetRGFloat32Format();
    if (nameLower == "r16f")
        return GetFloat16Format();
    if (nameLower == "r32f" || nameLower == "float")
        return GetFloat32Format();
    if (nameLower == "lineardepth" || nameLower == "depth")
        return GetLinearDepthFormat();
    if (nameLower == "d24s8")
        return GetDepthStencilFormat();
    if (nameLower == "readabledepth" || nameLower == "hwdepth")
        return GetReadableDepthFormat();

    return GetRGBFormat();
}

// ----------------------------------------------------------------------------
void Graphics::CheckFeatureSupport()
{
}

// ----------------------------------------------------------------------------
void Graphics::PrepareDraw()
{
}

// ----------------------------------------------------------------------------
void Graphics::CleanupFramebuffers()
{
    if (!IsDeviceLost())
    {
        BindFramebuffer(impl_->systemFBO_);
        impl_->boundFBO_ = impl_->systemFBO_;
        impl_->fboDirty_ = true;

        for (HashMap<unsigned long long, FrameBufferObject>::Iterator i = impl_->frameBuffers_.Begin();
             i != impl_->frameBuffers_.End(); ++i)
            DeleteFramebuffer(i->second_.fbo_);

        if (impl_->resolveSrcFBO_)
            DeleteFramebuffer(impl_->resolveSrcFBO_);
        if (impl_->resolveDestFBO_)
            DeleteFramebuffer(impl_->resolveDestFBO_);
    }
    else
    {
        impl_->boundFBO_ = 0;
        impl_->resolveSrcFBO_ = 0;
        impl_->resolveDestFBO_ = 0;
    }

    impl_->frameBuffers_.Clear();
}

// ----------------------------------------------------------------------------
void Graphics::ResetCachedState()
{
    for (unsigned i = 0; i < MAX_VERTEX_STREAMS; ++i)
        vertexBuffers_[i] = 0;

    for (unsigned i = 0; i < MAX_TEXTURE_UNITS; ++i)
    {
        textures_[i] = 0;
        impl_->textureTypes_[i] = 0;
    }

    for (unsigned i = 0; i < MAX_RENDERTARGETS; ++i)
        renderTargets_[i] = 0;

    depthStencil_ = 0;
    viewport_ = IntRect(0, 0, 0, 0);
    indexBuffer_ = 0;
    vertexShader_ = 0;
    pixelShader_ = 0;
    blendMode_ = BLEND_REPLACE;
    alphaToCoverage_ = false;
    colorWrite_ = true;
    cullMode_ = CULL_NONE;
    constantDepthBias_ = 0.0f;
    slopeScaledDepthBias_ = 0.0f;
    depthTestMode_ = CMP_ALWAYS;
    depthWrite_ = false;
    lineAntiAlias_ = false;
    fillMode_ = FILL_SOLID;
    scissorTest_ = false;
    scissorRect_ = IntRect::ZERO;
    stencilTest_ = false;
    stencilTestMode_ = CMP_ALWAYS;
    stencilPass_ = OP_KEEP;
    stencilFail_ = OP_KEEP;
    stencilZFail_ = OP_KEEP;
    stencilRef_ = 0;
    stencilCompareMask_ = M_MAX_UNSIGNED;
    stencilWriteMask_ = M_MAX_UNSIGNED;
    useClipPlane_ = false;
    impl_->shaderProgram_ = 0;
    impl_->lastInstanceOffset_ = 0;
    impl_->activeTexture_ = 0;
    impl_->enabledVertexAttributes_ = 0;
    impl_->usedVertexAttributes_ = 0;
    impl_->instancingVertexAttributes_ = 0;
    impl_->boundFBO_ = impl_->systemFBO_;
    impl_->boundVBO_ = 0;
    impl_->boundUBO_ = 0;
    impl_->sRGBWrite_ = false;

    // Set initial state to match Direct3D
    //if (impl_->context_)
    {
        // TODO
    }

    for (unsigned i = 0; i < MAX_SHADER_PARAMETER_GROUPS * 2; ++i)
        impl_->constantBuffers_[i] = 0;
    impl_->dirtyConstantBuffers_.Clear();
}

// ----------------------------------------------------------------------------
void Graphics::SetTextureUnitMappings()
{
    textureUnits_["DiffMap"] = TU_DIFFUSE;
    textureUnits_["DiffCubeMap"] = TU_DIFFUSE;
    textureUnits_["AlbedoBuffer"] = TU_ALBEDOBUFFER;
    textureUnits_["NormalMap"] = TU_NORMAL;
    textureUnits_["NormalBuffer"] = TU_NORMALBUFFER;
    textureUnits_["SpecMap"] = TU_SPECULAR;
    textureUnits_["EmissiveMap"] = TU_EMISSIVE;
    textureUnits_["EnvMap"] = TU_ENVIRONMENT;
    textureUnits_["EnvCubeMap"] = TU_ENVIRONMENT;
    textureUnits_["LightRampMap"] = TU_LIGHTRAMP;
    textureUnits_["LightSpotMap"] = TU_LIGHTSHAPE;
    textureUnits_["LightCubeMap"] = TU_LIGHTSHAPE;
    textureUnits_["ShadowMap"] = TU_SHADOWMAP;
    textureUnits_["VolumeMap"] = TU_VOLUMEMAP;
    textureUnits_["FaceSelectCubeMap"] = TU_FACESELECT;
    textureUnits_["IndirectionCubeMap"] = TU_INDIRECTION;
    textureUnits_["DepthBuffer"] = TU_DEPTHBUFFER;
    textureUnits_["LightBuffer"] = TU_LIGHTBUFFER;
    textureUnits_["ZoneCubeMap"] = TU_ZONE;
    textureUnits_["ZoneVolumeMap"] = TU_ZONE;
}

// ----------------------------------------------------------------------------
unsigned Graphics::CreateFramebuffer()
{
}

// ----------------------------------------------------------------------------
void Graphics::DeleteFramebuffer(unsigned fbo)
{
}

// ----------------------------------------------------------------------------
void Graphics::BindFramebuffer(unsigned fbo)
{
}

// ----------------------------------------------------------------------------
void Graphics::BindColorAttachment(unsigned index, unsigned target, unsigned object, bool isRenderBuffer)
{
}

// ----------------------------------------------------------------------------
void Graphics::BindDepthAttachment(unsigned object, bool isRenderBuffer)
{
}

// ----------------------------------------------------------------------------
void Graphics::BindStencilAttachment(unsigned object, bool isRenderBuffer)
{
}

// ----------------------------------------------------------------------------
bool Graphics::CheckFramebuffer()
{
}

// ----------------------------------------------------------------------------
void Graphics::SetVertexAttribDivisor(unsigned location, unsigned divisor)
{
}

}
