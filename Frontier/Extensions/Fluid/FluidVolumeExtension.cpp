/*==========================================================================================================================================
                                                      FLUIDVOLUMEEXTENSION.CPP
==========================================================================================================================================*/
// 🧩 One refresh of the Fluid editor's viewport: the solver stepped and drawn (PyroSolverSession), the picture written into the texture the editor's
//    interface samples.

#include "Frontier/Extensions/Fluid/FluidVolumeExtension.h"

#include <chrono>

namespace Frontier
{

FluidVolumeExtension::FluidVolumeExtension(const VulkanDeviceAccess* HostDevice)
{
    if (HostDevice != nullptr && HostDevice->Device != VK_NULL_HANDLE)
    {
        Device_             = *HostDevice;
        HostDeviceAttached_ = true;
    }
}

FluidVolumeExtension::~FluidVolumeExtension() = default;

bool FluidVolumeExtension::ConstructExchange(std::string& Diagnostic)
{
    Exchange_ = std::make_unique<DeviceExchange>();
    return HostDeviceAttached_ ? Exchange_->AttachDevice(Device_, &Diagnostic) : Exchange_->Initialize("", &Diagnostic);
}

bool FluidVolumeExtension::ConstructPicture(std::array<uint32_t, 2> Picture, std::string& Diagnostic)
{
    TextureSpecification Specification;
    Specification.Label  = "Fluid picture";
    Specification.Width  = Picture[0];
    Specification.Height = Picture[1];
    Specification.Format = TextureFormat::Rgba8Unorm;
    Specification.Usage  = TextureUsage::Sampled | TextureUsage::CopyTarget | TextureUsage::CopySource;
    Presented_           = Exchange_->ConstructTexture(Specification);
    PresentedView_       = Exchange_->ConstructView(Presented_);
    if (!Presented_ || !PresentedView_)
    {
        Diagnostic = "the picture's texture could not be made";
        return false;
    }
    Picture_ = Picture;
    return true;
}

bool FluidVolumeExtension::ReadPresentedImage(std::vector<uint8_t>& Rgba, std::string* Diagnostic)
{
    if (!Presented_ || !Exchange_->ReadTexture(Presented_, 0, 0, Rgba))
    {
        if (Diagnostic) *Diagnostic = "the Fluid picture could not be read back";
        return false;
    }
    return true;
}

FluidViewportOutcome FluidVolumeExtension::Render(const FluidViewportRequest& Request)
{
    FluidViewportOutcome Outcome;
    if (Request.Settings == nullptr || Request.Extent[0] < 32 || Request.Extent[1] < 32)
    {
        Outcome.Image.Failure = "There is no picture to draw.";
        return Outcome;
    }
    const auto Begun = std::chrono::steady_clock::now();
    // 📝 On the editor's queue the previous interface refresh may still sample the presented picture; this refresh replaces what it needs to.
    if (HostDeviceAttached_ && Exchange_) Exchange_->AwaitIdle();
    std::string Diagnostic;
    if (Failure_.empty() && Exchange_ == nullptr && !ConstructExchange(Diagnostic)) Failure_ = "The Fluid picture's device failed: " + Diagnostic;
    if (!Failure_.empty())
    {
        Outcome.Image.Failure = Failure_;
        return Outcome;
    }

    std::array<uint32_t, 2> Picture{};
    FluidVolumeReadout      Readout{};
    std::string             Failure;
    if (!Solver_.Refresh(Request, Pixels_, Picture, Readout, Failure))
    {
        Outcome.Image.Failure = Failure;
        return Outcome;
    }
    if (Picture != Picture_ || Presented_ == nullptr)
    {
        if (!ConstructPicture(Picture, Diagnostic))
        {
            Failure_              = "The Fluid picture failed: " + Diagnostic;
            Outcome.Image.Failure = Failure_;
            return Outcome;
        }
    }
    Exchange_->WriteTexture(Presented_, 0, Pixels_.data(), Pixels_.size(), Picture_[0], Picture_[1]);
    CommandSequence Landing = Exchange_->BeginSequence();   // 📝 an empty sequence: the queued write lands before the next submission, and the submission waits
    if (!Exchange_->Submit(Landing, &Diagnostic))
    {
        Outcome.Image.Failure = "The Fluid picture could not be written: " + Diagnostic;
        return Outcome;
    }

    Outcome.View                      = PresentedView_->Handle;
    Outcome.Image.Extent              = Picture_;
    Outcome.Image.Adapter             = Solver_.QueryAdapter();
    Outcome.Image.Solving             = true;
    Outcome.Image.Readout             = Readout;
    Outcome.Image.RefreshMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Begun).count();
    return Outcome;
}

} // namespace Frontier
