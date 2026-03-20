#include "stdsfx.h"
#include "kengine.h"

class SandboxApp : public Application
{
public:
    SandboxApp() = default;
    ~SandboxApp() override = default;


};

extern "C" Application* CreateApplication()
{
    return new SandboxApp();
}