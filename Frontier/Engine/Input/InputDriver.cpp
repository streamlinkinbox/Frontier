/*==========================================================================================================================================
                                                             INPUTDRIVER.CPP
==========================================================================================================================================*/

#include "Frontier/Engine/Input/InputDriver.h"

namespace Frontier
{

InputPacket InputDriver::DriveFromState(const RawInputState& State, float StepSeconds) noexcept
{
    const InputPacket Packet = Encoder.EncodeState(State, StepSeconds);
    if (BoundAgent)
        BoundAgent->ApplyInputPacket(Packet, StepSeconds);
    return Packet;
}

#if defined(FRONTIER_WITH_GLFW)
InputPacket InputDriver::DriveFromWindow(GLFWwindow* Window, float StepSeconds, InputControlMode Mode) noexcept
{
    const InputPacket Packet = Encoder.SampleGlfwWindow(Window, StepSeconds, Mode);
    if (BoundAgent)
        BoundAgent->ApplyInputPacket(Packet, StepSeconds);
    return Packet;
}
#endif

void InputDriver::DriveFromPacket(const InputPacket& Packet, float StepSeconds) noexcept
{
    if (BoundAgent)
        BoundAgent->ApplyInputPacket(Packet, StepSeconds);
}

} // namespace Frontier
