#pragma once
// CPU proof backend, not production graphics code. Samples managed ImGui pixels,
// or an explicitly supplied legacy scene texture. Unknown IDs are errors.
#include <imgui.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
namespace FrontierProof {
struct Image { const unsigned char* Pixels; int Width, Height, Channels; };
inline void AcknowledgeTextures(int RetirementFrames = 3) {
    for (auto* T : ImGui::GetPlatformIO().Textures) {
        if (T->Status == ImTextureStatus_WantCreate || T->Status == ImTextureStatus_WantUpdates) {
            T->SetTexID(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(T)));
            T->SetStatus(ImTextureStatus_OK);
        } else if (T->Status == ImTextureStatus_WantDestroy && T->UnusedFrames >= RetirementFrames) {
            T->SetTexID(ImTextureID_Invalid);
            T->SetStatus(ImTextureStatus_Destroyed);
        }
    }
}
inline std::array<float, 4> Sample(Image S, float U, float V) {
    const float X = U * S.Width - 0.5f, Y = V * S.Height - 0.5f;
    const int X0 = static_cast<int>(std::floor(X)), Y0 = static_cast<int>(std::floor(Y));
    const float Fx = X - X0, Fy = Y - Y0;
    std::array<float, 4> Result{};
    for (int J = 0; J < 2; ++J) for (int I = 0; I < 2; ++I) {
        auto* P = S.Pixels + (std::clamp(Y0 + J, 0, S.Height - 1) * S.Width + std::clamp(X0 + I, 0, S.Width - 1)) * S.Channels;
        const float Weight = (I ? Fx : 1-Fx) * (J ? Fy : 1-Fy);
        for (int C = 0; C < 4; ++C) Result[C] += Weight * (S.Channels == 1 ? (C == 3 ? P[0] / 255.0f : 1.0f) : P[C] / 255.0f);
    }
    return Result;
}
inline double Edge(ImVec2 A, ImVec2 B, ImVec2 P) { return double(B.x-A.x)*(P.y-A.y)-double(B.y-A.y)*(P.x-A.x); }
inline bool TopLeft(ImVec2 A, ImVec2 B) { return B.y < A.y || (B.y == A.y && B.x > A.x); }
inline void Draw(const ImDrawList* List, unsigned char* Rgb, int Width, int Height, ImVec2 Origin, ImVec2 Scale,
                 ImTextureID SceneId = ImTextureID_Invalid, Image Scene = {}, const ImDrawCmd* OnlyCommand = nullptr) {
    for (const auto& Cmd : List->CmdBuffer) {
        if (OnlyCommand && &Cmd != OnlyCommand) continue;
        if (Cmd.UserCallback) {
            if (Cmd.UserCallback != ImDrawCallback_ResetRenderState) Cmd.UserCallback(List, &Cmd);
            continue;
        }
        if (!Cmd.ElemCount) continue;
        Image Sheet{};
        if (auto* T = Cmd.TexRef._TexData) Sheet = {T->Pixels, T->Width, T->Height, T->BytesPerPixel};
        else if (SceneId != ImTextureID_Invalid && Cmd.TexRef._TexID == SceneId) Sheet = Scene;
        if (!Sheet.Pixels || Sheet.Width <= 0 || Sheet.Height <= 0 || (Sheet.Channels != 1 && Sheet.Channels != 4))
            throw std::runtime_error("CPU proof: unknown or invalid texture");
        const float ClipL = (Cmd.ClipRect.x-Origin.x)*Scale.x, ClipT = (Cmd.ClipRect.y-Origin.y)*Scale.y;
        const float ClipR = (Cmd.ClipRect.z-Origin.x)*Scale.x, ClipB = (Cmd.ClipRect.w-Origin.y)*Scale.y;
        for (unsigned I = 0; I < Cmd.ElemCount; I += 3) {
            ImDrawVert V[3];
            for (int J = 0; J < 3; ++J) {
                V[J] = List->VtxBuffer[List->IdxBuffer[Cmd.IdxOffset+I+J]+Cmd.VtxOffset];
                V[J].pos = ImVec2((V[J].pos.x-Origin.x)*Scale.x, (V[J].pos.y-Origin.y)*Scale.y);
            }
            double Area = Edge(V[0].pos,V[1].pos,V[2].pos);
            if (Area == 0) continue;
            if (Area < 0) { std::swap(V[1],V[2]); Area = -Area; }
            int L = std::max(0, int(std::floor(std::max(ClipL,std::min({V[0].pos.x,V[1].pos.x,V[2].pos.x})))));
            int T = std::max(0, int(std::floor(std::max(ClipT,std::min({V[0].pos.y,V[1].pos.y,V[2].pos.y})))));
            int R = std::min(Width, int(std::ceil(std::min(ClipR,std::max({V[0].pos.x,V[1].pos.x,V[2].pos.x})))));
            int B = std::min(Height, int(std::ceil(std::min(ClipB,std::max({V[0].pos.y,V[1].pos.y,V[2].pos.y})))));
            for (int Y = T; Y < B; ++Y) for (int X = L; X < R; ++X) {
                ImVec2 P(X+0.5f,Y+0.5f);
                if (P.x < ClipL || P.x >= ClipR || P.y < ClipT || P.y >= ClipB) continue;
                double W[3] = {Edge(V[1].pos,V[2].pos,P),Edge(V[2].pos,V[0].pos,P),Edge(V[0].pos,V[1].pos,P)};
                bool Inside = true;
                for (int J = 0; J < 3; ++J)
                    if (W[J] < 0 || (W[J] == 0 && !TopLeft(V[(J+1)%3].pos,V[(J+2)%3].pos))) Inside = false;
                if (!Inside) continue;
                float U=0, VV=0; std::array<float,4> Tint{};
                for (int J=0; J<3; ++J) {
                    float F = float(W[J]/Area); U += F*V[J].uv.x; VV += F*V[J].uv.y;
                    const int Shifts[] = {IM_COL32_R_SHIFT,IM_COL32_G_SHIFT,IM_COL32_B_SHIFT,IM_COL32_A_SHIFT};
                    for (int C=0; C<4; ++C) Tint[C] += F*((V[J].col>>Shifts[C])&255)/255.0f;
                }
                auto Texel = Sample(Sheet,U,VV);
                float Alpha = Texel[3]*Tint[3];
                for (int C=0; C<3; ++C) {
                    auto& D = Rgb[(Y*Width+X)*3+C];
                    D = static_cast<unsigned char>(std::clamp(std::lround(Texel[C]*Tint[C]*Alpha*255 + D*(1-Alpha)),0l,255l));
                }
            }
        }
    }
}
}
