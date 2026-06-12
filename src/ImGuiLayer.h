#pragma once

#include <base.h>
#include "Layer.h"
#include "MouseEvent.h"
#include "KeyEvent.h"
#include <Camera/Camera.h>
#include <Transform.h>
#include <imgui.h>
#include <deque>
#include <string>
#include <filesystem>
#include <vector>

struct ConsoleMessage
{
    ImVec4 Color;
    std::string Message;
};

class ImGuiLayer : public Layer
{
public:
    ImGuiLayer();
    virtual ~ImGuiLayer() = default;

    virtual void OnAttach() override;
    virtual void OnDetach() override;
    virtual void OnUpdate() override;
    virtual void OnImGuiRender() override;
    virtual void OnEvent(Event& event) override;

    void SetDarkThemeColors();

    void Begin();
    void End();

    static void AddConsoleMessage(const ImVec4& color, const std::string& message);

private:
    void DrawMenuBar();
    void DrawEditorLayout(ImVec2 pos, ImVec2 size, float menuBarHeight);

    void DrawProjectPanel();
    void DrawPropertiesPanel();
    void DrawContentBrowser();
    void DrawConsolePanel();
    void DrawViewportPanel();
    void DrawShadowDebugWindow();
    void DrawProbeGIDebugWindow();
    void DrawSSAODebugWindow();
    void DrawFXAADebugWindow();
    void DrawSVGFDenoiserWindow();
    void DrawPBRIBLDebugWindow();
    void OpenModelFile();
    void OpenGCodeFile();
    bool IsSupportedModelFile(const std::filesystem::path& filepath) const;
    bool IsSupportedGCodeFile(const std::filesystem::path& filepath) const;

    bool OnMouseButtonDown(MouseButtonPressedEvent& e);
    bool OnMouseButtonUp(MouseButtonReleasedEvent& e);
    bool OnMouseMove(MouseMovedEvent& e);

private:
    bool m_LeftDownCamera = false;
    bool m_LeftDownGizmo = false;

    glm::vec2 m_MousePos;
	Transform g_DefaultTransform;
	Ref<Camera> m_Camera;
    float m_Time = 0.0f;
    bool m_UpdateGizmo = false;

    // Panel state
    std::string m_ContentBrowserPath;
    std::string m_SelectedFile;
    std::string m_CurrentDir;
    std::string m_ContentBrowserCachedDir;
    std::vector<std::filesystem::path> m_ContentBrowserDirectories;
    std::vector<std::filesystem::path> m_ContentBrowserFiles;
    bool m_ContentBrowserNeedsRefresh = true;

    // Console
    static std::deque<ConsoleMessage> s_ConsoleMessages;
    static const size_t MAX_CONSOLE_MESSAGES = 500;
    char m_ConsoleFilter[128] = {};
};
