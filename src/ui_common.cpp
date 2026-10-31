#include "ui.h"
#include "imgui_internal.h"

const CompPreset kCompPresets[] = {
    {"HD 1080p  ·  30 fps", 1920, 1080, 30},
    {"HD 1080p  ·  29.97 fps", 1920, 1080, 29.97},
    {"HD 1080p  ·  25 fps", 1920, 1080, 25},
    {"HD 1080p  ·  24 fps", 1920, 1080, 24},
    {"HD 1080p  ·  60 fps", 1920, 1080, 60},
    {"4K UHD  ·  30 fps", 3840, 2160, 30},
    {"4K UHD  ·  60 fps", 3840, 2160, 60},
    {"DCI 2K  ·  24 fps", 2048, 1080, 24},
    {"DCI 4K  ·  24 fps", 4096, 2160, 24},
    {"HD 720p  ·  30 fps", 1280, 720, 30},
    {"Vertical 9:16  ·  Reels / TikTok / Shorts", 1080, 1920, 30},
    {"Vertical 9:16  ·  60 fps", 1080, 1920, 60},
    {"Square 1:1  ·  Instagram", 1080, 1080, 30},
    {"Portrait 4:5  ·  Instagram Feed", 1080, 1350, 30},
    {"Cinema 2.39:1", 1920, 804, 24},
    {"Ultrawide 21:9", 2560, 1080, 30},
};
const int kCompPresetCount = (int)(sizeof(kCompPresets) / sizeof(kCompPresets[0]));

const char* const kFacts[] = {
    "AN INTERESTING FACT: THE FIRST COMPUTER-GENERATED 3D IMAGES IN A FEATURE FILM APPEARED IN FUTUREWORLD (1976).",
    "AN INTERESTING FACT: CHROMA KEYING WAS FIRST DONE WITH BLUE SCREENS AND OPTICAL PRINTERS, LONG BEFORE COMPUTERS.",
    "AN INTERESTING FACT: A CUBIC-BEZIER EASING CURVE NEEDS ONLY FOUR NUMBERS TO DESCRIBE THE WHOLE FEEL OF A MOVE.",
    "AN INTERESTING FACT: 24 FRAMES PER SECOND BECAME THE CINEMA STANDARD BECAUSE OF SOUND FILM IN THE LATE 1920S, NOT BECAUSE OF THE HUMAN EYE.",
    "AN INTERESTING FACT: GREEN SCREENS ARE GREEN BECAUSE CAMERA SENSORS HAVE TWICE AS MANY GREEN PIXELS AS RED OR BLUE ONES.",
    "AN INTERESTING FACT: THE WORD KEYFRAME COMES FROM HAND-DRAWN ANIMATION, WHERE LEAD ARTISTS DREW THE KEY POSES AND ASSISTANTS DREW THE IN-BETWEENS.",
    "AN INTERESTING FACT: NTSC VIDEO RUNS AT 29.97 FPS INSTEAD OF 30 SO THAT THE COLOR SIGNAL WOULD NOT INTERFERE WITH THE SOUND.",
    "AN INTERESTING FACT: A 180-DEGREE SHUTTER BLURS MOTION FOR HALF OF EACH FRAME - THAT SOFT SMEAR IS WHAT MAKES FOOTAGE FEEL LIKE CINEMA.",
    "AN INTERESTING FACT: GESE RENDERS EVERY EFFECT ON THE GPU IN 16-BIT FLOAT, SO EVEN TWENTY STACKED EFFECTS DO NOT CAUSE BANDING.",
    "AN INTERESTING FACT: LOST IN YONKERS (1993) WAS ONE OF THE FIRST FEATURE FILMS EDITED COMPLETELY ON A COMPUTER.",
};
const int kFactCount = (int)(sizeof(kFacts) / sizeof(kFacts[0]));

float S(float v) { return v * (g_app ? g_app->dpi * g_app->settings.uiScale : 1.f); }

// ---------------------------------------------------------------------------
// fonts & theme
// ---------------------------------------------------------------------------
static std::string firstExisting(std::initializer_list<std::string> paths) {
    for (auto& p : paths) if (fileExists(p)) return p;
    return {};
}

void uiLoadFonts(App& a) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    std::string win = "C:\\Windows\\Fonts\\";
    std::string userFonts = joinPath(localDataDir(), "..\\Microsoft\\Windows\\Fonts\\");
    std::string mainFonts = resPath("Fonts\\");
    // Source Sans 3 when installed (or dropped into resources\Fonts), else Segoe UI
    std::string ui = firstExisting({mainFonts + "SourceSans3-Regular.ttf", win + "SourceSans3-Regular.ttf", userFonts + "SourceSans3-Regular.ttf",
                                    win + "segoeui.ttf"});
    std::string uiBold = firstExisting({mainFonts + "SourceSans3-Semibold.ttf", win + "SourceSans3-Semibold.ttf", userFonts + "SourceSans3-Semibold.ttf",
                                        win + "seguisb.ttf", win + "segoeuib.ttf"});
    std::string akira = firstExisting({mainFonts + "AkiraExpanded.otf", userFonts + "Akira Expanded Demo.otf", win + "Akira Expanded Demo.otf",
                                       userFonts + "AkiraExpanded.otf", win + "ariblk.ttf"});
    std::string arial = firstExisting({win + "arial.ttf", win + "segoeui.ttf"});
    std::string mono = firstExisting({win + "consola.ttf", win + "cour.ttf"});
    a.uiFontName = fileStem(ui);
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 1;
    float base = a.settings.fontSize;
    a.fUI = ui.empty() ? io.Fonts->AddFontDefault() : io.Fonts->AddFontFromFileTTF(ui.c_str(), base, &cfg);
    a.fBold = uiBold.empty() ? a.fUI : io.Fonts->AddFontFromFileTTF(uiBold.c_str(), base, &cfg);
    a.fAkira = akira.empty() ? a.fBold : io.Fonts->AddFontFromFileTTF(akira.c_str(), 24.f, &cfg);
    a.fArial = arial.empty() ? a.fUI : io.Fonts->AddFontFromFileTTF(arial.c_str(), 14.f, &cfg);
    a.fMono = mono.empty() ? a.fUI : io.Fonts->AddFontFromFileTTF(mono.c_str(), base, &cfg);
    if (!a.fUI) a.fUI = io.Fonts->AddFontDefault();
    io.FontDefault = a.fUI;
}

void uiApplyTheme(App& a) {
    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();
    using namespace pal;
    // Dark UI: hairline-bordered fields, calmer contrast steps, a bit more air.
    // Flat fills only: no gradients, no glow, no shadows.
    st.WindowPadding = ImVec2(10, 8);
    st.FramePadding = ImVec2(7, 4);
    st.ItemSpacing = ImVec2(7, 5);
    st.ItemInnerSpacing = ImVec2(6, 4);
    st.CellPadding = ImVec2(6, 3);
    st.IndentSpacing = 16;
    st.ScrollbarSize = 10;
    st.GrabMinSize = 10;
    st.WindowBorderSize = 1;
    st.ChildBorderSize = 1;
    st.PopupBorderSize = 1;
    st.FrameBorderSize = 1;
    st.TabBorderSize = 0;
    st.TabBarBorderSize = 1;
    st.TabBarOverlineSize = 2;
    st.WindowRounding = 6;
    st.ChildRounding = 4;
    st.FrameRounding = 4;
    st.PopupRounding = 6;
    st.ScrollbarRounding = 6;
    st.GrabRounding = 4;
    st.TabRounding = 4;
    st.DockingSeparatorSize = 2;
    st.WindowMenuButtonPosition = ImGuiDir_None;
    st.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    st.SeparatorTextBorderSize = 1;
    st.SeparatorTextPadding = ImVec2(0, 4);
    st.SeparatorTextAlign = ImVec2(0.0f, 0.5f);
    st.SelectableTextAlign = ImVec2(0.0f, 0.5f);
    st.DisabledAlpha = 0.42f;

    ImVec4* c = st.Colors;
    c[ImGuiCol_Text] = v4(Text);
    c[ImGuiCol_TextDisabled] = v4(TextDim);
    c[ImGuiCol_WindowBg] = v4(Bg2);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = v4(IM_COL32(0x28, 0x28, 0x28, 0xFC));
    c[ImGuiCol_Border] = v4(IM_COL32(0x38, 0x38, 0x38, 0xFF));
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = v4(IM_COL32(0x1A, 0x1A, 0x1A, 0xFF));
    c[ImGuiCol_FrameBgHovered] = v4(IM_COL32(0x1E, 0x1E, 0x1E, 0xFF));
    c[ImGuiCol_FrameBgActive] = v4(IM_COL32(0x15, 0x15, 0x15, 0xFF));
    c[ImGuiCol_TitleBg] = v4(Bg1);
    c[ImGuiCol_TitleBgActive] = v4(Bg1);
    c[ImGuiCol_TitleBgCollapsed] = v4(Bg1);
    c[ImGuiCol_MenuBarBg] = v4(Bg2);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = v4(IM_COL32(0x40, 0x40, 0x40, 0xFF));
    c[ImGuiCol_ScrollbarGrabHovered] = v4(IM_COL32(0x52, 0x52, 0x52, 0xFF));
    c[ImGuiCol_ScrollbarGrabActive] = v4(Accent);
    c[ImGuiCol_CheckMark] = v4(White);
    c[ImGuiCol_CheckboxSelectedBg] = v4(Accent);
    c[ImGuiCol_SliderGrab] = v4(Accent);
    c[ImGuiCol_SliderGrabActive] = v4(IM_COL32(0xD0, 0x66, 0x0E, 0xFF));
    c[ImGuiCol_Button] = v4(IM_COL32(0x2E, 0x2E, 0x2E, 0xFF));
    c[ImGuiCol_ButtonHovered] = v4(IM_COL32(0x39, 0x39, 0x39, 0xFF));
    c[ImGuiCol_ButtonActive] = v4(IM_COL32(0x44, 0x44, 0x44, 0xFF));
    c[ImGuiCol_Header] = v4(AccentDark);
    c[ImGuiCol_HeaderHovered] = v4(IM_COL32(0x31, 0x31, 0x31, 0xFF));
    c[ImGuiCol_HeaderActive] = v4(IM_COL32(0x3A, 0x1C, 0x04, 0xFF));
    c[ImGuiCol_Separator] = v4(Line);
    c[ImGuiCol_SeparatorHovered] = v4(Accent);
    c[ImGuiCol_SeparatorActive] = v4(Accent);
    c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered] = v4(AccentA(160));
    c[ImGuiCol_ResizeGripActive] = v4(Accent);
    c[ImGuiCol_InputTextCursor] = v4(White);
    c[ImGuiCol_Tab] = v4(Bg1);
    c[ImGuiCol_TabHovered] = v4(Bg3);
    c[ImGuiCol_TabSelected] = v4(Bg2);
    c[ImGuiCol_TabSelectedOverline] = v4(Accent);
    c[ImGuiCol_TabDimmed] = v4(Bg1);
    c[ImGuiCol_TabDimmedSelected] = v4(Bg2);
    c[ImGuiCol_TabDimmedSelectedOverline] = v4(IM_COL32(0x4A, 0x4A, 0x4A, 0xFF));
    c[ImGuiCol_DockingPreview] = v4(AccentA(110));
    c[ImGuiCol_DockingEmptyBg] = v4(Bg0);
    c[ImGuiCol_PlotLines] = v4(Accent);
    c[ImGuiCol_PlotHistogram] = v4(Accent);
    c[ImGuiCol_TableHeaderBg] = v4(Bg2);
    c[ImGuiCol_TableBorderStrong] = v4(Line);
    c[ImGuiCol_TableBorderLight] = v4(IM_COL32(0x1A, 0x1A, 0x1A, 0xFF));
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = v4(IM_COL32(0xFF, 0xFF, 0xFF, 0x05));
    c[ImGuiCol_TextLink] = v4(Accent);
    c[ImGuiCol_TextSelectedBg] = v4(AccentA(110));
    c[ImGuiCol_TreeLines] = v4(Bg5);
    c[ImGuiCol_DragDropTarget] = v4(Accent);
    c[ImGuiCol_DragDropTargetBg] = v4(AccentA(40));
    c[ImGuiCol_NavCursor] = v4(Accent);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.55f);
    st.ScaleAllSizes(a.dpi * a.settings.uiScale);
    st.FontScaleDpi = a.dpi * a.settings.uiScale;
    st.FontSizeBase = a.settings.fontSize;
    st._NextFrameFontSizeBase = a.settings.fontSize;
}

// ---------------------------------------------------------------------------
// icons (vector drawn so they stay crisp at any DPI)
// ---------------------------------------------------------------------------
void drawIcon(ImDrawList* dl, Icon ic, ImVec2 c, float s, ImU32 col) {
    float h = s * 0.5f, t = std::max(1.0f, s / 11.f);
    auto P = [&](float x, float y) { return ImVec2(c.x + x * h, c.y + y * h); };
    switch (ic) {
    case Icon::Play: dl->AddTriangleFilled(P(-0.55f, -0.75f), P(0.75f, 0), P(-0.55f, 0.75f), col); break;
    case Icon::Pause:
        dl->AddRectFilled(P(-0.6f, -0.7f), P(-0.15f, 0.7f), col);
        dl->AddRectFilled(P(0.15f, -0.7f), P(0.6f, 0.7f), col);
        break;
    case Icon::StepBack:
        dl->AddRectFilled(P(-0.75f, -0.65f), P(-0.5f, 0.65f), col);
        dl->AddTriangleFilled(P(0.65f, -0.65f), P(0.65f, 0.65f), P(-0.4f, 0), col);
        break;
    case Icon::StepFwd:
        dl->AddRectFilled(P(0.5f, -0.65f), P(0.75f, 0.65f), col);
        dl->AddTriangleFilled(P(-0.65f, -0.65f), P(0.4f, 0), P(-0.65f, 0.65f), col);
        break;
    case Icon::ToStart:
        dl->AddRectFilled(P(-0.8f, -0.65f), P(-0.58f, 0.65f), col);
        dl->AddTriangleFilled(P(0.1f, -0.65f), P(0.1f, 0.65f), P(-0.5f, 0), col);
        dl->AddTriangleFilled(P(0.75f, -0.65f), P(0.75f, 0.65f), P(0.15f, 0), col);
        break;
    case Icon::ToEnd:
        dl->AddRectFilled(P(0.58f, -0.65f), P(0.8f, 0.65f), col);
        dl->AddTriangleFilled(P(-0.1f, -0.65f), P(0.5f, 0), P(-0.1f, 0.65f), col);
        dl->AddTriangleFilled(P(-0.75f, -0.65f), P(-0.15f, 0), P(-0.75f, 0.65f), col);
        break;
    case Icon::Loop:
        dl->PathArcTo(c, h * 0.62f, 0.4f, kPi * 1.75f, 16);
        dl->PathStroke(col, 0, t * 1.2f);
        dl->AddTriangleFilled(P(0.35f, -0.95f), P(0.95f, -0.55f), P(0.35f, -0.25f), col);
        break;
    case Icon::Eye:
        dl->PathArcTo(P(0, 0.75f), h * 1.15f, kPi * 1.22f, kPi * 1.78f, 12);
        dl->PathArcTo(P(0, -0.75f), h * 1.15f, kPi * 0.22f, kPi * 0.78f, 12);
        dl->PathStroke(col, ImDrawFlags_Closed, t);
        dl->AddCircleFilled(c, h * 0.3f, col);
        break;
    case Icon::Speaker:
        dl->AddRectFilled(P(-0.85f, -0.3f), P(-0.45f, 0.3f), col);
        dl->AddTriangleFilled(P(-0.6f, 0), P(0.1f, -0.75f), P(0.1f, 0.75f), col);
        dl->PathArcTo(P(0.1f, 0), h * 0.45f, -0.8f, 0.8f, 8);
        dl->PathStroke(col, 0, t);
        dl->PathArcTo(P(0.1f, 0), h * 0.75f, -0.8f, 0.8f, 8);
        dl->PathStroke(col, 0, t);
        break;
    case Icon::Solo:
        dl->AddCircle(c, h * 0.7f, col, 16, t);
        dl->AddCircleFilled(c, h * 0.32f, col);
        break;
    case Icon::Lock:
        dl->AddRectFilled(P(-0.6f, -0.1f), P(0.6f, 0.75f), col, 1);
        dl->PathArcTo(P(0, -0.15f), h * 0.4f, kPi, kPi * 2, 10);
        dl->PathStroke(col, 0, t * 1.2f);
        break;
    case Icon::Stopwatch:
        dl->AddCircle(P(0, 0.12f), h * 0.68f, col, 18, t);
        dl->AddLine(P(0, 0.12f), P(0, -0.3f), col, t);
        dl->AddLine(P(0, 0.12f), P(0.32f, 0.12f), col, t);
        dl->AddRectFilled(P(-0.22f, -0.95f), P(0.22f, -0.72f), col);
        break;
    case Icon::KeyPrev: dl->AddTriangleFilled(P(0.45f, -0.6f), P(0.45f, 0.6f), P(-0.45f, 0), col); break;
    case Icon::KeyNext: dl->AddTriangleFilled(P(-0.45f, -0.6f), P(0.45f, 0), P(-0.45f, 0.6f), col); break;
    case Icon::Key: dl->AddQuadFilled(P(0, -0.75f), P(0.75f, 0), P(0, 0.75f), P(-0.75f, 0), col); break;
    case Icon::Plus:
        dl->AddLine(P(-0.7f, 0), P(0.7f, 0), col, t * 1.3f);
        dl->AddLine(P(0, -0.7f), P(0, 0.7f), col, t * 1.3f);
        break;
    case Icon::Close:
        dl->AddLine(P(-0.55f, -0.55f), P(0.55f, 0.55f), col, t * 1.3f);
        dl->AddLine(P(-0.55f, 0.55f), P(0.55f, -0.55f), col, t * 1.3f);
        break;
    case Icon::Folder:
        dl->AddRectFilled(P(-0.85f, -0.6f), P(-0.1f, -0.35f), col, 1);
        dl->AddRectFilled(P(-0.85f, -0.4f), P(0.85f, 0.65f), col, 1);
        break;
    case Icon::Comp:
        dl->AddRect(P(-0.85f, -0.65f), P(0.85f, 0.65f), col, 1, 0, t);
        dl->AddRectFilled(P(-0.5f, -0.3f), P(0.1f, 0.3f), col);
        dl->AddRect(P(-0.15f, -0.05f), P(0.55f, 0.45f), col, 0, 0, t);
        break;
    case Icon::Video:
        dl->AddRectFilled(P(-0.85f, -0.5f), P(0.35f, 0.5f), col, 1);
        dl->AddTriangleFilled(P(0.35f, 0), P(0.9f, -0.45f), P(0.9f, 0.45f), col);
        break;
    case Icon::Image:
        dl->AddRect(P(-0.85f, -0.65f), P(0.85f, 0.65f), col, 1, 0, t);
        dl->AddTriangleFilled(P(-0.7f, 0.5f), P(-0.15f, -0.2f), P(0.35f, 0.5f), col);
        dl->AddCircleFilled(P(0.4f, -0.25f), h * 0.15f, col);
        break;
    case Icon::Sequence:
        dl->AddRect(P(-0.55f, -0.75f), P(0.85f, 0.4f), col, 1, 0, t);
        dl->AddRectFilled(P(-0.85f, -0.4f), P(0.55f, 0.75f), col, 1);
        break;
    case Icon::Audio:
        dl->AddLine(P(-0.1f, 0.45f), P(-0.1f, -0.75f), col, t * 1.3f);
        dl->AddLine(P(-0.1f, -0.75f), P(0.6f, -0.45f), col, t * 1.3f);
        dl->AddCircleFilled(P(-0.35f, 0.5f), h * 0.3f, col);
        break;
    case Icon::Text:
    case Icon::TextTool:
        dl->AddLine(P(-0.7f, -0.7f), P(0.7f, -0.7f), col, t * 1.6f);
        dl->AddLine(P(0, -0.7f), P(0, 0.75f), col, t * 1.6f);
        break;
    case Icon::Solid: dl->AddRectFilled(P(-0.75f, -0.75f), P(0.75f, 0.75f), col, 1); break;
    case Icon::Adjust:
        dl->AddRect(P(-0.75f, -0.75f), P(0.75f, 0.75f), col, 1, 0, t);
        dl->AddTriangleFilled(P(-0.75f, 0.75f), P(0.75f, -0.75f), P(0.75f, 0.75f), col);
        break;
    case Icon::Null:
        dl->AddRect(P(-0.7f, -0.7f), P(0.7f, 0.7f), col, 0, 0, t);
        dl->AddLine(P(-0.7f, -0.7f), P(0.7f, 0.7f), col, t);
        break;
    case Icon::Trash:
        dl->AddRect(P(-0.5f, -0.45f), P(0.5f, 0.8f), col, 1, 0, t);
        dl->AddLine(P(-0.75f, -0.5f), P(0.75f, -0.5f), col, t * 1.2f);
        dl->AddLine(P(-0.2f, -0.75f), P(0.2f, -0.75f), col, t * 1.2f);
        dl->AddLine(P(-0.15f, -0.2f), P(-0.15f, 0.55f), col, t);
        dl->AddLine(P(0.15f, -0.2f), P(0.15f, 0.55f), col, t);
        break;
    case Icon::Gear:
        for (int i = 0; i < 8; i++) {
            float an = i * kPi / 4;
            dl->AddLine(P(cosf(an) * 0.45f, sinf(an) * 0.45f), P(cosf(an) * 0.85f, sinf(an) * 0.85f), col, t * 1.6f);
        }
        dl->AddCircle(c, h * 0.5f, col, 16, t * 1.3f);
        break;
    case Icon::Search:
        dl->AddCircle(P(-0.15f, -0.15f), h * 0.5f, col, 16, t * 1.2f);
        dl->AddLine(P(0.22f, 0.22f), P(0.75f, 0.75f), col, t * 1.5f);
        break;
    case Icon::Arrow: {
        ImVec2 pts[7] = {P(-0.45f, -0.85f), P(-0.45f, 0.55f), P(-0.1f, 0.22f), P(0.15f, 0.8f), P(0.35f, 0.7f), P(0.1f, 0.12f), P(0.55f, 0.12f)};
        dl->AddConcavePolyFilled(pts, 7, col);
        break;
    }
    case Icon::Hand:
        dl->AddRectFilled(P(-0.45f, -0.1f), P(0.5f, 0.75f), col, h * 0.3f);
        for (int i = 0; i < 4; i++) dl->AddRectFilled(P(-0.45f + i * 0.25f, -0.75f + (i == 0 || i == 3 ? 0.2f : 0)), P(-0.28f + i * 0.25f, 0.1f), col, 2);
        dl->AddLine(P(-0.45f, 0.25f), P(-0.8f, -0.05f), col, t * 2);
        break;
    case Icon::Zoom:
        dl->AddCircle(P(-0.15f, -0.15f), h * 0.55f, col, 16, t * 1.2f);
        dl->AddLine(P(0.25f, 0.25f), P(0.8f, 0.8f), col, t * 1.8f);
        break;
    case Icon::Rotate:
        dl->PathArcTo(c, h * 0.62f, -kPi * 0.2f, kPi * 1.3f, 16);
        dl->PathStroke(col, 0, t * 1.2f);
        dl->AddTriangleFilled(P(0.35f, -0.85f), P(0.85f, -0.35f), P(0.3f, -0.2f), col);
        break;
    case Icon::Anchor:
        dl->AddCircle(c, h * 0.45f, col, 16, t);
        dl->AddLine(P(-0.9f, 0), P(0.9f, 0), col, t);
        dl->AddLine(P(0, -0.9f), P(0, 0.9f), col, t);
        break;
    case Icon::Rect: dl->AddRect(P(-0.75f, -0.6f), P(0.75f, 0.6f), col, 0, 0, t * 1.3f); break;
    case Icon::Ellipse: dl->AddEllipse(c, ImVec2(h * 0.78f, h * 0.6f), col, 0, 20, t * 1.3f); break;
    case Icon::Razor:
        dl->AddLine(P(-0.6f, 0.8f), P(0.5f, -0.6f), col, t * 1.4f);
        dl->AddTriangleFilled(P(0.25f, -0.85f), P(0.85f, -0.85f), P(0.75f, -0.3f), col);
        dl->AddCircle(P(-0.62f, 0.55f), h * 0.22f, col, 10, t);
        break;
    case Icon::Fx: {
        ImFont* f = ImGui::GetFont();
        float fs = s * 0.85f;
        ImVec2 ts = ImGui::CalcTextSize("fx");
        float k = fs / ImGui::GetFontSize();
        dl->AddText(f, fs, ImVec2(c.x - ts.x * k * 0.5f, c.y - fs * 0.55f), col, "fx");
        break;
    }
    case Icon::MotionBlur:
        dl->AddCircleFilled(P(0.35f, 0), h * 0.42f, col);
        dl->AddCircle(P(-0.05f, 0), h * 0.42f, (col & 0x00FFFFFF) | 0x90000000, 14, t);
        dl->AddCircle(P(-0.45f, 0), h * 0.42f, (col & 0x00FFFFFF) | 0x50000000, 14, t);
        break;
    case Icon::Snap:
        dl->PathArcTo(P(0, 0.05f), h * 0.55f, 0, kPi, 12);
        dl->PathStroke(col, 0, t * 2.2f);
        dl->AddLine(P(-0.55f, 0.05f), P(-0.55f, -0.7f), col, t * 2.2f);
        dl->AddLine(P(0.55f, 0.05f), P(0.55f, -0.7f), col, t * 2.2f);
        break;
    case Icon::Home:
        dl->AddTriangleFilled(P(-0.85f, -0.05f), P(0, -0.85f), P(0.85f, -0.05f), col);
        dl->AddRectFilled(P(-0.55f, -0.1f), P(0.55f, 0.75f), col);
        break;
    case Icon::Grid:
        for (int i = -1; i <= 1; i += 2) {
            dl->AddLine(P(i * 0.3f, -0.8f), P(i * 0.3f, 0.8f), col, t);
            dl->AddLine(P(-0.8f, i * 0.3f), P(0.8f, i * 0.3f), col, t);
        }
        break;
    case Icon::Safe:
        dl->AddRect(P(-0.85f, -0.65f), P(0.85f, 0.65f), col, 0, 0, t);
        dl->AddRect(P(-0.55f, -0.38f), P(0.55f, 0.38f), col, 0, 0, t);
        break;
    case Icon::Checker:
        for (int y = 0; y < 3; y++)
            for (int x = 0; x < 3; x++)
                if ((x + y) % 2 == 0) dl->AddRectFilled(P(-0.75f + x * 0.5f, -0.75f + y * 0.5f), P(-0.25f + x * 0.5f, -0.25f + y * 0.5f), col);
        dl->AddRect(P(-0.75f, -0.75f), P(0.75f, 0.75f), col, 0, 0, t);
        break;
    case Icon::Camera:
        dl->AddRectFilled(P(-0.85f, -0.45f), P(0.85f, 0.65f), col, 2);
        dl->AddRectFilled(P(-0.3f, -0.7f), P(0.3f, -0.4f), col);
        dl->AddCircleFilled(P(0, 0.1f), h * 0.32f, IM_COL32(0x23, 0x23, 0x23, 0xFF));
        break;
    case Icon::Plug:
        dl->AddRectFilled(P(-0.5f, -0.3f), P(0.5f, 0.35f), col, 2);
        dl->AddLine(P(-0.25f, -0.3f), P(-0.25f, -0.8f), col, t * 1.5f);
        dl->AddLine(P(0.25f, -0.3f), P(0.25f, -0.8f), col, t * 1.5f);
        dl->AddLine(P(0, 0.35f), P(0, 0.85f), col, t * 1.5f);
        break;
    case Icon::ChevronRight: dl->AddTriangleFilled(P(-0.3f, -0.55f), P(0.45f, 0), P(-0.3f, 0.55f), col); break;
    case Icon::ChevronDown: dl->AddTriangleFilled(P(-0.55f, -0.3f), P(0.55f, -0.3f), P(0, 0.45f), col); break;
    case Icon::Reset:
        dl->PathArcTo(c, h * 0.6f, kPi * 0.6f, kPi * 2.2f, 16);
        dl->PathStroke(col, 0, t * 1.2f);
        dl->AddTriangleFilled(P(-0.75f, -0.05f), P(-0.25f, 0.25f), P(-0.5f, 0.55f), col);
        break;
    case Icon::Shy:
        dl->AddCircle(c, h * 0.65f, col, 16, t);
        dl->AddLine(P(-0.3f, -0.1f), P(-0.1f, -0.1f), col, t);
        dl->AddLine(P(0.1f, -0.1f), P(0.3f, -0.1f), col, t);
        dl->AddLine(P(-0.25f, 0.3f), P(0.25f, 0.3f), col, t);
        break;
    case Icon::Freeze:  // snowflake
        for (int i = 0; i < 3; i++) {
            float an = i * kPi / 3 + kPi / 2;
            ImVec2 d(cosf(an), sinf(an));
            dl->AddLine(c - d * (h * 0.85f), c + d * (h * 0.85f), col, t * 1.3f);
            for (float sgn : {-1.f, 1.f}) {
                ImVec2 tip = c + d * (h * 0.55f * sgn);
                ImVec2 n(-d.y, d.x);
                dl->AddLine(tip, tip + (d * sgn + n) * (h * 0.22f), col, t);
                dl->AddLine(tip, tip + (d * sgn - n) * (h * 0.22f), col, t);
            }
        }
        break;
    case Icon::Link:
        dl->AddCircle(P(-0.3f, 0.3f), h * 0.35f, col, 12, t * 1.2f);
        dl->AddCircle(P(0.3f, -0.3f), h * 0.35f, col, 12, t * 1.2f);
        break;
    case Icon::Cube: {  // isometric cube outline
        ImVec2 top = P(0, -0.85f), l = P(-0.75f, -0.42f), r = P(0.75f, -0.42f), mid = P(0, 0.0f), bl = P(-0.75f, 0.42f), br = P(0.75f, 0.42f), bot = P(0, 0.85f);
        dl->AddLine(top, l, col, t * 1.3f);
        dl->AddLine(top, r, col, t * 1.3f);
        dl->AddLine(l, mid, col, t * 1.3f);
        dl->AddLine(r, mid, col, t * 1.3f);
        dl->AddLine(mid, bot, col, t * 1.3f);
        dl->AddLine(l, bl, col, t * 1.3f);
        dl->AddLine(r, br, col, t * 1.3f);
        dl->AddLine(bl, bot, col, t * 1.3f);
        dl->AddLine(br, bot, col, t * 1.3f);
        break;
    }
    case Icon::Fullscreen:  // four corner brackets
        for (int sx = -1; sx <= 1; sx += 2)
            for (int sy = -1; sy <= 1; sy += 2) {
                ImVec2 cn = P(0.75f * sx, 0.6f * sy);
                dl->AddLine(cn, cn - ImVec2(sx * h * 0.42f, 0), col, t * 1.4f);
                dl->AddLine(cn, cn - ImVec2(0, sy * h * 0.36f), col, t * 1.4f);
            }
        break;
    case Icon::Light:  // sun: disc with rays
        dl->AddCircleFilled(c, h * 0.36f, col);
        for (int i = 0; i < 8; i++) {
            float an = i * kPi / 4;
            ImVec2 d(cosf(an), sinf(an));
            dl->AddLine(c + d * (h * 0.55f), c + d * (h * 0.88f), col, t * 1.3f);
        }
        break;
    case Icon::Graph:  // s-curve with two handles
        dl->AddLine(P(-0.8f, 0.8f), P(0.8f, 0.8f), col, t);
        dl->AddLine(P(-0.8f, 0.8f), P(-0.8f, -0.8f), col, t);
        dl->AddBezierCubic(P(-0.7f, 0.7f), P(0.1f, 0.7f), P(-0.1f, -0.7f), P(0.7f, -0.7f), col, t * 1.6f, 16);
        dl->AddCircleFilled(P(0.1f, 0.7f), h * 0.14f, col);
        dl->AddCircleFilled(P(-0.1f, -0.7f), h * 0.14f, col);
        break;
    }
}

void tooltip(const char* text) {
    if (text && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", text);
}

bool iconButton(const char* id, Icon ic, bool active, const char* tip, float size) {
    float h = size > 0 ? size : ImGui::GetFrameHeight();
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(id, ImVec2(h, h));
    bool hov = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (held) dl->AddRectFilled(p, p + ImVec2(h, h), IM_COL32(0x45, 0x45, 0x45, 0xFF), S(2));
    else if (hov) dl->AddRectFilled(p, p + ImVec2(h, h), IM_COL32(0x38, 0x38, 0x38, 0xFF), S(2));
    ImU32 col = active ? pal::Accent : (hov ? pal::White : pal::TextDim);
    drawIcon(dl, ic, p + ImVec2(h * 0.5f, h * 0.5f), h * 0.62f, col);
    tooltip(tip);
    return pressed;
}

bool iconToggle(const char* id, Icon ic, bool* v, const char* tip) {
    bool r = iconButton(id, ic, *v, tip);
    if (r) *v = !*v;
    return r;
}

void sectionLabel(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, pal::v4(pal::TextDim));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

bool accentButton(const char* label, ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_Button, pal::v4(pal::Accent));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, pal::v4(IM_COL32(0xC8, 0x62, 0x08, 0xFF)));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, pal::v4(IM_COL32(0xA0, 0x48, 0x00, 0xFF)));
    ImGui::PushStyleColor(ImGuiCol_Text, pal::v4(pal::White));
    bool r = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return r;
}

bool scrubFloat(const char* id, float* v, float speed, float mn, float mx, const char* fmt, bool accent) {
    char buf[64];
    snprintf(buf, sizeof(buf), fmt, *v);
    float w = ImGui::CalcTextSize(buf).x + S(6);
    ImGui::SetNextItemWidth(std::max(w, S(22)));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, pal::v4(IM_COL32(0xFF, 0xFF, 0xFF, 0x10)));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, pal::v4(IM_COL32(0x10, 0x10, 0x10, 0xFF)));
    ImGui::PushStyleColor(ImGuiCol_Text, pal::v4(accent ? pal::Accent : pal::Text));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(2), ImGui::GetStyle().FramePadding.y));
    bool r = ImGui::DragFloat(id, v, speed, mn, mx, fmt, ImGuiSliderFlags_NoRoundToFormat);
    if (ImGui::IsItemHovered() && !ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    return r;
}

std::string timeLabel(double t, double fps) {
    if (g_app && g_app->settings.timeDisplay == 1) return strf("%05lld", (long long)timeToFrame(t, fps));
    return fmtTimecode(t, fps);
}

bool timecodeField(const char* id, double* t, double fps, ImU32 col, float fontScale) {
    ImGuiID gid = ImGui::GetID(id);
    static ImGuiID editing = 0;
    static char buf[64];
    bool changed = false;
    std::string label = timeLabel(*t, fps);
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * fontScale);
    if (editing == gid) {
        ImGui::SetNextItemWidth(ImGui::CalcTextSize("00:00:00:000").x + S(8));
        ImGui::SetKeyboardFocusHere();
        if (ImGui::InputText(id, buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
            bool ok = false;
            double v = parseTimecode(buf, fps, &ok);
            if (ok) { *t = v; changed = true; }
            editing = 0;
        } else if (ImGui::IsItemDeactivated()) editing = 0;
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, pal::v4(col));
        ImGui::TextUnformatted(label.c_str());
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsItemClicked()) { editing = gid; snprintf(buf, sizeof(buf), "%s", label.c_str()); }
        tooltip("Click to type a time (H:MM:SS:FF or a frame number)");
    }
    ImGui::PopFont();
    return changed;
}

// ---------------------------------------------------------------------------
// property editing
// ---------------------------------------------------------------------------
bool stopwatch(const char* id, Prop& p, double lt) {
    bool r = iconButton(id, Icon::Stopwatch, p.animated, p.animated ? "Disable animation (removes keyframes)" : "Enable keyframes",
                        ImGui::GetFrameHeight() * 0.9f);
    if (r) p.setAnimated(!p.animated, lt);
    return r;
}

bool keyNav(const char* id, Prop& p, double lt, double layerStart, double fps) {
    if (!p.animated) return false;
    ImGui::PushID(id);
    float h = ImGui::GetFrameHeight() * 0.8f;
    bool changed = false;
    int prev = -1, next = -1;
    for (int i = 0; i < (int)p.keys.size(); i++) {
        if (p.keys[i].t < lt - 1e-4) prev = i;
        if (p.keys[i].t > lt + 1e-4 && next < 0) next = i;
    }
    if (iconButton("prev", Icon::KeyPrev, false, "Previous keyframe", h) && prev >= 0) g_app->setTime(layerStart + p.keys[prev].t);
    ImGui::SameLine(0, 0);
    int at = p.keyAt(lt, 0.5 / fps);
    if (iconButton("key", Icon::Key, at >= 0, at >= 0 ? "Remove keyframe" : "Add keyframe", h)) {
        if (at >= 0) {
            p.keys.erase(p.keys.begin() + at);
            if (p.keys.empty()) p.animated = false;
        } else {
            float cur[4];
            p.eval(lt, cur);
            p.addKey(lt, cur);
        }
        changed = true;
    }
    ImGui::SameLine(0, 0);
    if (iconButton("next", Icon::KeyNext, false, "Next keyframe", h) && next >= 0) g_app->setTime(layerStart + p.keys[next].t);
    ImGui::PopID();
    return changed;
}

PropUI uiForParam(const ParamDef& d, ImVec2 ls) {
    PropUI u;
    u.label = d.label.c_str();
    switch (d.type) {
    case ParamType::Float:
        u.kind = d.unit == "%" ? 6 : 0;
        u.unit = d.unit.c_str();
        if (d.hasRange) { u.mn = d.minv; u.mx = d.maxv; u.speed = std::max(0.001f, (d.maxv - d.minv) / 400.f); }
        else u.speed = 0.1f;
        break;
    case ParamType::Angle: u.kind = 5; u.speed = 0.5f; break;
    case ParamType::Point: u.kind = 7; u.pxScale = ImVec2(std::max(1.f, ls.x), std::max(1.f, ls.y)); break;
    case ParamType::Color: u.kind = 2; break;
    case ParamType::Bool: u.kind = 3; break;
    case ParamType::Choice: u.kind = 4; u.choices = &d.choices; break;
    }
    return u;
}

bool propValue(const char* id, Prop& p, double lt, const PropUI& ui, float width) {
    ImGui::PushID(id);
    float v[4];
    p.eval(lt, v);
    bool changed = false;
    switch (ui.kind) {
    case 0:
    case 5:
    case 6:
    case 9: {
        char fmt[32];
        float range = ui.mx - ui.mn;
        const char* prec = (ui.mx < 1e8f && range <= 2.f) ? "%.3f" : (range <= 20.f ? "%.2f" : "%.1f");
        if (ui.kind == 5) snprintf(fmt, sizeof(fmt), "%%.1f°");
        else if (ui.kind == 6) snprintf(fmt, sizeof(fmt), "%%.1f%%%%");
        else if (ui.kind == 9) snprintf(fmt, sizeof(fmt), "%%+.2f dB");
        else snprintf(fmt, sizeof(fmt), "%s%s%s", prec, ui.unit[0] ? " " : "", ui.unit);
        changed = scrubFloat("##v", &v[0], ui.speed, ui.mn, ui.mx, fmt);
        break;
    }
    case 1:
    case 8: {
        const char* fmt = ui.kind == 8 ? "%.1f%%" : "%.1f";
        changed |= scrubFloat("##x", &v[0], ui.speed, ui.mn, ui.mx, fmt);
        ImGui::SameLine(0, S(2));
        ImGui::TextDisabled(",");
        ImGui::SameLine(0, S(2));
        changed |= scrubFloat("##y", &v[1], ui.speed, ui.mn, ui.mx, fmt);
        break;
    }
    case 7: {
        float px[2] = {v[0] * ui.pxScale.x, v[1] * ui.pxScale.y};
        bool c1 = scrubFloat("##x", &px[0], 1.f, -1e6f, 1e6f, "%.1f");
        ImGui::SameLine(0, S(2));
        ImGui::TextDisabled(",");
        ImGui::SameLine(0, S(2));
        bool c2 = scrubFloat("##y", &px[1], 1.f, -1e6f, 1e6f, "%.1f");
        if (c1 || c2) { v[0] = px[0] / ui.pxScale.x; v[1] = px[1] / ui.pxScale.y; changed = true; }
        break;
    }
    case 2: {
        changed = ImGui::ColorEdit4("##c", v, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf |
                                                  ImGuiColorEditFlags_NoLabel);
        break;
    }
    case 3: {
        bool b = v[0] > 0.5f;
        if (ImGui::Checkbox("##b", &b)) { v[0] = b ? 1.f : 0.f; changed = true; }
        break;
    }
    case 4: {
        int cur = (int)(v[0] + 0.5f);
        ImGui::SetNextItemWidth(std::max(S(90), width));
        const auto& ch = *ui.choices;
        if (ImGui::BeginCombo("##ch", cur >= 0 && cur < (int)ch.size() ? ch[cur].c_str() : "?")) {
            for (int i = 0; i < (int)ch.size(); i++)
                if (ImGui::Selectable(ch[i].c_str(), i == cur)) { v[0] = (float)i; changed = true; }
            ImGui::EndCombo();
        }
        break;
    }
    }
    if (changed) {
        if (ui.kind != 4 && ui.kind != 3 && ui.kind != 2 && ui.kind != 7) {
            v[0] = clampv(v[0], ui.mn, ui.mx);
            if (ui.kind == 1 || ui.kind == 8) v[1] = clampv(v[1], ui.mn, ui.mx);
        }
        p.set(lt, v);
        g_app->markChanged();
    }
    ImGui::PopID();
    return changed;
}
