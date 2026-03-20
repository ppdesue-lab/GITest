#include "ImGuiLayer.h"
#include "Log.h"

ImGuiLayer::ImGuiLayer()
    : Layer("ImGuiLayer")
{
}

void ImGuiLayer::OnAttach()
{
    INFO("ImGuiLayer attached");
    // Initialize ImGui here
    // ImGui::CreateContext();
    // ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
}

void ImGuiLayer::OnDetach()
{
    INFO("ImGuiLayer detached");
    // Cleanup ImGui here
    // ImGui::DestroyContext();
}

void ImGuiLayer::OnUpdate()
{
    // Update ImGui state
}

void ImGuiLayer::OnRender()
{
    // Render ImGui
}

void ImGuiLayer::OnEvent(Event& event)
{
    // Handle events for ImGui
}

void ImGuiLayer::Begin()
{
    // Begin ImGui frame
    // ImGui::NewFrame();
}

void ImGuiLayer::End()
{
    // End ImGui frame and render
    // ImGui::Render();
}
