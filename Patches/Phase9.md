# Phase 9 — the validated route

## The scenarios, and the keys that drive them

Every key is `SLATE_DEBUG` only and drives the **ordinary** path. A scenario with a route of its own
would test that route and nothing else.

| Key  | Scenario           | What it exercises                                                    |
|------|--------------------|----------------------------------------------------------------------|
| F9   | Recover display    | One chain re-establishment — the resize path, without a resize        |
| F10  | Resize storm       | Re-establishes every tick until pressed again — the freeze case       |
| F11  | Recover device     | Retires and rebuilds the device tier; the host rebuilds its interface |
| F12  | State reports      | Prints the diagnostic verdict mid-run, without exiting                |

Unkeyed, and driven from the window manager: resize by dragging, minimise and restore, move the window
between displays of different density, and close it.

## Reading the result

A run ends in one line from `HostLifecycle::StateDiagnostics`, and the exit code is the serious count.

    InterfaceValidationHost — diagnostics: 0 serious, 12 retained, 47 appended, 47 arrived, 0 discarded

- `0 serious` with `Negotiated` true is a clean run.
- `no diagnostic layer was negotiated` means the run was **not watched** — not that it was clean.
- `discarded` above zero means the register overflowed and an early error may already be gone.

## What phase 9 changed, and why

🔴 **`DisplayScheduler::Present` reported `VK_ERROR_OUT_OF_DATE_KHR` as success.** All three accepted
results delivered `true`, so `HostLifecycle::Surrender`'s re-establishment branch — whose comment says a
refused present rebuilds the chain — was unreachable on a resize. The chain was rebuilt a tick late by the
extent test, or never, when the display outgrew a chain the window extent had not moved. `Present` now
delivers `false` for an outgrown chain and the caller reads the value.

🔴 **A withdrawn tick never advanced the rotation.** `Cycle.Advance` lives in `Surrender` alone, so the
`Reclaimed` path in `Await` returned without turning the cycle. Every subsequent tick withdrew against the
same slot, nothing was presented, and the artist saw the last good image frozen on screen for as long as
the chain kept reporting itself outgrown. This is the freeze.

🔴 **Three paths acquired an image and returned without submitting.** `Cycle.Standing`, `Cycle.Arm` and
`Commands.Surrender` each refused with `ImageArrived` signalled and no waiter. A binary semaphore is
unsignalled only by a wait, so the next acquire on that slot signalled it twice; and a chain destroyed with
an acquire outstanding is `VUID-vkDestroySwapchainKHR-swapchain-01282`, which drivers report as
`VK_ERROR_DEVICE_LOST` on a later submit — several ticks after the resize that caused it.
`SettleAcquisition` retires the acquisition on every such path.

⚠️ **`RecoverDisplay` returned early on a zero extent**, before `AdoptExtent`. That part is correct — the
extent must stay unadopted so the restore re-establishes — but it is now stated as deliberate, and nothing
on that path clears `LoopStanding`.

🔴 **Device loss was terminal at every site.** A driver reset, which is ordinary on a machine whose display
driver updates while Slate runs, closed the application. `RecoverDevice` retires the device tier and
rebuilds it, leaving the window, instance and surface standing — `Construct` opens a window at step ②, so
recovering through it would stand a second window in front of the artist. Bounded at
`DeviceRecoveryCeiling`, because a device lost twice is a driver that is not coming back.

🔴 **Synchronisation validation was off.** It is the only check that catches this whole class of defect,
and it is requested through `VK_EXT_layer_settings` rather than vkconfig or an environment variable, so a
validation run reproduces on a machine that was never configured for one.

## Not verified

⚠️ Everything above is verified by reading and by 16 state-model checks, and compiles clean in both
configurations. **None of it has been run against a device.** The freeze and the swallowed
`VK_ERROR_OUT_OF_DATE_KHR` are provable from the source; whether they were the freeze observed on the
artist's machine is not, until F10 runs there.
