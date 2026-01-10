#include "core/App.h"

int main()
{
    App app;

    if (!app.run())
    {
        return 1;
    }

    return 0;
}