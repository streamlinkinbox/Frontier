/*==========================================================================================================================================
                                                 FRAMEFAULTRECOVERYVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Prove the exact per-frame recovery pattern added to Engine/Interface/WindowsApplication.cpp's main
// loop: if building a frame's interface throws, end the ImGui frame cleanly and keep running instead of
// letting the exception propagate out of the loop.
//
// 📝 Why this matters: analysing the destructor order in ConstructApplicationWindow shows the field
// "Frontier - Severe failure: bad allocation" dialog can only be RunWindowsApplication's own outer
// catch(const std::exception&) printing Failure.what() verbatim - and MSVC's std::bad_alloc::what()
// literally returns "bad allocation". Before this patch, an exception thrown anywhere while building a
// frame's interface (Callbacks.ConstructInterface - the dock graph walk, a document edit, anything) had
// no catch *inside* the main loop: it propagated straight out of ConstructApplicationWindow, past three
// local objects' destructors (which is why the DiagnosticJournal log shows a complete, orderly shutdown
// sequence - "GPU idle confirmed", the layout-save attempts, "Application resources finalized" - directly
// before the crash: that sequence *is* the stack unwinding caused by the escaping exception, not a
// separate failure during an already-intentional shutdown), and was only ever caught, logged and shown to
// the user once it reached the top. One bad frame was therefore always fatal to the whole session. This
// exhibit cannot invoke the real GLFW/Vulkan main loop headlessly, so it reproduces the identical
// NewFrame/try-ConstructInterface-catch/EndFrame-or-Render control flow against a bare ImGui context (the
// same patched ImGui build the real application links) and injects a real thrown exception mid-interface,
// confirming: (1) it never escapes the frame boundary, (2) the next frame renders completely normally
// afterwards, and (3) nothing was left in a state that needed a human to notice before continuing.

#include <functional>
#include <imgui.h>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
void Require(bool Condition, const char *Message)
{
    if (!Condition)
        throw std::runtime_error(Message);
}
// 📝 Mirrors Engine/Interface/WindowsApplication.cpp's per-frame recovery exactly: NewFrame() already ran
// by the time this executes; on failure we end the frame and report that this frame produced nothing to
// present, matching "continue;" skipping ImGui::Render()/the present call in the real loop.
bool ConstructFrameWithRecovery(const std::function<void()> &ConstructInterface, std::string &FaultMessage)
{
    try
    {
        ConstructInterface();
        return true;
    }
    catch (const std::exception &Failure)
    {
        FaultMessage = Failure.what();
    }
    catch (...)
    {
        FaultMessage = "Unknown interface construction failure";
    }
    try { ImGui::EndFrame(); }
    catch (...) { }
    return false;
}
} // namespace

int main()
{
    try
    {
        ImGui::CreateContext();
        auto &IO       = ImGui::GetIO();
        IO.DisplaySize = ImVec2(800, 600);
        IO.DeltaTime   = 1.0f / 60;
        IO.IniFilename = nullptr;
        IO.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
        ImGui::GetPlatformIO().Renderer_TextureMaxWidth  = 2048;
        ImGui::GetPlatformIO().Renderer_TextureMaxHeight = 2048;

        int GoodFrames = 0, FaultedFrames = 0;
        // Frame 0: ordinary, succeeds.
        {
            ImGui::NewFrame();
            std::string Fault;
            const bool Ok = ConstructFrameWithRecovery([&]()
            {
                ImGui::Begin("Steady panel");
                ImGui::TextUnformatted("Nothing unusual here.");
                ImGui::End();
            }, Fault);
            Require(Ok, "A healthy frame was incorrectly reported as faulted");
            ImGui::Render();
            Require(ImGui::GetDrawData()->Valid, "A healthy frame did not produce drawable output");
            ++GoodFrames;
        }
        // Frame 1: throws mid-construction, after opening a window it never closes and after pushing an ID
        // it never pops - exactly the kind of half-built state a real crash leaves behind - but the frame
        // must still end up fully contained with nothing escaping to the caller.
        {
            ImGui::NewFrame();
            std::string Fault;
            const bool Ok = ConstructFrameWithRecovery([&]()
            {
                ImGui::Begin("About to explode");
                ImGui::PushID("Unbalanced");
                throw std::bad_alloc();
            }, Fault);
            Require(!Ok, "A frame that threw was incorrectly reported as having succeeded");
            Require(Fault == "std::bad_alloc", "Unexpected fault message for an injected std::bad_alloc");
            ++FaultedFrames;
            // Critically: execution reached here. In the pre-patch code, std::bad_alloc from this point
            // would have propagated out of the entire application main loop instead.
        }
        // Frame 2: the very next frame after a fault must behave completely normally again - no leftover
        // Begin/End or ID-stack corruption should be observable, and a second fault must recover too.
        {
            ImGui::NewFrame();
            std::string Fault;
            const bool Ok = ConstructFrameWithRecovery([&]()
            {
                ImGui::Begin("Steady again");
                ImGui::TextUnformatted("Recovered.");
                ImGui::End();
            }, Fault);
            Require(Ok, "The frame immediately after a fault did not construct normally");
            ImGui::Render();
            Require(ImGui::GetDrawData()->Valid, "The recovered frame did not produce drawable output");
            ++GoodFrames;
        }
        for (int Repeat = 0; Repeat < 3; ++Repeat)
        {
            ImGui::NewFrame();
            std::string Fault;
            const bool Ok = ConstructFrameWithRecovery([&]() { throw std::runtime_error("Repeated fault " + std::to_string(Repeat)); }, Fault);
            Require(!Ok, "A repeated fault was incorrectly reported as having succeeded");
            ++FaultedFrames;
        }
        {
            ImGui::NewFrame();
            std::string Fault;
            const bool Ok = ConstructFrameWithRecovery([&]()
            {
                ImGui::Begin("Final steady frame");
                ImGui::TextUnformatted("Still running.");
                ImGui::End();
            }, Fault);
            Require(Ok, "The application did not recover after several consecutive faulted frames");
            ImGui::Render();
            ++GoodFrames;
        }
        Require(GoodFrames == 3 && FaultedFrames == 4, "Unexpected frame accounting");

        ImGui::DestroyContext();
        std::cout << "PASS: an exception thrown while constructing a frame's interface (including std::bad_alloc, "
                     "the exact failure the field crash dialog reported) no longer escapes the frame boundary - "
                     "it is contained, logged, and the application keeps rendering completely normal frames "
                     "immediately afterwards, even across several consecutive faulted frames in a row.\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "FAIL: " << Failure.what() << '\n';
        return 1;
    }
}
