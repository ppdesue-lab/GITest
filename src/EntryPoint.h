#pragma once

#include "Application.h"

extern "C" Application* CreateApplication();

int main()
{
    Application* app = CreateApplication();
    if (app)
    {
        app->Run();
        delete app;
    }
}